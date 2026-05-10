/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "parts/assetsBrowser.h"
#include "parts/compileErrorsWindow.h"
#include "parts/layerInspector.h"
#include "parts/logWindow.h"
#include "parts/memoryDashboard.h"
#include "parts/nodeEditor.h"
#include "parts/objectInspector.h"
#include "parts/preferenceOverlay.h"
#include "parts/projectSettings.h"
#include "parts/sceneGraph.h"
#include "parts/sceneInspector.h"
#include "parts/viewport2D.h"
#include "parts/viewport3D.h"
#include "parts/assets/matThumbnailCache.h"
#include "parts/assets/modelThumbnailCache.h"

namespace Project::Compile { struct Error; }

namespace Editor
{
  class ModelEditor;
  class ImageEditor;
  class CodeEditor;
  class PrefabEditor;
  class PrefabEventGraphEditor;
  class PrefabFunctionCodeEditor;
  class MaterialEditor;
  class WidgetBlueprintEditor;
  class FontEditor;
  class AudioEditor;
  class ResourceTypeEditorWindow;
  class ResourceInstanceEditor;

  class Scene
  {
    private:
      Viewport3D viewport3d{};
      Viewport2D viewport2d{};

      // Editors
      std::vector<std::shared_ptr<NodeEditor>> nodeEditors{};
      std::map<uint64_t, std::shared_ptr<ModelEditor>> modelEditors{};
      std::map<uint64_t, std::shared_ptr<ImageEditor>> imageEditors{};
      std::map<uint64_t, std::shared_ptr<CodeEditor>> codeEditors{};
      // SPBF64 fork: per-asset prefab editors. Same lifecycle pattern as the
      // other asset editors above.
      std::map<uint64_t, std::shared_ptr<PrefabEditor>> prefabEditors{};
      // Per-prefab event graph editors. Keyed by the parent prefab's asset
      // UUID — only one event graph window per prefab can be open at a time.
      std::map<uint64_t, std::shared_ptr<PrefabEventGraphEditor>> prefabEventGraphEditors{};
      // Per-function code editors (slice editors that show only one
      // P64_NODE function from a prefab's user .cpp). Keyed by a synthetic
      // UUID derived from (prefabName, functionName) so re-opens dedupe.
      std::map<uint64_t, std::shared_ptr<PrefabFunctionCodeEditor>> prefabFunctionCodeEditors{};
      // Per-asset .p64mat material editors. Lifecycle parallel to the
      // model/image editors above — keyed on the asset's UUID for de-dupe.
      std::map<uint64_t, std::shared_ptr<MaterialEditor>> materialEditors{};
      // Per-asset .p64widget editors (WYSIWYG canvas tab for HUD/menu
      // authoring). Lifecycle mirrors the prefab/material editors above.
      std::map<uint64_t, std::shared_ptr<WidgetBlueprintEditor>> widgetEditors{};
      // Per-asset font / audio / resource-type / resource-instance editors.
      // These types previously had no dedicated window and relied on the
      // "Asset" tab in the scene editor; that tab has been removed and each
      // type now opens its own window with the AssetInspector strip on the
      // right.
      std::map<uint64_t, std::shared_ptr<FontEditor>> fontEditors{};
      std::map<uint64_t, std::shared_ptr<AudioEditor>> audioEditors{};
      std::map<uint64_t, std::shared_ptr<ResourceTypeEditorWindow>> resourceTypeEditors{};
      std::map<uint64_t, std::shared_ptr<ResourceInstanceEditor>> resourceInstanceEditors{};

      // Material thumbnail cache (browser-wide). Each entry owns its own
      // tiny offscreen viewport so the framebuffer texture is stable across
      // frames. Saving a material in MaterialEditor invalidates its entry.
      MaterialThumbnailCache matThumbnails{};

      // Model thumbnail cache. Stub: only loads pre-existing PNGs from
      // <project>/.cache/modelThumb/. The render-and-persist path is not
      // wired yet; see modelThumbnailCache.h for the migration plan.
      ModelThumbnailCache modelThumbnails{};

      // Defer-destroy list: PrefabEditor owns a Viewport3D whose framebuffer
      // GPU texture is referenced by ImGui's draw list for the current frame.
      // Erasing same-frame causes a use-after-free / hard crash when the draw
      // list is rendered. Hold the editor alive for one frame; drained at the
      // top of the next draw before any rendering. Mirrors pendingModelEditorErase.
      std::vector<std::shared_ptr<PrefabEditor>> pendingPrefabEditorErase{};

      // The unsaved-on-close popup target.
      uint64_t pendingPrefabEditorCloseUUID{0};
      bool pendingPrefabEditorClosePopup{false};

      // Deferred-destroy lists for editors that own GPU resources referenced
      // by ImGui draw data (e.g. ModelEditor's preview framebuffer texture).
      // Erasing same-frame as the close click frees the GPU texture before
      // ImGui's draw list — built earlier in the same frame — gets rendered,
      // causing a use-after-free / hard crash.
      std::vector<std::shared_ptr<ModelEditor>> pendingModelEditorErase{};
      // MaterialEditor owns a MaterialPreviewViewport whose framebuffer GPU
      // texture is still in this frame's draw list when the user closes the
      // window. Defer its destruction by one frame for the same reason as
      // pendingModelEditorErase / pendingPrefabEditorErase.
      std::vector<std::shared_ptr<MaterialEditor>> pendingMaterialEditorErase{};
      PreferenceOverlay prefOverlay{};
      ProjectSettings projectSettings{};
      AssetsBrowser assetsBrowser{};
      SceneInspector sceneInspector{};
      LayerInspector layerInspector{};
      ObjectInspector objectInspector{};
      LogWindow logWindow{};
      CompileErrorsWindow compileErrorsWindow{};
      MemoryDashboard memoryDashboard{};
      SceneGraph sceneGraph{};

      // Two-level dockspace, Unreal-style:
      //   outer (MAIN_DOCK)   -> dockTopID + dockBottomID (Files/Log/ROM, universal)
      //   "Scene Editor" tab  -> nested dockspace with sceneDockLeftID +
      //                          sceneDockRightID + sceneDockCenterID (3D-Viewport)
      // Asset editors dock into dockTopID as siblings of the Scene Editor tab,
      // so focusing one swaps the entire upper region instead of squeezing the
      // editor into the same panel as the 3D-Viewport.
      bool dockSpaceInit{false};
      ImGuiID dockTopID{0};
      ImGuiID dockBottomID{0};
      ImGuiID sceneDockLeftID{0};
      ImGuiID sceneDockRightID{0};
      ImGuiID sceneDockCenterID{0};

      uint64_t pendingNodeEditorCloseUUID{0};
      bool pendingNodeEditorClosePopup{false};

      // Restoration of persisted open editors must happen after a project is
      // loaded — PrefabEditor::loadFromDisk needs ctx.project, and instantiating
      // it with a null project leaves it permanently empty. onProjectOpened()
      // populates these vectors from <project>/.cache/editorState/editorState.json;
      // processPendingRestores() drains them at the top of draw() once
      // ctx.project is non-null.
      std::vector<uint64_t> pendingRestoreModels{};
      std::vector<uint64_t> pendingRestoreImages{};
      std::vector<uint64_t> pendingRestoreCode{};
      std::vector<uint64_t> pendingRestorePrefabs{};
      void processPendingRestores();

    public:
      Scene();
      ~Scene();

      // Project lifecycle hooks. onProjectOpened() loads persisted open-editor
      // UUIDs from the new project's cache; onProjectClosing() flushes the
      // current set to the *closing* project's cache and tears down all open
      // editors so they don't bleed into the next project.
      void onProjectOpened();
      void onProjectClosing();

      void openModelEditor(uint64_t assetUUID);
      void openImageEditor(uint64_t assetUUID);
      void openCodeEditor(uint64_t assetUUID);
      // Path-based code-editor open: needed for files outside the
      // AssetManager (per-prefab user .cpp lives in src/user/<name>.cpp and
      // uses `namespace User::` which buildCodeEntry doesn't dispatch on).
      // The synthetic UUID is sha256_64bit of the absolute path so re-opens
      // de-dupe through codeEditors. dockTarget, when nonzero, becomes the
      // editor's first-frame dock override.
      void openCodeEditorByPath(const std::string &absolutePath, ImGuiID dockTarget = 0);
      // SPBF64 fork: open the dedicated prefab editor for the given .prefab asset.
      void openPrefabEditor(uint64_t assetUUID);
      // Open the event graph window for the given prefab. Idempotent — if a
      // window is already open, brings it to the front instead of creating a
      // new one. dockTarget, when nonzero, becomes the editor's first-frame
      // dock override (used by PrefabEditor to land it next to its viewport).
      void openPrefabEventGraphEditor(uint64_t prefabAssetUUID, ImGuiID dockTarget = 0);
      // Open the material asset editor (.p64mat). Idempotent — re-opens
      // focus the existing window. dockTarget is honoured the same way as
      // the prefab event graph opener.
      void openMaterialEditor(uint64_t assetUUID, ImGuiID dockTarget = 0);
      // Open the widget blueprint editor (.p64widget). Idempotent: re-opens
      // focus the existing window. Same dock-target convention as the other
      // asset editors above.
      void openWidgetBlueprintEditor(uint64_t assetUUID, ImGuiID dockTarget = 0);

      // Open the font / audio / resource-type / resource-instance editor for
      // the given asset. Idempotent — re-opens focus the existing window.
      void openFontEditor(uint64_t assetUUID);
      void openAudioEditor(uint64_t assetUUID);
      void openResourceTypeEditor(uint64_t assetUUID);
      void openResourceInstanceEditor(uint64_t assetUUID);

      // Open the standalone NODE_GRAPH editor for the given asset. Idempotent
      // — re-opens focus the existing window rather than spawning a duplicate
      // tab. Same lifecycle pattern as the prefab/material editors above.
      void openNodeGraphEditor(uint64_t assetUUID);

      // Material thumbnail cache accessor — used by MaterialEditor::save()
      // to invalidate a saved material's thumbnail and by AssetsBrowser to
      // fetch / display them.
      MaterialThumbnailCache& getMatThumbnails() { return matThumbnails; }

      // Model thumbnail cache accessor (currently stub-only — see comment
      // on the member and in modelThumbnailCache.h).
      ModelThumbnailCache& getModelThumbnails() { return modelThumbnails; }

      // Asset browser accessor. main.cpp consults its hover state to gate
      // the global Ctrl+wheel UI zoom; the browser owns its own thumbScale
      // when the cursor is over it. editorScene.cpp uses this for the
      // per-project cache load/save path too.
      AssetsBrowser& getAssetsBrowser() { return assetsBrowser; }
      // Open a slice editor showing only the named P64_NODE function from
      // <project>/src/user/<prefabName>.cpp. Idempotent — re-opens focus
      // the existing window. dockTarget, when nonzero, becomes the
      // editor's first-frame dock override (used by PrefabEditor to land
      // it next to its viewport). Returns the synthetic UUID so the
      // PrefabEditor can track ownership for cleanup on close.
      uint64_t openPrefabFunctionCodeEditor(
        const std::string &prefabName,
        const std::string &functionName,
        ImGuiID dockTarget = 0
      );

      // Open the asset that owns the offending node, focus the graph window,
      // and pan its viewport so the offending node is centered (with a brief
      // highlight). Used by the Compile Errors panel on double-click.
      void revealCompileError(const ::Project::Compile::Error &e);

      void draw();
      void save();
  };
}
