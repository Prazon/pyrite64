#include "prefabEventGraphEditor.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "ImNodeFlow.h"
#include "json.hpp"
#include "IconsMaterialDesignIcons.h"

#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"
#include "../../../../project/graph/nodes/baseNode.h"
#include "../../../../project/graph/nodes/nodePrefabEvent.h"
#include "../../../../project/graph/nodes/nodePrefabFunc.h"
#include "../../../../project/graph/nodes/nodePrefabVarGet.h"
#include "../../../dragDropPayloads.h"

namespace
{
  constexpr ImVec2 DEF_WIN_SIZE{960, 600};

  // Right-click / dropped-link palette: lets the user create any registered
  // graph node type. Future work will filter this to prefab-relevant nodes
  // (events, function calls, variable get/set) once those types exist.
  void drawCreatePopup(Project::Graph::Graph &graph, ImFlow::Pin* pin)
  {
    ImGui::Text("Create New");
    ImGui::Separator();
    auto &names = Project::Graph::Graph::getNodeNames();
    for (size_t i = 0; i < names.size(); ++i) {
      if (ImGui::Selectable(names[i].c_str())) {
        auto newPos = pin ? pin->getParent()->getPos() : ImVec2{0, 0};
        newPos.x += 150;
        auto node = graph.addNode(static_cast<uint32_t>(i), newPos);
        auto &ins = node->getIns();
        if (pin && !ins.empty()) ins[0]->createLink(pin);
        node->setPos(newPos);
        ImGui::CloseCurrentPopup();
      }
    }
  }
}

Editor::PrefabEventGraphEditor::PrefabEventGraphEditor(uint64_t prefabAssetUUID)
  : assetUUID(prefabAssetUUID)
{
  // Apply the same pin styling NodeEditor does — keeps prefab event graphs
  // visually consistent with standalone node-graph assets.
  auto &stylePin = *Project::Graph::Node::PIN_STYLE_LOGIC;
  stylePin = ImFlow::PinStyle{
    IM_COL32(0xAA, 0xAA, 0xAA, 0xFF),
    3, 6.0f, 7.0f, 6.5f, 1.3f
  };
  stylePin.extra.padding.y = 16;

  auto &stylePinVal = *Project::Graph::Node::PIN_STYLE_VALUE;
  stylePinVal = ImFlow::PinStyle{
    IM_COL32(0xFF, 0x99, 0x55, 0xFF),
    0, 6.0f, 7.0f, 6.5f, 1.3f
  };
  stylePinVal.extra.padding.y = 16;

  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || !asset->prefab) return;

  // Load existing graph state if any. When the prefab has no graph yet,
  // seed default event entry nodes — same idea as GameMaker's pre-listed
  // object events (Create / Step / Draw): users open the graph to a layout
  // that already shows the common entry points, ready to be wired up.
  const std::string &jsonState = asset->prefab->eventGraphJSON;
  if (!jsonState.empty()) {
    graph.deserialize(jsonState);
  } else {
    // Find the registry indices for our PrefabEvent nodes by name. The
    // alternative — hardcoding the index — is fragile against future
    // additions to NODE_TABLE.
    auto &names = Project::Graph::Graph::getNodeNames();
    auto findIdx = [&](const char* needle) -> int {
      for (size_t i = 0; i < names.size(); ++i) {
        if (names[i].find(needle) != std::string::npos) return static_cast<int>(i);
      }
      return -1;
    };
    int eventIdx = findIdx("Event");
    if (eventIdx >= 0) {
      using Kind = Project::Graph::Node::PrefabEvent::Kind;
      struct Seed { Kind kind; float y; };
      Seed seeds[] = {
        {Kind::Ready,   40.0f},
        {Kind::Enable,  180.0f},
        {Kind::Disable, 320.0f},
      };
      for (const auto &s : seeds) {
        auto node = graph.addNode(static_cast<uint32_t>(eventIdx), {40.0f, s.y});
        if (auto evt = dynamic_cast<Project::Graph::Node::PrefabEvent*>(node.get())) {
          evt->kind = s.kind;
          evt->updateTitle();
        }
      }
    }
  }
  // savedState reflects whatever's on screen now — including seeded nodes.
  // That way the editor doesn't immediately read as dirty on first open;
  // the seeds become "the saved state" until the user actually edits.
  savedState = graph.serialize();
  // Persist the seeded layout immediately so a fresh prefab on disk
  // matches the in-editor state next time it's opened.
  if (jsonState.empty() && !savedState.empty()) {
    asset->prefab->eventGraphJSON = savedState;
  }

  graph.graph.droppedLinkPopUpContent([this](ImFlow::Pin* pin) {
    drawCreatePopup(graph, pin);
  });
  graph.graph.rightClickPopUpContent([this](ImFlow::BaseNode* node) {
    if (node) {
      if (ImGui::Selectable(ICON_MDI_CONTENT_COPY " Duplicate")) {
        auto nodeP64 = (Project::Graph::Node::Base*)(node);
        ImVec2 newPos{
          node->getPos().x + node->getSize().x,
          node->getPos().y + 20.0f,
        };
        nlohmann::json jNode;
        nodeP64->serialize(jNode);
        auto newNode = graph.addNode(nodeP64->type, newPos);
        newNode->deserialize(jNode);
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::Selectable(ICON_MDI_TRASH_CAN_OUTLINE " Remove")) {
        node->destroy();
        ImGui::CloseCurrentPopup();
      }
    } else {
      drawCreatePopup(graph, nullptr);
    }
  });
}

std::string Editor::PrefabEventGraphEditor::getName() const
{
  // Tab title is just "EventGraph" — when this editor is docked into the
  // parent PrefabEditor's tabset, the prefab name is implicit from context.
  // The hidden ID suffix in winName (###...UUID) gives ImGui per-prefab
  // uniqueness without bloating what the user sees.
  return "EventGraph";
}

bool Editor::PrefabEventGraphEditor::isDirty() const
{
  // serialize() is non-const on Graph (touches ImNodeFlow internals) so we
  // pay the cost on the read path. Cheap enough for an event-graph window.
  return const_cast<Project::Graph::Graph&>(graph).serialize() != savedState;
}

bool Editor::PrefabEventGraphEditor::draw(ImGuiID defDockId)
{
  if (!ctx.project) return false;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::PREFAB) return false;

  std::string title = std::string{ICON_MDI_GRAPH " "} + getName()
    + (isDirty() ? " *" : "");
  // ID suffix bumped to invalidate stale imgui.ini entries from before
  // multi-viewport support landed.
  winName = title + "###PrefabEventGraphWin_" + std::to_string(assetUUID);

  // Dock as a sibling tab of Scene Editor; OS chrome on undock — see
  // PrefabEditor::draw for rationale.
  ImGuiWindowClass cls{};
  cls.ViewportFlagsOverrideSet   = ImGuiViewportFlags_NoAutoMerge;
  cls.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
  ImGui::SetNextWindowClass(&cls);

  // First-frame dock override wins over the loop-passed default. Lets the
  // PrefabEditor host its event graph as a sibling tab of its viewport.
  // DockBuilderDockWindow + SetNextWindowDockID(Always) together beat any
  // stale imgui.ini layout from a prior session that placed this window in
  // the outer Scene Editor strip — the loaded Window record otherwise wins
  // on the very first frame.
  if (firstDockTarget && !firstDockApplied) {
    ImGui::DockBuilderDockWindow(winName.c_str(), firstDockTarget);
    ImGui::SetNextWindowDockID(firstDockTarget, ImGuiCond_Always);
    firstDockApplied = true;
  } else if (defDockId) {
    ImGui::SetNextWindowDockID(defDockId, ImGuiCond_FirstUseEver);
  }

  if (!isInit) {
    isInit = true;
    auto *mvp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
      {
        mvp->Pos.x + (mvp->Size.x - DEF_WIN_SIZE.x) * 0.5f,
        mvp->Pos.y + (mvp->Size.y - DEF_WIN_SIZE.y) * 0.5f,
      },
      ImGuiCond_FirstUseEver
    );
  }
  if (forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  ImGui::Begin(winName.c_str(), &isOpen,
    ImGuiWindowFlags_NoCollapse
    | (isDirty() ? ImGuiWindowFlags_UnsavedDocument : 0));

  // Toolbar — Save lives here so the user doesn't have to bounce back to
  // the parent PrefabEditor to persist graph edits.
  if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) save();
  ImGui::SameLine();
  ImGui::TextDisabled("(stored in %s)", asset->path.c_str());

  // Ctrl+S → save while focused on this graph window.
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
  }

  // Push prefab context so prefab-aware nodes (PrefabEvent / PrefabFunc /
  // PrefabVarGet) can populate their dropdowns from this prefab's data.
  // Reset after to keep the standalone NodeEditor's view of the world clean.
  auto &pctx = Project::Graph::Node::activePrefabCtx();
  pctx.prefab      = asset->prefab.get();
  pctx.prefabName  = asset->name;
  pctx.projectPath = ctx.project->getPath();

  // Capture the canvas rect *before* update() so the drop target below covers
  // exactly what ImNodeFlow just rendered into.
  ImVec2 canvasMin  = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  graph.graph.setSize(canvasSize);

  // Reveal-from-Compile-Errors: if a node UUID is pending, pan the canvas so
  // its center matches the canvas center, and arm the highlight overlay. We
  // do this *before* update() so the panned scroll value is what ImNodeFlow
  // renders this frame — no one-frame flicker.
  Project::Graph::Node::Base* focusNode = nullptr;
  if(pendingFocusNodeUUID != 0) {
    for(const auto &kv : graph.graph.getNodes()) {
      auto *n = (Project::Graph::Node::Base*)kv.second.get();
      if(n && n->uuid == pendingFocusNodeUUID) { focusNode = n; break; }
    }
    if(focusNode) {
      ImVec2 nodePos  = focusNode->getPos();
      ImVec2 nodeSize = focusNode->getSize();
      // ImNodeFlow scroll is the grid-space offset of the canvas origin; to
      // center a node at grid-pos P with size S inside a canvas of size C,
      // the scroll should be (C/2 - (P + S/2)).
      ImVec2 target{
        canvasSize.x * 0.5f - (nodePos.x + nodeSize.x * 0.5f),
        canvasSize.y * 0.5f - (nodePos.y + nodeSize.y * 0.5f),
      };
      // ImNodeFlow's grid wrapper only exposes scroll() as a const-ref
      // getter (no public setter upstream). We strip the top-level const
      // to write into the same `m_scroll` member — well-defined because
      // the underlying member is non-const. Lets us recenter on a node
      // without patching the vendored submodule.
      const_cast<ImVec2&>(graph.graph.getGrid().scroll()) = target;
      highlightNodeUUID    = pendingFocusNodeUUID;
      highlightSecondsLeft = 2.0f;
    }
    pendingFocusNodeUUID = 0;
  }

  graph.graph.update();

  // Highlight overlay: draw a colored rect on top of the node for a couple
  // seconds after a focus request. Uses the foreground draw list so the
  // outline sits over ImNodeFlow's rendering.
  if(highlightSecondsLeft > 0.0f && highlightNodeUUID != 0) {
    Project::Graph::Node::Base* hn = nullptr;
    for(const auto &kv : graph.graph.getNodes()) {
      auto *n = (Project::Graph::Node::Base*)kv.second.get();
      if(n && n->uuid == highlightNodeUUID) { hn = n; break; }
    }
    if(hn) {
      ImVec2 g = hn->getPos();
      ImVec2 sz = hn->getSize();
      ImVec2 scroll = graph.graph.getGrid().scroll();
      ImVec2 mn{canvasMin.x + scroll.x + g.x - 4.0f,
                canvasMin.y + scroll.y + g.y - 4.0f};
      ImVec2 mx{mn.x + sz.x + 8.0f, mn.y + sz.y + 8.0f};
      float a = highlightSecondsLeft > 1.0f ? 1.0f : highlightSecondsLeft;
      ImU32 col = IM_COL32(255, 80, 80, (int)(255.0f * a));
      ImGui::GetForegroundDrawList()->AddRect(mn, mx, col, 4.0f, 0, 3.0f);
      highlightSecondsLeft -= ImGui::GetIO().DeltaTime;
    } else {
      highlightSecondsLeft = 0.0f;
    }
  }

  // Drag-drop drop target over the canvas — accepts payloads emitted by
  // PrefabEditor's My-Prefab variables / functions panels. Spawns a
  // pre-filled PrefabVarGet / PrefabFunc node at the drop location so the
  // user doesn't have to find it in the right-click create palette.
  {
    ImRect canvasRect{canvasMin, ImVec2{canvasMin.x + canvasSize.x,
                                        canvasMin.y + canvasSize.y}};
    ImGuiID dropID = ImGui::GetID("##prefabGraphDrop");
    if (ImGui::BeginDragDropTargetCustom(canvasRect, dropID)) {
      // PrefabVarGet drop.
      if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(
            Editor::DragDrop::TYPE_PREFAB_VAR)) {
        const auto *vp = static_cast<const Editor::DragDrop::PrefabVarPayload*>(p->Data);
        ImVec2 pos = graph.graph.screen2grid(ImGui::GetMousePos());
        auto node = graph.addNode(Project::Graph::TYPE_PREFAB_VAR_GET, pos);
        if (auto *vn = dynamic_cast<Project::Graph::Node::PrefabVarGet*>(node.get())) {
          vn->varUUID = vp->uuid;
          vn->varName = vp->name;
          vn->varKind = vp->kind;
          vn->updateTitle();
        }
      }
      // PrefabFunc drop.
      if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(
            Editor::DragDrop::TYPE_PREFAB_FUNC)) {
        const auto *fp = static_cast<const Editor::DragDrop::PrefabFuncPayload*>(p->Data);
        ImVec2 pos = graph.graph.screen2grid(ImGui::GetMousePos());
        auto node = graph.addNode(Project::Graph::TYPE_PREFAB_FUNC, pos);
        if (auto *fn = dynamic_cast<Project::Graph::Node::PrefabFunc*>(node.get())) {
          fn->funcName = fp->name;
          fn->updateTitle();
        }
      }
      ImGui::EndDragDropTarget();
    }
  }

  pctx.prefab = nullptr;
  pctx.prefabName.clear();
  pctx.projectPath.clear();

  ImGui::End();
  return isOpen;
}

void Editor::PrefabEventGraphEditor::save()
{
  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || !asset->prefab) return;

  auto state = graph.serialize();
  asset->prefab->eventGraphJSON = state;

  // Re-emit the .prefab file with the updated graph blob so the change
  // survives an editor restart even if the parent PrefabEditor isn't open.
  Utils::FS::saveTextFile(asset->path, asset->prefab->serialize());
  savedState = state;
  Utils::Logger::log("Saved Event Graph: " + asset->name);
}

void Editor::PrefabEventGraphEditor::discardUnsavedChanges()
{
  graph.deserialize(savedState);
}

void Editor::PrefabEventGraphEditor::focus() const
{
  ImGui::SetWindowFocus(("###PrefabEventGraphWin_" + std::to_string(assetUUID)).c_str());
}
