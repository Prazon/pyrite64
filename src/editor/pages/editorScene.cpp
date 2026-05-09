/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "editorScene.h"

#include "IconsMaterialDesignIcons.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../actions.h"
#include "../undoRedo.h"
#include "../../context.h"
#include "../../project/cacheDir.h"
#include "../../project/project.h"

#define IMVIEWGUIZMO_IMPLEMENTATION 1
#include "ImGuizmo.h"
#include "ImViewGuizmo.h"
#include "../../utils/hash.h"
#include "../../utils/logger.h"
#include "../../utils/ringBuffer.h"
#include "../../utils/updater.h"
#include "../imgui/notification.h"
#include "../imgui/theme.h"
#include "parts/assets/modelEditor.h"
#include "parts/assets/imageEditor.h"
#include "parts/assets/codeEditor.h"
#include "parts/assets/prefabEditor.h"
#include "parts/assets/prefabEventGraphEditor.h"
#include "parts/assets/prefabFunctionCodeEditor.h"
#include "parts/assets/materialEditor.h"
#include "parts/assets/widgetBlueprintEditor.h"
#include "parts/assets/fontEditor.h"
#include "parts/assets/audioEditor.h"
#include "parts/assets/resourceTypeEditorWindow.h"
#include "parts/assets/resourceInstanceEditor.h"
#include "../../project/compile/compileErrors.h"

namespace
{
  constinit bool preferencesOpen{false};
  constinit bool projectSettingsOpen{false};
  constinit bool needsSanityCheck{false};
  constinit Utils::RingBuffer<double, 16> fpsRingBuffer{};
}

Editor::Scene::Scene()
{
  Editor::Actions::registerAction(Editor::Actions::Type::OPEN_NODE_GRAPH, [this](const std::string& asset)
  {
    printf("OPEN_NODE_GRAPH action called with asset: %s\n", asset.c_str());
    if(!ctx.project)return false;
    auto assetEntry = ctx.project->getAssets().getEntryByUUID(std::stoull(asset));
    if(assetEntry) {
      nodeEditors.push_back(std::make_unique<NodeEditor>(assetEntry->getUUID()));
      return true;
    }
    return false;
  });
  needsSanityCheck = true;

  // Open-editor restoration is per-project now (lives in
  // <project>/.cache/editorState/editorState.json). Loading is deferred
  // until onProjectOpened() runs, after PROJECT_OPEN has populated
  // ctx.project. Constructing a Scene without an active project leaves the
  // pendingRestore* lists empty, which is the correct behaviour at the
  // launcher screen.
}

Editor::Scene::~Scene()
{
  // Persistence has moved to onProjectClosing(); the destructor runs at
  // editor shutdown well after the last project has been closed and torn
  // down, so there's nothing to flush here.
  Editor::Actions::registerAction(Editor::Actions::Type::OPEN_NODE_GRAPH, nullptr);
}

void Editor::Scene::onProjectOpened()
{
  // Drain any per-project state from a previously open project before loading
  // the new one. The close hook should have already done this, but call it
  // again defensively in case PROJECT_OPEN is reached without a clean close.
  pendingRestoreModels.clear();
  pendingRestoreImages.clear();
  pendingRestoreCode.clear();
  pendingRestorePrefabs.clear();

  if (!ctx.project) return;

  auto path = Project::Cache::fileFor(*ctx.project, "editorState", "editorState.json");
  if (path.empty()) return;

  try {
    auto json = Utils::JSON::loadFile(path);
    auto stashList = [&](const char *key, std::vector<uint64_t> &out) {
      if (!json.contains(key)) return;
      for (const auto &assetUUID : json[key]) {
        out.push_back(assetUUID.get<uint64_t>());
      }
    };
    stashList("winModels",  pendingRestoreModels);
    stashList("winImages",  pendingRestoreImages);
    stashList("winCode",    pendingRestoreCode);
    stashList("winPrefabs", pendingRestorePrefabs);
    if (json.contains("assetsBrowserThumbScale")) {
      assetsBrowser.setThumbScale(json["assetsBrowserThumbScale"].get<float>());
    }
  } catch (const std::exception &) {
    // Missing or malformed cache file is non-fatal — the user just gets a
    // clean tab strip. JSON::loadFile already logs parse errors.
  }
}

void Editor::Scene::onProjectClosing()
{
  if (ctx.project) {
    nlohmann::json conf{};
    conf["winModels"] = nlohmann::json::array();
    for (const auto& [assetUUID, _] : modelEditors) {
      conf["winModels"].push_back(assetUUID);
    }
    conf["winImages"] = nlohmann::json::array();
    for (const auto& [assetUUID, _] : imageEditors) {
      conf["winImages"].push_back(assetUUID);
    }
    conf["winCode"] = nlohmann::json::array();
    for (const auto& [assetUUID, _] : codeEditors) {
      conf["winCode"].push_back(assetUUID);
    }
    conf["winPrefabs"] = nlohmann::json::array();
    for (const auto& [assetUUID, _] : prefabEditors) {
      conf["winPrefabs"].push_back(assetUUID);
    }
    conf["assetsBrowserThumbScale"] = assetsBrowser.getThumbScale();

    auto path = Project::Cache::fileFor(*ctx.project, "editorState", "editorState.json");
    if (!path.empty()) {
      Utils::FS::saveTextFile(path, conf.dump(2));
    }
  }

  // Tear down editors that hold references to the project being closed. Same
  // hazard as the per-frame defer-erase lists used during normal close: any
  // editor with a GPU framebuffer is held one frame in its pendingErase
  // bucket so ImGui's already-built draw list can finish referencing the
  // texture. Models / prefabs / materials all use that pattern.
  pendingRestoreModels.clear();
  pendingRestoreImages.clear();
  pendingRestoreCode.clear();
  pendingRestorePrefabs.clear();

  for (auto &[uuid, editor] : modelEditors) pendingModelEditorErase.push_back(std::move(editor));
  modelEditors.clear();
  imageEditors.clear();
  codeEditors.clear();
  for (auto &[uuid, editor] : prefabEditors) pendingPrefabEditorErase.push_back(std::move(editor));
  prefabEditors.clear();
  prefabEventGraphEditors.clear();
  prefabFunctionCodeEditors.clear();
  for (auto &[uuid, editor] : materialEditors) pendingMaterialEditorErase.push_back(std::move(editor));
  materialEditors.clear();
  widgetEditors.clear();
  fontEditors.clear();
  audioEditors.clear();
  resourceTypeEditors.clear();
  resourceInstanceEditors.clear();
  nodeEditors.clear();

  matThumbnails.clear();
  modelThumbnails.clear();
}

void Editor::Scene::openModelEditor(uint64_t assetUUID)
{
  auto it = modelEditors.find(assetUUID);
  if(it != modelEditors.end()) {
    it->second->focus();
  } else {
    modelEditors[assetUUID] = std::make_unique<ModelEditor>(assetUUID);
  }
}

void Editor::Scene::openImageEditor(uint64_t assetUUID)
{
  auto it = imageEditors.find(assetUUID);
  if(it != imageEditors.end()) {
    it->second->focus();
  } else {
    imageEditors[assetUUID] = std::make_shared<ImageEditor>(assetUUID);
  }
}

void Editor::Scene::openCodeEditor(uint64_t assetUUID)
{
  auto it = codeEditors.find(assetUUID);
  if(it != codeEditors.end()) {
    it->second->focus();
  } else {
    codeEditors[assetUUID] = std::make_shared<CodeEditor>(assetUUID);
  }
}

void Editor::Scene::openCodeEditorByPath(const std::string &absolutePath, ImGuiID dockTarget)
{
  // Hash the absolute path into a stable synthetic UUID so the codeEditors
  // map de-dupes when the same file is reopened from different call sites.
  uint64_t synthUUID = Utils::Hash::sha256_64bit(absolutePath);
  auto it = codeEditors.find(synthUUID);
  if (it != codeEditors.end()) {
    // Already open — refresh the dock target so a follow-up open from a
    // different host (e.g. another PrefabEditor) can still land it where
    // expected on its first draw cycle. focus() ensures the user sees it.
    if (dockTarget) it->second->setFirstDockTarget(dockTarget);
    it->second->focus();
    return;
  }
  auto editor = std::make_shared<CodeEditor>(synthUUID, absolutePath);
  if (dockTarget) editor->setFirstDockTarget(dockTarget);
  codeEditors[synthUUID] = std::move(editor);
}

void Editor::Scene::openPrefabEditor(uint64_t assetUUID)
{
  auto it = prefabEditors.find(assetUUID);
  if(it != prefabEditors.end()) {
    it->second->focus();
  } else {
    prefabEditors[assetUUID] = std::make_shared<PrefabEditor>(assetUUID);
  }
}

void Editor::Scene::openMaterialEditor(uint64_t assetUUID, ImGuiID dockTarget)
{
  auto it = materialEditors.find(assetUUID);
  if (it != materialEditors.end()) {
    if (dockTarget) it->second->setFirstDockTarget(dockTarget);
    it->second->focus();
    return;
  }
  auto editor = std::make_shared<MaterialEditor>(assetUUID);
  if (dockTarget) editor->setFirstDockTarget(dockTarget);
  materialEditors[assetUUID] = std::move(editor);
}

void Editor::Scene::openWidgetBlueprintEditor(uint64_t assetUUID, ImGuiID dockTarget)
{
  auto it = widgetEditors.find(assetUUID);
  if (it != widgetEditors.end()) {
    it->second->focus();
    return;
  }
  (void)dockTarget; // dockTarget honored on first draw via defDockId arg
  widgetEditors[assetUUID] = std::make_shared<WidgetBlueprintEditor>(assetUUID);
}

void Editor::Scene::openFontEditor(uint64_t assetUUID)
{
  auto it = fontEditors.find(assetUUID);
  if (it != fontEditors.end()) {
    it->second->focus();
  } else {
    fontEditors[assetUUID] = std::make_shared<FontEditor>(assetUUID);
  }
}

void Editor::Scene::openAudioEditor(uint64_t assetUUID)
{
  auto it = audioEditors.find(assetUUID);
  if (it != audioEditors.end()) {
    it->second->focus();
  } else {
    audioEditors[assetUUID] = std::make_shared<AudioEditor>(assetUUID);
  }
}

void Editor::Scene::openResourceTypeEditor(uint64_t assetUUID)
{
  auto it = resourceTypeEditors.find(assetUUID);
  if (it != resourceTypeEditors.end()) {
    it->second->focus();
  } else {
    resourceTypeEditors[assetUUID] = std::make_shared<ResourceTypeEditorWindow>(assetUUID);
  }
}

void Editor::Scene::openResourceInstanceEditor(uint64_t assetUUID)
{
  auto it = resourceInstanceEditors.find(assetUUID);
  if (it != resourceInstanceEditors.end()) {
    it->second->focus();
  } else {
    resourceInstanceEditors[assetUUID] = std::make_shared<ResourceInstanceEditor>(assetUUID);
  }
}

void Editor::Scene::openPrefabEventGraphEditor(uint64_t prefabAssetUUID, ImGuiID dockTarget)
{
  auto it = prefabEventGraphEditors.find(prefabAssetUUID);
  if(it != prefabEventGraphEditors.end()) {
    if (dockTarget) it->second->setFirstDockTarget(dockTarget);
    it->second->focus();
    return;
  }
  auto editor = std::make_shared<PrefabEventGraphEditor>(prefabAssetUUID);
  if (dockTarget) editor->setFirstDockTarget(dockTarget);
  prefabEventGraphEditors[prefabAssetUUID] = std::move(editor);
}

uint64_t Editor::Scene::openPrefabFunctionCodeEditor(
  const std::string &prefabName,
  const std::string &functionName,
  ImGuiID dockTarget)
{
  // Synthetic UUID keys de-dupe re-opens of the same (prefab, function)
  // pair across right-click / double-click / drag-from-graph paths.
  uint64_t synthUUID = Utils::Hash::sha256_64bit(
    prefabName + "::" + functionName
  );
  auto it = prefabFunctionCodeEditors.find(synthUUID);
  if (it != prefabFunctionCodeEditors.end()) {
    if (dockTarget) it->second->setFirstDockTarget(dockTarget);
    it->second->focus();
    return synthUUID;
  }
  auto editor = std::make_shared<PrefabFunctionCodeEditor>(
    synthUUID, prefabName, functionName
  );
  if (dockTarget) editor->setFirstDockTarget(dockTarget);
  prefabFunctionCodeEditors[synthUUID] = std::move(editor);
  return synthUUID;
}

void Editor::Scene::revealCompileError(const ::Project::Compile::Error &e)
{
  if(!ctx.project || e.assetUUID == 0) return;

  auto *asset = ctx.project->getAssets().getEntryByUUID(e.assetUUID);
  if(!asset) return;

  if(asset->type == Project::FileType::PREFAB) {
    openPrefabEventGraphEditor(e.assetUUID, 0);
    auto it = prefabEventGraphEditors.find(e.assetUUID);
    if(it != prefabEventGraphEditors.end() && e.nodeUUID != 0) {
      it->second->requestFocusNode(e.nodeUUID);
    }
    return;
  }

  if(asset->type == Project::FileType::NODE_GRAPH) {
    // Reuse the standard OPEN_NODE_GRAPH action to spawn a NodeEditor if one
    // doesn't yet exist for this asset (mirrors how the asset browser opens
    // these). Then walk nodeEditors to find the just-created instance and
    // forward the focus request.
    auto matches = [&](const std::shared_ptr<NodeEditor> &ed) {
      return ed && ed->getAssetUUID() == e.assetUUID;
    };
    auto it = std::find_if(nodeEditors.begin(), nodeEditors.end(), matches);
    if(it == nodeEditors.end()) {
      Editor::Actions::call(Editor::Actions::Type::OPEN_NODE_GRAPH,
        std::to_string(e.assetUUID));
      it = std::find_if(nodeEditors.begin(), nodeEditors.end(), matches);
    }
    if(it != nodeEditors.end()) {
      (*it)->focus();
      if(e.nodeUUID != 0) (*it)->requestFocusNode(e.nodeUUID);
    }
    return;
  }
}

void Editor::Scene::processPendingRestores()
{
  // No project yet — wait. Restoration is non-destructive; the lists stay put
  // until they can be replayed against a real asset table.
  if (!ctx.project) return;

  // Drop UUIDs the current project doesn't recognise (saved from a different
  // project, or assets deleted since last session) so we don't spawn zombie
  // editors. Each open* helper is idempotent and handles re-focus on its own.
  auto &assets = ctx.project->getAssets();
  auto resolve = [&](std::vector<uint64_t> &list, auto opener) {
    for (uint64_t uuid : list) {
      if (assets.getEntryByUUID(uuid)) opener(uuid);
    }
    list.clear();
  };
  resolve(pendingRestoreModels,  [&](uint64_t u){ openModelEditor(u);  });
  resolve(pendingRestoreImages,  [&](uint64_t u){ openImageEditor(u);  });
  resolve(pendingRestoreCode,    [&](uint64_t u){ openCodeEditor(u);   });
  resolve(pendingRestorePrefabs, [&](uint64_t u){ openPrefabEditor(u); });
}

void Editor::Scene::draw()
{
  // Replay any persisted-open editors now that a project is loaded. Cheap
  // no-op once the lists are drained; cheap no-op while ctx.project is null.
  processPendingRestores();

  float HEIGHT_TOP_BAR = 28_px;
  float HEIGHT_STATUS_BAR = 24_px;

  ImViewGuizmo::BeginFrame();
  ImGuizmo::BeginFrame();

  auto &io = ImGui::GetIO();
  auto viewport = ImGui::GetMainViewport();

  bool isRunning = ctx.isBuildOrRunning();

  // Multi-viewport: positions are in global virtual-desktop coords, so anchor
  // to the main viewport's Pos rather than (0,0). Without this the host
  // dockspace, top bar, and status bar end up offscreen relative to the main
  // SDL window when ViewportsEnable is on.
  ImGui::SetNextWindowPos({viewport->Pos.x, viewport->Pos.y + HEIGHT_TOP_BAR});
  ImGui::SetNextWindowSize({
    viewport->Size.x,
    viewport->Size.y - HEIGHT_TOP_BAR - HEIGHT_STATUS_BAR,
  });
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGuiWindowFlags host_window_flags = 0;
  host_window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
  host_window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
  ImGui::Begin("MAIN_DOCK", NULL, host_window_flags);
  ImGui::PopStyleVar(3);

  // Outer dockspace. Layout-id bumped to "DockSpaceV2" so the previous
  // single-region layout from imgui.ini doesn't override the new
  // top/bottom + nested-Scene-Editor structure on first run after upgrade.
  auto outerDockID = ImGui::GetID("DockSpaceV2");
  auto outerNode = ImGui::DockBuilderGetNode(outerDockID);
  outerDockID = ImGui::DockSpace(outerDockID, ImVec2(0.0f, 0.0f), 0, 0);
  ImGui::End();

  if(!outerNode)
  {
    ImGui::DockBuilderRemoveNode(outerDockID);
    ImGui::DockBuilderAddNode(outerDockID);
    ImGui::DockBuilderSetNodeSize(outerDockID, ImGui::GetMainViewport()->Size);

    // Bottom (universal) holds Files / Log / ROM. Visible regardless of which
    // tab is active in the upper region.
    dockBottomID = ImGui::DockBuilderSplitNode(outerDockID, ImGuiDir_Down, 0.25f, nullptr, &outerDockID);
    // Top is the tab strip area: Scene Editor + every open asset editor.
    dockTopID = outerDockID;

    ImGui::DockBuilderDockWindow("Scene Editor",   dockTopID);
    ImGui::DockBuilderDockWindow("Files",          dockBottomID);
    ImGui::DockBuilderDockWindow("Log",            dockBottomID);
    ImGui::DockBuilderDockWindow("Compile Errors", dockBottomID);
    ImGui::DockBuilderDockWindow("ROM",            dockBottomID);
    ImGui::DockBuilderFinish(outerDockID);
  }
  else
  {
    // Existing layout: re-resolve the named windows to their current dock
    // node IDs so we still know where to send asset editors on first open.
    if (auto *w = ImGui::FindWindowByName("Scene Editor")) dockTopID = w->DockId;
    if (auto *w = ImGui::FindWindowByName("Files"))        dockBottomID = w->DockId;
  }

  // Scene Editor wrapper hosts a nested dockspace with the actual scene
  // panels. NoCollapse so the user can't accidentally fold it shut from the
  // tab bar; padding zeroed so the inner dockspace fills the tab content.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
  ImGui::Begin("Scene Editor", nullptr, ImGuiWindowFlags_NoCollapse);
  ImGui::PopStyleVar();
  {
    // Layout id bumped to V4 when Scene + Layers were moved to the right
    // panel alongside Object so returning users with a cached imgui.ini
    // pick up the new default placement.
    auto sceneDockID = ImGui::GetID("SceneEditorDockV4");
    auto sceneNode = ImGui::DockBuilderGetNode(sceneDockID);
    sceneDockID = ImGui::DockSpace(sceneDockID, ImVec2(0.0f, 0.0f), 0, 0);

    if (!sceneNode) {
      ImGui::DockBuilderRemoveNode(sceneDockID);
      ImGui::DockBuilderAddNode(sceneDockID);
      ImGui::DockBuilderSetNodeSize(sceneDockID, ImGui::GetContentRegionAvail());

      sceneDockLeftID  = ImGui::DockBuilderSplitNode(sceneDockID, ImGuiDir_Left,  0.18f, nullptr, &sceneDockID);
      sceneDockRightID = ImGui::DockBuilderSplitNode(sceneDockID, ImGuiDir_Right, 0.28f, nullptr, &sceneDockID);
      sceneDockCenterID = sceneDockID;

      // Lock the central viewport node so the user can't drop other windows
      // onto it or split it — keeps the viewport pair exclusive, like
      // Unreal's Level Viewport. Other panels still dock into the
      // left/right groups. Tab bar stays enabled so 2D-Viewport can sit
      // alongside 3D-Viewport as a Godot-style switcher.
      if (auto *centerNode = ImGui::DockBuilderGetNode(sceneDockCenterID)) {
        centerNode->LocalFlags |=
          ImGuiDockNodeFlags_NoDockingOverMe |
          ImGuiDockNodeFlags_NoDockingSplit;
      }

      ImGui::DockBuilderDockWindow("3D-Viewport", sceneDockCenterID);
      ImGui::DockBuilderDockWindow("2D-Viewport", sceneDockCenterID);
      ImGui::DockBuilderDockWindow("Graph",       sceneDockLeftID);
      ImGui::DockBuilderDockWindow("Object",      sceneDockRightID);
      ImGui::DockBuilderDockWindow("Scene",       sceneDockRightID);
      ImGui::DockBuilderDockWindow("Layers",      sceneDockRightID);
      ImGui::DockBuilderDockWindow("Model",       sceneDockRightID);
      ImGui::DockBuilderFinish(sceneDockID);
    }
  }
  ImGui::End();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2_px, 2_px));
  ImGui::Begin("3D-Viewport");
    // Re-apply the central-viewport lock every frame so it survives returning
    // users whose imgui.ini already has the layout cached (LocalFlags isn't
    // fully restored from .ini). The tab bar must remain enabled here so
    // 2D-Viewport can dock as a sibling tab.
    if (auto *node = ImGui::GetWindowDockNode()) {
      node->LocalFlags |=
        ImGuiDockNodeFlags_NoDockingOverMe |
        ImGuiDockNodeFlags_NoDockingSplit;
      // Clear NoTabBar in case a stale imgui.ini from before the 2D
      // viewport was added still locks it off. Otherwise the user can
      // never see the 2D-Viewport tab.
      node->LocalFlags &= ~ImGuiDockNodeFlags_NoTabBar;
    }
    viewport3d.draw();
  ImGui::End();

  // Returning users already have an imgui.ini that predates this window, so
  // DockBuilderDockWindow inside the first-time-setup branch never runs for
  // them and sceneDockCenterID stays 0. Resolve the central node from the
  // 3D-Viewport's current DockId (we just drew it above), then dock 2D-
  // Viewport into the same node with FirstUseEver — ImGui's .ini takes over
  // afterwards so user rearrangements stick.
  ImGuiID centerNode = sceneDockCenterID;
  if (auto *w = ImGui::FindWindowByName("3D-Viewport")) {
    if (w->DockId) centerNode = w->DockId;
  }
  if (centerNode) ImGui::SetNextWindowDockID(centerNode, ImGuiCond_FirstUseEver);
  ImGui::Begin("2D-Viewport");
    viewport2d.draw();
  ImGui::End();
  ImGui::PopStyleVar(1);

  // Asset editors dock into the outer top region (siblings of the
  // Scene Editor tab). dockTopID is what we hand them as their default
  // dock target on first open.
  ImGuiID dockSpaceID = dockTopID;

  std::vector<uint32_t> delIndices{};
  for(uint32_t i = 0; i < nodeEditors.size(); ++i) {
    auto &nodeEditor = nodeEditors[i];
    if (!nodeEditor->draw(dockSpaceID)) {
      if (nodeEditor->isDirty()) {
        pendingNodeEditorCloseUUID = nodeEditor->getAssetUUID();
        pendingNodeEditorClosePopup = true;
      } else {
        delIndices.push_back(i);
      }
    }
  }
  // Remove closed editors
  for(int32_t i = (int32_t)delIndices.size() - 1; i >= 0; --i) {
    nodeEditors.erase(nodeEditors.begin() + delIndices[i]);
  }

  if (pendingNodeEditorClosePopup) {
    ImGui::OpenPopup("Unsaved Node Graph");
    pendingNodeEditorClosePopup = false;
  }

  if (ImGui::BeginPopupModal("Unsaved Node Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    auto itEditor = std::find_if(nodeEditors.begin(), nodeEditors.end(), [&](const std::shared_ptr<NodeEditor> &editor) {
      return editor && editor->getAssetUUID() == pendingNodeEditorCloseUUID;
    });

    if (itEditor == nodeEditors.end()) {
      pendingNodeEditorCloseUUID = 0;
      ImGui::CloseCurrentPopup();
    } else {
      auto &editor = *itEditor;
      ImGui::Text("The node graph '%s' has unsaved changes.", editor->getName().c_str());
      ImGui::Spacing();
      if (ImGui::Button("Save", {100_px, 0})) {
        editor->save();
        nodeEditors.erase(itEditor);
        pendingNodeEditorCloseUUID = 0;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard", {100_px, 0})) {
        editor->discardUnsavedChanges();
        nodeEditors.erase(itEditor);
        pendingNodeEditorCloseUUID = 0;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {100_px, 0})) {
        pendingNodeEditorCloseUUID = 0;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  // Drain any model / prefab editors that were closed last frame. Holding
  // them for one frame lets ImGui finish rendering the draw list that
  // referenced their preview framebuffer textures before SDL_ReleaseGPUTexture
  // runs. Same hazard for both: each owns a GPU framebuffer.
  pendingModelEditorErase.clear();
  pendingPrefabEditorErase.clear();
  pendingMaterialEditorErase.clear();

  std::vector<uint64_t> delUUIDs{};
  for(auto &[uuid, editor] : modelEditors) {
    if (!editor->draw(dockSpaceID)) {
      delUUIDs.push_back(uuid);
    }
  }
  for(auto &uuid : delUUIDs) {
    auto it = modelEditors.find(uuid);
    if (it != modelEditors.end()) {
      pendingModelEditorErase.push_back(std::move(it->second));
      modelEditors.erase(it);
    }
  }

  std::vector<uint64_t> delImageUUIDs{};
  for(auto &[uuid, editor] : imageEditors) {
    if (!editor->draw(dockSpaceID)) {
      delImageUUIDs.push_back(uuid);
    }
  }
  for(auto &uuid : delImageUUIDs)imageEditors.erase(uuid);

  std::vector<uint64_t> delCodeUUIDs{};
  for(auto &[uuid, editor] : codeEditors) {
    if (!editor->draw(dockSpaceID)) {
      delCodeUUIDs.push_back(uuid);
    }
  }
  for(auto &uuid : delCodeUUIDs)codeEditors.erase(uuid);

  // SPBF64 fork: prefab editors. On close attempts, prompt the user if there
  // are unsaved changes; same pattern as the unsaved-NodeGraph popup above.
  // Erase paths route through pendingPrefabEditorErase so the Viewport3D's
  // GPU framebuffer outlives the current frame's ImGui draw list.
  std::vector<uint64_t> delPrefabUUIDs{};
  for(auto &[uuid, editor] : prefabEditors) {
    if (!editor->draw(dockSpaceID)) {
      if (editor->isDirty()) {
        pendingPrefabEditorCloseUUID = uuid;
        pendingPrefabEditorClosePopup = true;
      } else {
        delPrefabUUIDs.push_back(uuid);
      }
    }
  }
  // Helper: when a prefab editor is dismissed, also close the auxiliary
  // tabs it spawned (its EventGraph window + every CodeEditor opened from
  // its function rows). Without this, undocked function-source tabs hang
  // around as ghost windows referring to a no-longer-open prefab.
  auto closePrefabAux = [&](PrefabEditor &editor) {
    prefabEventGraphEditors.erase(editor.getAssetUUID());
    for (uint64_t synth : editor.getOwnedCodeEditorUUIDs()) {
      codeEditors.erase(synth);
      // Same synth-UUID space is also used for per-function slice editors
      // when PrefabEditor opens them — the parent tracks both kinds in the
      // same vector. Erasing both maps is harmless when the UUID isn't
      // present in one of them.
      prefabFunctionCodeEditors.erase(synth);
    }
  };

  for(auto &uuid : delPrefabUUIDs) {
    auto it = prefabEditors.find(uuid);
    if (it != prefabEditors.end()) {
      closePrefabAux(*it->second);
      pendingPrefabEditorErase.push_back(std::move(it->second));
      prefabEditors.erase(it);
    }
  }

  if (pendingPrefabEditorClosePopup) {
    ImGui::OpenPopup("Unsaved Prefab");
    pendingPrefabEditorClosePopup = false;
  }

  if (ImGui::BeginPopupModal("Unsaved Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    auto it = prefabEditors.find(pendingPrefabEditorCloseUUID);
    if (it == prefabEditors.end()) {
      pendingPrefabEditorCloseUUID = 0;
      ImGui::CloseCurrentPopup();
    } else {
      auto &editor = it->second;
      ImGui::Text("The prefab '%s' has unsaved changes.", editor->getName().c_str());
      ImGui::Spacing();
      if (ImGui::Button("Save", {100_px, 0})) {
        editor->save();
        // Defer-erase: keep the editor (and its Viewport3D's GPU texture)
        // alive until next frame.
        closePrefabAux(*it->second);
        pendingPrefabEditorErase.push_back(std::move(it->second));
        prefabEditors.erase(it);
        pendingPrefabEditorCloseUUID = 0;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard", {100_px, 0})) {
        closePrefabAux(*it->second);
        pendingPrefabEditorErase.push_back(std::move(it->second));
        prefabEditors.erase(it);
        pendingPrefabEditorCloseUUID = 0;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {100_px, 0})) {
        pendingPrefabEditorCloseUUID = 0;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  // Prefab event-graph windows. Independent of the parent PrefabEditor —
  // closing the prefab tab doesn't auto-close its graph, and edits go
  // through their own Save button. Dirty graphs are dropped silently for
  // now (the user has explicit Save and the .prefab still has the prior
  // state); a confirm-modal is the natural follow-up.
  std::vector<uint64_t> delEventGraphUUIDs{};
  for(auto &[uuid, editor] : prefabEventGraphEditors) {
    if (!editor->draw(dockSpaceID)) {
      delEventGraphUUIDs.push_back(uuid);
    }
  }
  for(auto &uuid : delEventGraphUUIDs) prefabEventGraphEditors.erase(uuid);

  // Per-function code-editor windows. Same lifecycle as codeEditors above:
  // each instance returns false when its 'X' button is hit; we drop it.
  std::vector<uint64_t> delFnCodeUUIDs{};
  for(auto &[uuid, editor] : prefabFunctionCodeEditors) {
    if (!editor->draw(dockSpaceID)) {
      delFnCodeUUIDs.push_back(uuid);
    }
  }
  for(auto &uuid : delFnCodeUUIDs) prefabFunctionCodeEditors.erase(uuid);

  // Material asset editors. The editor owns a preview framebuffer (GPU
  // texture referenced by ImGui's draw list this frame), so erase paths
  // route through pendingMaterialEditorErase to defer destruction by one
  // frame — same hazard as ModelEditor / PrefabEditor above.
  std::vector<uint64_t> delMaterialUUIDs{};
  for(auto &[uuid, editor] : materialEditors) {
    if (!editor->draw(dockSpaceID)) {
      delMaterialUUIDs.push_back(uuid);
    }
  }
  for(auto &uuid : delMaterialUUIDs) {
    auto it = materialEditors.find(uuid);
    if (it != materialEditors.end()) {
      pendingMaterialEditorErase.push_back(std::move(it->second));
      materialEditors.erase(it);
    }
  }

  // Widget blueprint editors. Lifecycle parallel to material editors but
  // without a GPU framebuffer of their own (pure ImGui-drawn 2D canvas), so
  // immediate erase is safe; no pendingErase queue needed.
  std::vector<uint64_t> delWidgetUUIDs{};
  for(auto &[uuid, editor] : widgetEditors) {
    if (!editor->draw(dockSpaceID)) {
      delWidgetUUIDs.push_back(uuid);
    }
  }
  for(auto &uuid : delWidgetUUIDs) widgetEditors.erase(uuid);

  std::vector<uint64_t> delFontUUIDs{};
  for(auto &[uuid, editor] : fontEditors) {
    if (!editor->draw(dockSpaceID)) delFontUUIDs.push_back(uuid);
  }
  for(auto &uuid : delFontUUIDs) fontEditors.erase(uuid);

  std::vector<uint64_t> delAudioUUIDs{};
  for(auto &[uuid, editor] : audioEditors) {
    if (!editor->draw(dockSpaceID)) delAudioUUIDs.push_back(uuid);
  }
  for(auto &uuid : delAudioUUIDs) audioEditors.erase(uuid);

  std::vector<uint64_t> delResTypeUUIDs{};
  for(auto &[uuid, editor] : resourceTypeEditors) {
    if (!editor->draw(dockSpaceID)) delResTypeUUIDs.push_back(uuid);
  }
  for(auto &uuid : delResTypeUUIDs) resourceTypeEditors.erase(uuid);

  std::vector<uint64_t> delResInstUUIDs{};
  for(auto &[uuid, editor] : resourceInstanceEditors) {
    if (!editor->draw(dockSpaceID)) delResInstUUIDs.push_back(uuid);
  }
  for(auto &uuid : delResInstUUIDs) resourceInstanceEditors.erase(uuid);

  // SPBF64 fork: graph + inspector now take an explicit scene + selection.
  // Here we drive them with the project's active scene and the main selection.
  // PrefabEditor windows below set up their own EditScope and call these
  // widgets with their own (in-memory) scene + selection.
  auto* mainScene = ctx.project->getScenes().getLoadedScene();

  ImGui::Begin("Object");
    if (mainScene) objectInspector.draw(*mainScene, ctx.mainSelection);
    else ImGui::Text("No Scene loaded");
  ImGui::End();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2_px, 2_px));
  ImGui::Begin("Files");
  ImGui::PopStyleVar();
    assetsBrowser.draw();
  ImGui::End();

  if (mainScene) {

    ImGui::Begin("Graph");
      sceneGraph.draw(*mainScene, ctx.mainSelection);
    ImGui::End();

    ImGui::Begin("Scene");
      sceneInspector.draw();
    ImGui::End();

    ImGui::Begin("Layers");
      layerInspector.draw();
    ImGui::End();
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2_px, 2_px));
  ImGui::Begin("Log");
  ImGui::PopStyleVar();;
    logWindow.draw();
  ImGui::End();

  // Auto-focus the Compile Errors tab on each new build cycle (revision bump
  // means clear() or push() ran). Cheap one-shot — doesn't fight the user
  // when they tab away to Log between builds.
  {
    uint32_t curRev = ctx.compileErrors.getRevision();
    if(curRev != compileErrorsWindow.lastSeenRevision
       && ctx.compileErrors.errorCount() + ctx.compileErrors.warningCount() > 0) {
      ImGui::SetNextWindowFocus();
    }
    compileErrorsWindow.lastSeenRevision = curRev;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2_px, 2_px));
  ImGui::Begin("Compile Errors");
  ImGui::PopStyleVar();
    compileErrorsWindow.draw();
  ImGui::End();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4_px, 4_px));
  ImGui::Begin("ROM");
  ImGui::PopStyleVar();
    memoryDashboard.draw();
  ImGui::End();

  if (preferencesOpen) {
    ImVec2 windowSize{500_px, 300_px};
    auto screenSize = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowPos({(screenSize.x - windowSize.x) / 2, (screenSize.y - windowSize.y) / 2}, ImGuiCond_Appearing, {0.0f, 0.0f});
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Appearing);

    // Thick borders
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0_px);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::Begin(ICON_MDI_COG " Preferences", &preferencesOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
    if (prefOverlay.draw()) {
      preferencesOpen = false;
    }
    ImGui::End();

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(1);
  }

  if (projectSettingsOpen) {
    ImVec2 windowSize{600_px,400_px};
    auto screenSize = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowPos({(screenSize.x - windowSize.x) / 2, (screenSize.y - windowSize.y) / 2}, ImGuiCond_Appearing, {0.0f, 0.0f});
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Appearing);

    // Thick borders
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0_px);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::Begin(ICON_MDI_FILE_COG_OUTLINE " Project Settings", &projectSettingsOpen,
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    if (projectSettings.draw()) {
      projectSettingsOpen = false;
    }
    ImGui::End();

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(1);
  }

  // Top bar — anchor to the main viewport (global desktop coords with
  // ViewportsEnable) and pin to it so it doesn't spawn its own OS window.
  ImGui::SetNextWindowPos({viewport->Pos.x, viewport->Pos.y}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({viewport->Size.x, 4}, ImGuiCond_Always);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8_px,6_px});
  if(ImGui::Begin("TOP_BAR", 0,
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoTitleBar
    | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking
  )) {
    if(ImGui::BeginMenuBar())
    {
      if(ImGui::BeginMenu("Project"))
      {
        if(ImGui::MenuItem(ICON_MDI_CONTENT_SAVE_OUTLINE " Save")) {
          ctx.project->save();
          save();
        }
        if(ImGui::MenuItem(ICON_MDI_FILE_COG_OUTLINE " Settings"))projectSettingsOpen = true;
        if(ImGui::MenuItem(ICON_MDI_CLOSE " Close"))Actions::call(Actions::Type::PROJECT_CLOSE);
        ImGui::EndMenu();
      }

      // Edit Menu with undo/redo functionality including description
      if(ImGui::BeginMenu("Edit"))
      {
        auto& history = UndoRedo::getHistory();

        std::string undoText = ICON_MDI_UNDO " Undo";
        if (history.canUndo()) {
          undoText += " (" + history.getUndoDescription() + ")";
        }
        if(ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, history.canUndo())) {
          history.undo();
        }

        std::string redoText = ICON_MDI_REDO " Redo";
        if (history.canRedo()) {
          redoText += " (" + history.getRedoDescription() + ")";
        }
        if(ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false, history.canRedo())) {
          history.redo();
        }

        if(ImGui::MenuItem(ICON_MDI_COG " Preferences", "Ctrl+."))preferencesOpen = true;

        ImGui::EndMenu();
      }

      if(ImGui::BeginMenu("Build"))
      {
        if(ImGui::MenuItem(ICON_MDI_HAMMER " Build"))Actions::call(Actions::Type::PROJECT_BUILD);
        if(ImGui::MenuItem(ICON_MDI_PLAY " Build & Run"))Actions::call(Actions::Type::PROJECT_BUILD, "run");
        if(ImGui::MenuItem("Clean"))Actions::call(Actions::Type::PROJECT_CLEAN);
        ImGui::EndMenu();
      }

      if(ImGui::BeginMenu("View"))
      {
        if(ImGui::MenuItem(ICON_MDI_MAGNIFY_PLUS " Zoom In")) {
          ImGui::Theme::changeZoom(+1);
        }
        if(ImGui::MenuItem(ICON_MDI_MAGNIFY_MINUS "Zoom Out")) {
          ImGui::Theme::changeZoom(-1);
        }
        if(ImGui::MenuItem("Reset Layout")) {
          // Nuke both dockspaces; they get rebuilt next frame from defaults.
          ImGui::DockBuilderRemoveNode(ImGui::GetID("DockSpaceV2"));
          ImGui::DockBuilderRemoveNode(ImGui::GetID("SceneEditorDockV2"));
          ImGui::DockBuilderRemoveNode(ImGui::GetID("SceneEditorDockV3"));
          ImGui::DockBuilderRemoveNode(ImGui::GetID("SceneEditorDockV4"));
        }
        ImGui::EndMenu();
      }

      // Lists currently-open asset editor windows so the user can refocus one
      // that's been hidden, minimized, or dragged onto a different OS viewport
      // (multi-viewport mode lets these escape the main window).
      if(ImGui::BeginMenu("Window"))
      {
        auto assetLabel = [](uint64_t uuid) -> std::string {
          if (!ctx.project) return std::to_string(uuid);
          auto *e = ctx.project->getAssets().getEntryByUUID(uuid);
          return e ? e->name : std::to_string(uuid);
        };
        bool any = false;
        auto section = [&](const char *icon, const char *header, auto &map) {
          if (map.empty()) return;
          if (any) ImGui::Separator();
          ImGui::TextDisabled("%s", header);
          for (const auto &[uuid, editor] : map) {
            std::string label = std::string(icon) + " " + assetLabel(uuid);
            if (ImGui::MenuItem(label.c_str())) editor->focus();
          }
          any = true;
        };
        section(ICON_MDI_PACKAGE_VARIANT_CLOSED, "Prefabs",            prefabEditors);
        section(ICON_MDI_GRAPH,                  "Prefab Event Graphs", prefabEventGraphEditors);
        section(ICON_MDI_CUBE_OUTLINE,           "Models",             modelEditors);
        section(ICON_MDI_IMAGE_OUTLINE,          "Images",             imageEditors);
        section(ICON_MDI_CODE_BRACES,            "Code",               codeEditors);
        if (!nodeEditors.empty()) {
          if (any) ImGui::Separator();
          ImGui::TextDisabled("Node Graphs");
          for (const auto &editor : nodeEditors) {
            std::string label = std::string(ICON_MDI_GRAPH_OUTLINE) + " " + editor->getName();
            if (ImGui::MenuItem(label.c_str())) editor->focus();
          }
          any = true;
        }
        if (!any) {
          ImGui::MenuItem("(no editors open)", nullptr, false, false);
        }
        ImGui::EndMenu();
      }

      ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 40_px);

      const char* tooltip{};
      ImGui::PushFont(nullptr, 20.0_px);
      if(isRunning){
        ImGui::BeginDisabled();
        ImGui::MenuItem(ICON_MDI_STOP);
        ImGui::EndDisabled();
      } else {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.6f, 0.85f, 0.6f, 1.0f});
        if(ImGui::MenuItem(ICON_MDI_PLAY))Actions::call(Actions::Type::PROJECT_BUILD, "run");
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))tooltip = "Run (F12)";
        ImGui::PopStyleColor();
      }

      ImGui::PopFont();

      if(tooltip)ImGui::SetTooltip("%s", tooltip);

      ImGui::EndMenuBar();
    }
    ImGui::End();
  }
  ImGui::PopStyleVar();

  // Bottom Status bar — anchor to main viewport (see TOP_BAR comment).
  ImGui::SetNextWindowPos(
    {viewport->Pos.x, viewport->Pos.y + viewport->Size.y - HEIGHT_STATUS_BAR},
    ImGuiCond_Always, {0.0f, 0.0f}
  );
  ImGui::SetNextWindowSize({viewport->Size.x, HEIGHT_STATUS_BAR}, ImGuiCond_Always);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::Begin("STATUS_BAR", 0,
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar
    | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
  );

  fpsRingBuffer.push((double)ctx.timeCpuSelf / 1000.0 / 1000.0);

  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5_px);
  ImGui::PushFont(ImGui::Theme::getFontMono(), 16_px);
  ImVec4 perfColor{1.0f,1.0f,1.0f,0.4f};
  if (io.Framerate < 45) perfColor = {1.0f, 0.5f, 0.5f, 1.0f};
  ImGui::TextColored(perfColor, "%d FPS | History: %d/%d %s | CPU: %.2fms",
    (int)roundf(io.Framerate),
    UndoRedo::getHistory().getUndoCount(),
    UndoRedo::getHistory().getRedoCount(),
    Utils::byteSize(UndoRedo::getHistory().getMemoryUsage()).c_str(),
    fpsRingBuffer.average()
  );

  ImGui::SameLine();
  auto posX = io.DisplaySize.x - 12_px;

  if(!ctx.newerVersion.empty()) {
    ImGui::PopFont();

    auto txt = ICON_MDI_DOWNLOAD " Update Available: " + ctx.newerVersion;
    posX -= ImGui::CalcTextSize(txt.c_str()).x + 4;
    auto posY = ImGui::GetCursorPosY();;
    ImGui::SetCursorPos({posX, posY - 2});

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {5_px, 2_px});
    ImGui::PushStyleColor(ImGuiCol_Button, {0.5f, 0.8f, 0.0f, 0.9f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.5f, 0.8f, 0.0f, 0.75f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {1.0f, 0.5f, 0.0f, 0.6f});
    ImGui::PushStyleColor(ImGuiCol_Text, {0.0f, 0.0f, 0.0f, 1.0f});

    if(ImGui::Button(txt.c_str(), {0,0})) {
      Utils::Updater::doUpdate(ctx.newerVersion);
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(1);

    ImGui::SetCursorPosY(posY);
    ImGui::PushFont(ImGui::Theme::getFontMono());
    posX -= 8_px;
  }

  perfColor = {1.0f,1.0f,1.0f,0.4f};
  std::string txtInfo = "v" PYRITE_VERSION;
  #ifndef NDEBUG
    perfColor = {1.0f,1.0f,1.0f,0.6f};
    txtInfo += " [DEBUG]";
  #endif

  ImGui::SetCursorPosX(posX - ImGui::CalcTextSize(txtInfo.c_str()).x);
  ImGui::TextColored(perfColor, "%s", txtInfo.c_str());

  ImGui::PopFont();
  ImGui::End();

  // Global keyboard shortcuts
  if (!ImGui::GetIO().WantTextInput) {
    bool isCtrl = ImGui::GetIO().KeyCtrl;
    bool isShift = ImGui::GetIO().KeyShift;
    
    // Undo: Ctrl+Z
    if (isCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
      UndoRedo::getHistory().undo();
    }
    
    // Redo: Ctrl+Y
    if (isCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
      UndoRedo::getHistory().redo();
    }

    // Align focused object to the editor camera: Ctrl+Shift+F
    if (isCtrl && isShift && ImGui::IsKeyPressed(ImGuiKey_F)) {
      viewport3d.alignFocusedObjectToCamera();
    }

    // Preferences
    if (isCtrl && ImGui::IsKeyPressed(ImGuiKey_Period))preferencesOpen = true;
  }

  if(needsSanityCheck)
  {
    // check for duplicated asset UUIDs
    auto &assets = ctx.project->getAssets().getEntries();
    std::unordered_map<uint64_t, const Project::AssetManagerEntry*> uuids{};
    for (const auto &assetTypes : assets)
    {
      for (const auto &asset : assetTypes)
      {
        auto existing = uuids.find(asset.getUUID());
        if (existing != uuids.end()) {
          auto msg = "Duplicate UUID found: " + std::to_string(asset.getUUID()) + "\nAsset: " + asset.name
             + "\nWith: " + existing->second->name;
          if(ctx.window) {
            Editor::Noti::add(Noti::ERROR, msg);
          } else {
            Utils::Logger::log(msg, Utils::Logger::LEVEL_ERROR);
          }
        } else {
          uuids[asset.getUUID()] = &asset;
        }
      }
    }
    needsSanityCheck = false;
  }
}

void Editor::Scene::save()
{
  for(auto &nodeEditor : nodeEditors) {
    nodeEditor->save();
  }
  UndoRedo::getHistory().markSaved();
}
