/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "parts/assetInspector.h"
#include "parts/assetsBrowser.h"
#include "parts/layerInspector.h"
#include "parts/logWindow.h"
#include "parts/memoryDashboard.h"
#include "parts/nodeEditor.h"
#include "parts/objectInspector.h"
#include "parts/preferenceOverlay.h"
#include "parts/projectSettings.h"
#include "parts/sceneGraph.h"
#include "parts/sceneInspector.h"
#include "parts/viewport3D.h"

namespace Editor
{
  class ModelEditor;
  class ImageEditor;
  class CodeEditor;
  class PrefabEditor;
  class PrefabEventGraphEditor;

  class Scene
  {
    private:
      Viewport3D viewport3d{};

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

    public:
      Scene();
      ~Scene();

      void openModelEditor(uint64_t assetUUID);
      void openImageEditor(uint64_t assetUUID);
      void openCodeEditor(uint64_t assetUUID);
      // SPBF64 fork: open the dedicated prefab editor for the given .prefab asset.
      void openPrefabEditor(uint64_t assetUUID);
      // Open the event graph window for the given prefab. Idempotent — if a
      // window is already open, brings it to the front instead of creating a
      // new one.
      void openPrefabEventGraphEditor(uint64_t prefabAssetUUID);

      void draw();
      void save();
  };
}
