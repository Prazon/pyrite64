/**
* @copyright 2026 - Prazon
* @license MIT
*
* Floating window that hosts a .p64mat material asset's node graph plus a
* live preview pane. Lifecycle mirrors the other asset editors — one
* instance per open material asset, keyed by UUID in
* EditorScene::materialEditors.
*
* Storage lives in a Project::Assets::MaterialAsset on the AssetManagerEntry.
* The editor materialises a live MaterialGraph::Graph from the asset's
* serialized graph JSON on open and writes back on save.
*/
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

#include "../../../../project/materialGraph/graph.h"

namespace Editor
{
  class MaterialEditor
  {
    private:
      uint64_t assetUUID{};
      std::string winName{};
      Project::MaterialGraph::Graph graph{};
      std::string savedState{};
      bool isInit{false};
      bool forceFocusNextFrame{true};

      ImGuiID firstDockTarget{0};
      bool   firstDockApplied{false};

      // Bottom-pinned preview pane shows the compiled Material's prim/env
      // colour swatches and a flag-state dump. A full 3D preview comes
      // later — wiring AssetPreviewViewport here would need a host mesh
      // and a way to swap the material per-frame, which is more plumbing
      // than v1 needs to ship a working asset workflow.
      float previewSplitFrac{0.40f};
      bool  splitDragging{false};

      // Compiled cache so the preview doesn't re-walk the graph every
      // frame — recomputed on edit and after deserialize.
      Project::Assets::Material compiledCache{};
      void recompileCache();

    public:
      explicit MaterialEditor(uint64_t materialAssetUUID);

      bool draw(ImGuiID defDockId = 0);
      void focus() const;
      void save();
      void discardUnsavedChanges();

      void setFirstDockTarget(ImGuiID dockId) {
        firstDockTarget = dockId;
        firstDockApplied = false;
      }

      [[nodiscard]] uint64_t getAssetUUID() const { return assetUUID; }
      [[nodiscard]] bool isDirty() const;
      [[nodiscard]] std::string getName() const;
  };
}
