// added by SPBF64 fork
#include "prefabEditor.h"

#include "imgui_internal.h"
#include "IconsMaterialDesignIcons.h"

#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"
#include "../../../imgui/helper.h"

namespace
{
  constexpr ImVec2 DEF_WIN_SIZE{720, 540};
  constexpr float MIN_PANE_WIDTH = 120.0f;
  constexpr float SPLITTER_WIDTH = 4.0f;
}

Editor::PrefabEditor::PrefabEditor(uint64_t uuid) : assetUUID(uuid)
{
  loadFromDisk();
}

void Editor::PrefabEditor::loadFromDisk()
{
  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::PREFAB || !asset->prefab) {
    Utils::Logger::log(
      "PrefabEditor: asset " + std::to_string(assetUUID) + " is not a prefab",
      Utils::Logger::LEVEL_ERROR
    );
    return;
  }
  filePath = asset->path;

  // Snapshot the prefab's editable subtree as JSON, then load into our scene
  // as the only child of the wrapper root.
  std::string objJson = asset->prefab->obj.serialize().dump();
  scene.loadFromObjectJSON(objJson);
  savedJSON = scene.serializeRootChild();
}

void Editor::PrefabEditor::saveToDisk()
{
  if (filePath.empty() || !ctx.project) return;

  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::PREFAB || !asset->prefab) return;

  // Pull the edited subtree out of the in-memory scene; rewrite the prefab's
  // canonical Object so any open instance inspectors see the new content.
  std::string subtreeJson = scene.serializeRootChild();
  auto subtreeDoc = nlohmann::json::parse(subtreeJson, nullptr, false);
  if (subtreeDoc.is_object()) {
    asset->prefab->obj = Project::Object{};
    asset->prefab->obj.deserialize(nullptr, subtreeDoc);
  }

  // Persist via the canonical prefab serializer (writes uuid + obj).
  Utils::FS::saveTextFile(filePath, asset->prefab->serialize());
  savedJSON = subtreeJson;
  history.markSaved();
  Utils::Logger::log("Saved prefab: " + filePath);
}

bool Editor::PrefabEditor::isDirty() const
{
  return scene.serializeRootChild() != savedJSON;
}

std::string Editor::PrefabEditor::getName() const
{
  if (!ctx.project) return {};
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  return asset ? asset->name : std::string{};
}

bool Editor::PrefabEditor::draw(ImGuiID defDockId)
{
  if (!ctx.project) return false;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::PREFAB) return false;

  std::string baseTitle = std::string{ICON_MDI_PACKAGE_VARIANT_CLOSED " "}
    + asset->name + (isDirty() ? " *" : "");
  winName = baseTitle + "###PrefabEditor_" + std::to_string(assetUUID);

  if (dockOnFirstAppearance) {
    ImGuiID targetDock = 0;
    if (ImGuiWindow* vpWin = ImGui::FindWindowByName("3D-Viewport")) {
      targetDock = vpWin->DockId;
    }
    if (targetDock == 0) targetDock = defDockId;
    ImGui::SetNextWindowDockID(targetDock, ImGuiCond_Always);
    dockOnFirstAppearance = false;
  } else {
    ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  }
  if (forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  if (!ImGui::Begin(winName.c_str(), &isOpen,
        isDirty() ? ImGuiWindowFlags_UnsavedDocument : 0))
  {
    ImGui::End();
    return isOpen;
  }

  // EditScope binds this editor's history+scene+selection so that any
  // markChanged() call inside the nested SceneGraph / ObjectInspector routes
  // here, not to the main scene's history.
  Editor::UndoRedo::EditScope scope(history, scene, selection);

  // Toolbar
  if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) saveToDisk();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", filePath.c_str());

  // Ctrl+S → save (only when this window has focus).
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
      saveToDisk();
    }
    // Undo/Redo route to this editor's history via the active EditScope.
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      history.undo();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
      history.redo();
    }
  }

  // Two-pane body: scene graph on the left, object inspector on the right,
  // with a draggable splitter between them.
  float avail = ImGui::GetContentRegionAvail().x;
  float leftW = ImClamp(avail * splitFrac, MIN_PANE_WIDTH, avail - MIN_PANE_WIDTH - SPLITTER_WIDTH);

  ImGui::BeginChild("##GraphPane", ImVec2(leftW, 0), ImGuiChildFlags_Borders);
    graph.draw(scene, selection);
  ImGui::EndChild();

  ImGui::SameLine();

  // Splitter: an invisible button that the user can drag horizontally to
  // resize the panes. Cursor and visual feedback come from the surrounding
  // style; ImGuiCol_Separator gets drawn as a thin vertical strip.
  ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Separator));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
  ImGui::Button("##Splitter", ImVec2(SPLITTER_WIDTH, -1));
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemActive()) {
    float delta = ImGui::GetIO().MouseDelta.x;
    splitFrac = ImClamp((leftW + delta) / avail, 0.1f, 0.9f);
  }
  if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }

  ImGui::SameLine();

  ImGui::BeginChild("##InspectorPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
    inspector.draw(scene, selection);
  ImGui::EndChild();

  ImGui::End();
  return isOpen;
}

void Editor::PrefabEditor::focus() const
{
  ImGui::SetWindowFocus(("###PrefabEditor_" + std::to_string(assetUUID)).c_str());
}
