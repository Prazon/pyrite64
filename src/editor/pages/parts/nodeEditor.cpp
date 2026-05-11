/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "nodeEditor.h"

#include "imgui.h"
#include "assets/assetEditorDocking.h"
#include "IconsMaterialDesignIcons.h"
#include "../../../context.h"
#include "../../../utils/logger.h"
#include "../../imgui/helper.h"

#include <unordered_set>

#include "ImNodeFlow.h"
#include "json.hpp"
#include "../../../project/graph/nodes/baseNode.h"
#include "../../../project/graph/nodeStyles.h"
#include "../../../project/compile/compileErrors.h"
#include "../../../utils/fs.h"
#include "../../nodePalette.h"
#include "../../nodeClipboard.h"
#include "../../graphHotkeys.h"

namespace
{

}

Editor::NodeEditor::NodeEditor(uint64_t assetUUID)
{
  // Pin/node visuals (UE5-faithful palette) live in nodeStyles.cpp. This
  // call is idempotent so the prefab event graph editor and material
  // editor can each fire it on construction without ordering concerns.
  Project::Graph::initNodeStyles();

  currentAsset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  auto loadedState = currentAsset ? Utils::FS::loadTextFile(currentAsset->path) : "{}";
  graph.deserialize(loadedState);
  savedState = graph.serialize();
  //name = "Node-Editor - ";
  name = currentAsset ? currentAsset->name : "*New Graph*";

  // Spawn `typeIdx` at `gridPos` and, if `pin` is non-null, wire it to
  // the first compatible slot on the new node. Output-pin drags get
  // wired to the new node's matching IN; input-pin drags get wired to
  // the new node's matching OUT. Replaces the old "always ins[0]"
  // behaviour that silently dropped value-pin drops.
  auto spawnAndWire = [](Project::Graph::Graph &g, uint32_t typeIdx,
                         const ImVec2 &gridPos, ImFlow::Pin* pin)
  {
    auto node = g.addNode(typeIdx, gridPos);
    if (!node) return;
    node->setPos(gridPos);
    if (!pin) return;
    auto srcStyle = pin->getStyle().get();
    if (pin->getType() == ImFlow::PinType_Output) {
      if (auto *target = ::Editor::NodePalette::firstMatchingInputPin(node.get(), pin)) {
        target->createLink(pin);
      }
    } else {
      for (auto &p : node->getOuts()) {
        if (p && p->getStyle().get() == srcStyle) {
          pin->createLink(p.get());
          break;
        }
      }
    }
  };

  graph.graph.droppedLinkPopUpContent([&, spawnAndWire](ImFlow::Pin* pin)
  {
    uint32_t typeIdx = 0;
    if (::Editor::NodePalette::draw(
          Project::Graph::Graph::getPaletteEntries(), pin, &typeIdx)) {
      auto pos = pin ? pin->getParent()->getPos() : ImVec2{0,0};
      pos.x += 180.0f;
      spawnAndWire(graph, typeIdx, pos, pin);
      ImGui::CloseCurrentPopup();
    }
  });

  graph.graph.rightClickPopUpContent([&, spawnAndWire](ImFlow::BaseNode* node)
  {
    if(node) {
      if(ImGui::Selectable(ICON_MDI_CONTENT_COPY " Duplicate")) {
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
      if(ImGui::Selectable(ICON_MDI_TRASH_CAN_OUTLINE " Remove")) {
        node->destroy();
        ImGui::CloseCurrentPopup();
      }
    } else {
      uint32_t typeIdx = 0;
      if (::Editor::NodePalette::draw(
            Project::Graph::Graph::getPaletteEntries(), nullptr, &typeIdx)) {
        ImVec2 mp = ImGui::GetMousePos();
        ImVec2 gridPos = graph.graph.screen2grid(mp);
        spawnAndWire(graph, typeIdx, gridPos, nullptr);
        ImGui::CloseCurrentPopup();
      }
    }
  });

}

Editor::NodeEditor::~NodeEditor()
{
}

bool Editor::NodeEditor::draw(ImGuiID defDockId)
{
  if(!currentAsset)
  {
    return false;
  }

  if(!isInit)
  {
    isInit = true;
    auto *mvp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({800,600}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
      {
        mvp->Pos.x + (mvp->Size.x - 800.0f) * 0.5f,
        mvp->Pos.y + (mvp->Size.y - 600.0f) * 0.5f,
      },
      ImGuiCond_FirstUseEver
    );
  }

  // Dock as a sibling tab of Scene Editor on first open. Stable ###suffix
  // keeps saved position/dock state across asset renames and invalidates
  // legacy imgui.ini entries that had no ### at all.
  Editor::setupAssetEditorDocking(defDockId, firstDockFrame);

  uint64_t uuid = currentAsset ? currentAsset->getUUID() : 0;
  // Title mirrors PrefabEventGraphEditor: graph icon prefix + dirty
  // indicator. Unsaved-document flag below puts the standard ImGui dot in
  // the close-button slot.
  std::string title = std::string{ICON_MDI_GRAPH " "} + name + (dirty ? " *" : "");
  std::string winName = title + "###NodeEditorWin_" + std::to_string(uuid);

  if(forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  ImGui::Begin(winName.c_str(), &isOpen,
    ImGuiWindowFlags_NoCollapse
    | (dirty ? ImGuiWindowFlags_UnsavedDocument : 0));

  // Toolbar — Save lives here so the user doesn't have to rely on Ctrl+S
  // alone, matching the prefab event graph editor's UX.
  if(ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) save();
  ImGui::SameLine();

  // Compile button — Unreal-Blueprint-style "validate this graph now".
  // Clears just this asset's diagnostics so unrelated errors from a prior
  // full build (or other open graphs) survive, then re-runs Graph::validate
  // against the live in-editor graph. The CompileErrorsWindow auto-pops on
  // revision bump (see editorScene draw loop) so no further wiring needed.
  {
    size_t errsThisAsset = 0;
    for(const auto &e : ctx.compileErrors.all()) {
      if(e.assetUUID == uuid
         && e.severity == Project::Compile::Severity::ERROR) ++errsThisAsset;
    }

    if(ImGui::Button(ICON_MDI_PLAY " Compile")) {
      ctx.compileErrors.clearForAsset(uuid);
      graph.validate(&ctx.compileErrors, uuid);
    }
    ImGui::SameLine();
    if(errsThisAsset == 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0x6C, 0xC8, 0x6C, 0xFF));
      ImGui::TextUnformatted(ICON_MDI_CHECK_CIRCLE " 0 errors");
      ImGui::PopStyleColor();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xF0, 0x55, 0x55, 0xFF));
      ImGui::Text(ICON_MDI_ALERT_CIRCLE " %zu error%s",
        errsThisAsset, errsThisAsset == 1 ? "" : "s");
      ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", currentAsset->path.c_str());
    ImGui::Separator();
  }

  // Ctrl+S → save while focused on this graph window. Tab opens the
  // Add-Node palette at the current mouse position (UE-Blueprint
  // muscle memory). The mouse position is latched at open so the user
  // can move the cursor toward the palette while typing without the
  // spawn point sliding away.
  if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
    if(!ImGui::IsAnyItemActive()
       && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
      paletteSpawnPos = graph.graph.screen2grid(ImGui::GetMousePos());
      ImGui::OpenPopup("##nodePaletteTab");
    }
  }
  if(ImGui::BeginPopup("##nodePaletteTab")) {
    uint32_t typeIdx = 0;
    if(::Editor::NodePalette::draw(
         Project::Graph::Graph::getPaletteEntries(), nullptr, &typeIdx)) {
      auto node = graph.addNode(typeIdx, paletteSpawnPos);
      if(node) node->setPos(paletteSpawnPos);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImVec2 canvasMin  = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  graph.graph.setSize(canvasSize);

  // Standard graph hotkeys (frame, delete, duplicate, copy/cut/paste,
  // arrow nudge, alt-click pin). The clipboard is the shared
  // script-graph store so cut here can be pasted in the prefab event
  // graph editor and vice-versa.
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    Editor::GraphHotkeys::apply<Project::Graph::Graph,
                                Project::Graph::Node::Base>(
      graph, canvasSize, &Editor::NodeClipboard::scriptGraph());
  }

  // Reveal-from-Compile-Errors: pan canvas to center the requested node and
  // arm a brief highlight overlay. See the parallel block in
  // prefabEventGraphEditor.cpp for the full rationale.
  if(pendingFocusNodeUUID != 0) {
    Project::Graph::Node::Base* focusNode = nullptr;
    for(const auto &kv : graph.graph.getNodes()) {
      auto *n = (Project::Graph::Node::Base*)kv.second.get();
      if(n && n->uuid == pendingFocusNodeUUID) { focusNode = n; break; }
    }
    if(focusNode) {
      ImVec2 nodePos  = focusNode->getPos();
      ImVec2 nodeSize = focusNode->getSize();
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

  // Bad-node outline: persistent red rect on every node referenced by an
  // ERROR for this asset. Cleared implicitly when the user hits Compile
  // again (clearForAsset) or fixes the graph (next validate drops the
  // entry). Drawn on the foreground list so it sits over ImNodeFlow lines.
  {
    std::unordered_set<uint64_t> badNodes;
    for(const auto &e : ctx.compileErrors.all()) {
      if(e.assetUUID == uuid
         && e.severity == Project::Compile::Severity::ERROR
         && e.nodeUUID != 0) {
        badNodes.insert(e.nodeUUID);
      }
    }
    if(!badNodes.empty()) {
      ImVec2 scroll = graph.graph.getGrid().scroll();
      auto *fg = ImGui::GetForegroundDrawList();
      for(const auto &kv : graph.graph.getNodes()) {
        auto *n = (Project::Graph::Node::Base*)kv.second.get();
        if(!n || !badNodes.contains(n->uuid)) continue;
        ImVec2 g = n->getPos();
        ImVec2 sz = n->getSize();
        ImVec2 mn{canvasMin.x + scroll.x + g.x - 3.0f,
                  canvasMin.y + scroll.y + g.y - 3.0f};
        ImVec2 mx{mn.x + sz.x + 6.0f, mn.y + sz.y + 6.0f};
        fg->AddRect(mn, mx, IM_COL32(0xF0, 0x55, 0x55, 0xFF), 4.0f, 0, 2.0f);
      }
    }
  }

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

  ImGui::End();

  auto currentState = graph.serialize();
  auto isDirtyNow = currentState != savedState;

  if (isDirtyNow) {
    if (!dirty || currentState != trackedDirtyState) {
      ctx.project->getAssets().markNodeGraphDirty(currentAsset->getUUID(), currentState);
      trackedDirtyState = currentState;
    }
  } else if (dirty) {
    ctx.project->getAssets().clearNodeGraphDirty(currentAsset->getUUID());
    trackedDirtyState.clear();
  }

  dirty = isDirtyNow;

  return isOpen;
}

void Editor::NodeEditor::save()
{
  if (!currentAsset) {
    return;
  }

  auto currentState = graph.serialize();
  Utils::FS::saveTextFile(currentAsset->path, currentState);
  Utils::FS::saveTextFile(currentAsset->path + ".conf", currentAsset->conf.serialize());
  savedState = currentState;
  trackedDirtyState.clear();
  dirty = false;
  ctx.project->getAssets().markNodeGraphSaved(currentAsset->getUUID(), savedState);
}

void Editor::NodeEditor::discardUnsavedChanges()
{
  if (!currentAsset) {
    return;
  }
  graph.deserialize(savedState);
  trackedDirtyState.clear();
  dirty = false;
  ctx.project->getAssets().clearNodeGraphDirty(currentAsset->getUUID());
}

void Editor::NodeEditor::focus() const
{
  uint64_t uuid = currentAsset ? currentAsset->getUUID() : 0;
  ImGui::SetWindowFocus(("###NodeEditorWin_" + std::to_string(uuid)).c_str());
}
