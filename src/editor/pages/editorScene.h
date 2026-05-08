/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "parts/assetInspector.h"
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

namespace Project::Compile { struct Error; }

namespace Editor
{
  class ModelEditor;
  class ImageEditor;
  class CodeEditor;
  class PrefabEditor;
  class PrefabEventGraphEditor;
  class PrefabFunctionCodeEditor;

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
      PreferenceOverlay prefOverlay{};
      ProjectSettings projectSettings{};
      AssetsBrowser assetsBrowser{};
      AssetInspector assetInspector{};
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
      // it with a null project leaves it permanently empty. The constructor
      // populates these vectors from editorScene.json; processPendingRestores()
      // drains them at the top of draw() once ctx.project is non-null.
      std::vector<uint64_t> pendingRestoreModels{};
      std::vector<uint64_t> pendingRestoreImages{};
      std::vector<uint64_t> pendingRestoreCode{};
      std::vector<uint64_t> pendingRestorePrefabs{};
      void processPendingRestores();

    public:
      Scene();
      ~Scene();

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
