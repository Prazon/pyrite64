/**
* WidgetBlueprintEditor implementation. See header for the architectural
* notes; this file follows the PrefabEditor pattern of (1) loading the
* asset's Object subtree into a private in-memory Scene, (2) driving
* SceneGraph + ObjectInspector + Viewport2D against it through an
* EditScope so undo/redo and selection route locally, and (3) rewriting
* the prefab's obj on save so the asset entry stays consistent.
*/
#include "widgetBlueprintEditor.h"

#include <string_view>

#include "imgui_internal.h"
#include "IconsMaterialDesignIcons.h"

#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"
#include "../../../../project/component/components.h"
#include "../../../../project/scene/prefab.h"
#include "../../../dragDropPayloads.h"

namespace
{
  constexpr ImVec2 DEF_WIN_SIZE{780, 560};

  // Palette entries for Phase 3's drag-source. Defined here so the editor
  // can render the palette panel even before the drag-from-palette plumbing
  // is wired (Phase 3 hooks the drop side).
  struct PaletteEntry
  {
    int          componentID;
    const char  *icon;
    const char  *label;
  };

  const PaletteEntry PALETTE[] = {
    { 14, ICON_MDI_IMAGE_OUTLINE,         "Image (Sprite2D)" },
    { 15, ICON_MDI_FORMAT_TEXT,           "Text (Label2D)" },
    { 16, ICON_MDI_PROGRESS_HELPER,       "Progress Bar" },
    { 19, ICON_MDI_RECTANGLE_OUTLINE,     "Panel" },
    { 20, ICON_MDI_BORDER_ALL_VARIANT,    "Nine-Patch" },
    { 21, ICON_MDI_VIEW_COLUMN,           "HBox Layout" },
    { 22, ICON_MDI_VIEW_AGENDA,           "VBox Layout" },
    { 23, ICON_MDI_GESTURE_TAP,           "Button" },
  };
}

Editor::WidgetBlueprintEditor::WidgetBlueprintEditor(uint64_t uuid)
  : assetUUID(uuid)
{
  graph.showComponentsInline = true;
  graph.prefabRootMode       = true;
  loadFromDisk();
}

void Editor::WidgetBlueprintEditor::loadFromDisk()
{
  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset
      || asset->type != Project::FileType::WIDGET_BLUEPRINT
      || !asset->prefab) {
    Utils::Logger::log(
      "WidgetBlueprintEditor: asset " + std::to_string(assetUUID) + " is not a widget",
      Utils::Logger::LEVEL_ERROR
    );
    return;
  }
  filePath = asset->path;

  // Snapshot the asset's editable subtree as JSON, then load into our scene
  // as the only child of the wrapper root. Force isCanvas2D=true on the
  // root so the viewport renders the subtree as a 2D canvas regardless of
  // what the on-disk file may have lost.
  std::string objJson = asset->prefab->obj.serialize().dump();
  scene.loadFromObjectJSON(objJson);
  if (!scene.getRootObject().children.empty()) {
    scene.getRootObject().children.front()->isCanvas2D = true;
  }
  savedJSON = scene.serializeRootChild();

  // Force a 2D-friendly framebuffer for the in-memory scene so canvas
  // mode renders against the standard N64 size even if the project's main
  // scene uses something else.
  scene.conf.fbWidth  = 320;
  scene.conf.fbHeight = 240;

  viewport.setCanvasMode(true);

  // Pre-select the root so the inspector shows something on open.
  auto &root = scene.getRootObject();
  if (!root.children.empty()) {
    selection.set(root.children.front()->uuid);
  }
}

void Editor::WidgetBlueprintEditor::saveToDisk()
{
  if (filePath.empty() || !ctx.project) return;

  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset
      || asset->type != Project::FileType::WIDGET_BLUEPRINT
      || !asset->prefab) return;

  std::string subtreeJson = scene.serializeRootChild();
  auto subtreeDoc = nlohmann::json::parse(subtreeJson, nullptr, false);
  if (subtreeDoc.is_object()) {
    asset->prefab->obj = Project::Object{};
    asset->prefab->obj.deserialize(nullptr, subtreeDoc);
    // Make sure the canvas flag survives the round-trip; required for the
    // build pipeline to tag every descendant with RENDER_LAYER_2D.
    asset->prefab->obj.isCanvas2D = true;
  }

  Utils::FS::saveTextFile(filePath, asset->prefab->serialize());
  savedJSON = subtreeJson;
  history.markSaved();
  Utils::Logger::log("Saved widget: " + filePath);

  // Refresh any widget-instance objects in the active scene so structural
  // edits propagate without a full project reload (instances are stored as
  // prefab-instances internally).
  if (auto *active = ctx.project->getScenes().getLoadedScene()) {
    active->refreshPrefabInstances(asset->prefab->uuid.value);
  }
}

bool Editor::WidgetBlueprintEditor::isDirty() const
{
  return scene.serializeRootChild() != savedJSON;
}

std::string Editor::WidgetBlueprintEditor::getName() const
{
  if (!ctx.project) return {};
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  return asset ? asset->name : std::string{};
}

void Editor::WidgetBlueprintEditor::focus() const
{
  if (!winName.empty()) {
    if (auto *w = ImGui::FindWindowByName(winName.c_str())) {
      ImGui::FocusWindow(w);
    }
  }
}

void Editor::WidgetBlueprintEditor::drawPalette()
{
  ImGui::TextDisabled("Palette");
  ImGui::Separator();
  for (const auto &p : PALETTE) {
    ImGui::PushID(p.componentID);
    ImGui::Selectable((std::string{p.icon} + "  " + p.label).c_str(), false,
      ImGuiSelectableFlags_AllowDoubleClick);

    if (ImGui::BeginDragDropSource()) {
      Editor::DragDrop::WidgetPalettePayload payload{ (uint32_t)p.componentID };
      ImGui::SetDragDropPayload(
        Editor::DragDrop::TYPE_WIDGET_PALETTE, &payload, sizeof(payload));
      ImGui::TextUnformatted(p.label);
      ImGui::EndDragDropSource();
    }
    ImGui::PopID();
  }
}

bool Editor::WidgetBlueprintEditor::draw(ImGuiID defDockId)
{
  if (!ctx.project) return false;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::WIDGET_BLUEPRINT) return false;

  // Display name strips trailing ".p64widget" so the tab reads cleanly.
  std::string displayName = asset->name;
  constexpr std::string_view kExt{".p64widget"};
  if (displayName.size() > kExt.size()
      && std::string_view{displayName}.substr(
           displayName.size() - kExt.size()) == kExt) {
    displayName.resize(displayName.size() - kExt.size());
  }
  std::string baseTitle = std::string{ICON_MDI_VIEW_DASHBOARD_OUTLINE " "}
    + displayName + (isDirty() ? " *" : "");
  winName = baseTitle + "###WidgetBPEditorWin_" + std::to_string(assetUUID);

  ImGuiWindowClass cls{};
  cls.ViewportFlagsOverrideSet   = ImGuiViewportFlags_NoAutoMerge;
  cls.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
  ImGui::SetNextWindowClass(&cls);

  if (defDockId) ImGui::SetNextWindowDockID(defDockId, ImGuiCond_FirstUseEver);

  auto *mvp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(
    {
      mvp->Pos.x + (mvp->Size.x - DEF_WIN_SIZE.x) * 0.5f,
      mvp->Pos.y + (mvp->Size.y - DEF_WIN_SIZE.y) * 0.5f,
    },
    ImGuiCond_FirstUseEver
  );
  if (forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  ImGui::Begin(winName.c_str(), &isOpen,
    ImGuiWindowFlags_NoCollapse
    | (isDirty() ? ImGuiWindowFlags_UnsavedDocument : 0));

  Editor::UndoRedo::EditScope scope(history, scene, selection);

  // Toolbar.
  if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) saveToDisk();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", filePath.c_str());

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveToDisk();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) history.undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) history.redo();
  }

  // Per-instance dock-space, keyed by asset UUID.
  const std::string uuidStr = std::to_string(assetUUID);
  const std::string dockId  = "WidgetBPDock_" + uuidStr;
  ImGuiID dockspaceID = ImGui::GetID(dockId.c_str());
  bool firstBuild = (ImGui::DockBuilderGetNode(dockspaceID) == nullptr);
  ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), 0);

  const std::string winPal = std::string{ICON_MDI_PUZZLE_OUTLINE      "  Palette##WidgetPal_"}      + uuidStr;
  const std::string winHie = std::string{ICON_MDI_FILE_TREE           "  Hierarchy##WidgetHie_"}    + uuidStr;
  const std::string winVP  = std::string{ICON_MDI_VIEW_QUILT          "  Canvas##WidgetVP_"}        + uuidStr;
  const std::string winDet = std::string{ICON_MDI_INFORMATION         "  Details##WidgetDet_"}      + uuidStr;

  if (firstBuild) {
    ImGui::DockBuilderRemoveNode(dockspaceID);
    ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetContentRegionAvail());

    ImGuiID center = dockspaceID;
    ImGuiID left  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.22f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.55f, nullptr, &left);

    ImGui::DockBuilderDockWindow(winPal.c_str(), left);
    ImGui::DockBuilderDockWindow(winHie.c_str(), leftBottom);
    ImGui::DockBuilderDockWindow(winVP.c_str(),  center);
    ImGui::DockBuilderDockWindow(winDet.c_str(), right);
    ImGui::DockBuilderFinish(dockspaceID);
  }

  ImGui::End(); // host

  ImGui::Begin(winPal.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    drawPalette();
  ImGui::End();

  ImGui::Begin(winHie.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    graph.draw(scene, selection);
  ImGui::End();

  ImGui::Begin(winVP.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    viewport.draw();
  ImGui::End();

  ImGui::Begin(winDet.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    inspector.draw(scene, selection);
  ImGui::End();

  return isOpen;
}
