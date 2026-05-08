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

  // Pre-select the prefab root so the inspector shows something on open and
  // pressing the camera-focus shortcut frames the prefab subtree.
  auto &root = scene.getRootObject();
  if (!root.children.empty()) {
    selection.set(root.children.front()->uuid);
  }
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
  // Don't gate the body on Begin's return value — matches ModelEditor's
  // pattern. When Begin returns false (window hidden after X click), ImGui
  // renders the body widgets to a hidden draw list, but our internal C++
  // (viewport.draw) still runs every frame the editor is in the map. That
  // keeps GPU resource lifetime predictable: the framebuffer is written to
  // every frame up to and including the close frame, matching the
  // defer-by-one destruction timing that ModelEditor / AssetPreviewViewport
  // rely on. Gating the body would skip GPU writes on the close frame, which
  // sounds safer but actually leaves SDL_GPU's in-flight texture
  // tracking inconsistent with our defer cadence.
  ImGui::Begin(winName.c_str(), &isOpen,
        isDirty() ? ImGuiWindowFlags_UnsavedDocument : 0);

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

  // Three-pane body, modeled after Unreal's Blueprint Editor:
  //   [hierarchy] | [3D viewport] | [details inspector]
  // Two draggable splitters between them. We compute pane widths from
  // fractions of the current avail width so resizing the window keeps the
  // ratio consistent.
  float avail = ImGui::GetContentRegionAvail().x;
  float leftW = ImClamp(avail * leftSplitFrac,
    MIN_PANE_WIDTH, avail - 2.0f * MIN_PANE_WIDTH - 2.0f * SPLITTER_WIDTH);
  float rightW = ImClamp(avail * rightSplitFrac,
    MIN_PANE_WIDTH, avail - leftW - MIN_PANE_WIDTH - 2.0f * SPLITTER_WIDTH);

  auto drawSplitter = [](const char* id, float &fracTarget, float currentW, float avail,
                         float minThisPane, float minOtherPanes) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Separator));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
    ImGui::Button(id, ImVec2(SPLITTER_WIDTH, -1));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActive()) {
      float delta = ImGui::GetIO().MouseDelta.x;
      float newW = currentW + delta;
      float minFrac = minThisPane / avail;
      float maxFrac = (avail - minOtherPanes) / avail;
      fracTarget = ImClamp(newW / avail, minFrac, maxFrac);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
  };

  // Left: scene graph.
  ImGui::BeginChild("##GraphPane", ImVec2(leftW, 0), ImGuiChildFlags_Borders);
    graph.draw(scene, selection);
  ImGui::EndChild();

  ImGui::SameLine();
  // Left splitter: dragging changes left pane width; center pane absorbs.
  // Other-panes minimum = center min + right (current) + right-splitter.
  drawSplitter("##SplitterL", leftSplitFrac, leftW, avail,
               MIN_PANE_WIDTH, MIN_PANE_WIDTH + rightW + 2.0f * SPLITTER_WIDTH);
  ImGui::SameLine();

  // Center: 3D viewport bound to this editor's scene + selection. Same
  // Viewport3D class as the main viewport — picking, gizmos, drag-drop, and
  // component selection highlights all work against the prefab.
  ImGui::BeginChild("##ViewportPane", ImVec2(avail - leftW - rightW - 2.0f * SPLITTER_WIDTH, 0),
    ImGuiChildFlags_Borders);
    viewport.draw();
  ImGui::EndChild();

  ImGui::SameLine();
  // Right splitter: dragging changes right pane width; center pane absorbs.
  // Other-panes minimum = left (current) + left-splitter + center min.
  drawSplitter("##SplitterR", rightSplitFrac, rightW, avail,
               MIN_PANE_WIDTH, leftW + 2.0f * SPLITTER_WIDTH + MIN_PANE_WIDTH);
  ImGui::SameLine();

  // Right: object inspector.
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
