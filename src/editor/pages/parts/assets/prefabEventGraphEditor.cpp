#include "prefabEventGraphEditor.h"

#include "imgui.h"
#include "ImNodeFlow.h"
#include "json.hpp"
#include "IconsMaterialDesignIcons.h"

#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"
#include "../../../../project/graph/nodes/baseNode.h"

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

  // Load existing graph state if any. An empty eventGraphJSON means this is
  // the first time the user has opened the graph — start with a blank canvas.
  const std::string &jsonState = asset->prefab->eventGraphJSON;
  if (!jsonState.empty()) {
    graph.deserialize(jsonState);
  }
  savedState = graph.serialize();

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
  if (!ctx.project) return {};
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  return asset ? (asset->name + " — EventGraph") : std::string{"EventGraph"};
}

bool Editor::PrefabEventGraphEditor::isDirty() const
{
  // serialize() is non-const on Graph (touches ImNodeFlow internals) so we
  // pay the cost on the read path. Cheap enough for an event-graph window.
  return const_cast<Project::Graph::Graph&>(graph).serialize() != savedState;
}

bool Editor::PrefabEventGraphEditor::draw()
{
  if (!ctx.project) return false;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::PREFAB) return false;

  std::string title = std::string{ICON_MDI_GRAPH " "} + getName()
    + (isDirty() ? " *" : "");
  winName = title + "###PrefabEventGraph_" + std::to_string(assetUUID);

  if (!isInit) {
    isInit = true;
    auto screenSize = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
      {(screenSize.x - DEF_WIN_SIZE.x) / 2, (screenSize.y - DEF_WIN_SIZE.y) / 2},
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

  graph.graph.setSize(ImGui::GetContentRegionAvail());
  graph.graph.update();

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
  ImGui::SetWindowFocus(("###PrefabEventGraph_" + std::to_string(assetUUID)).c_str());
}
