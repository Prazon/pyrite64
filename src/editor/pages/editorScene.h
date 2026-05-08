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

      bool dockSpaceInit{false};
      ImGuiID dockLeftID;
      ImGuiID dockRightID;
      ImGuiID dockBottomID;

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

      void draw();
      void save();
  };
}
