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

  class Scene
  {
    private:
      Viewport3D viewport3d{};

      // Editors
      std::vector<std::shared_ptr<NodeEditor>> nodeEditors{};
      std::map<uint64_t, std::shared_ptr<ModelEditor>> modelEditors{};
      std::map<uint64_t, std::shared_ptr<ImageEditor>> imageEditors{};

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

      void draw();
      void save();
  };
}
