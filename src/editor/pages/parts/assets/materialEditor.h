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
#include "matPreviewViewport.h"

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
      bool   firstDockFrame{true};

      // Unreal-style left preview / right graph layout. previewSplitFrac is
      // the fraction of the editor's content width given to the left pane
      // (3D preview on top, compiled-state summary below).
      float previewSplitFrac{0.35f};
      bool  splitDragging{false};

      // Asset inspector right strip.
      float assetSplitFrac{0.22f};
      bool  assetSplitDragging{false};

      // Live 3D preview applied to a polygonal host cube. setMaterial() each
      // frame stamps compiledCache into the host's per-part material slots so
      // graph edits show up live.
      MaterialPreviewViewport preview{};

      // Compiled cache so the preview doesn't re-walk the graph every
      // frame — recomputed on edit and after deserialize.
      Project::Assets::Material compiledCache{};
      void recompileCache();

      // Latched mouse-grid position when Tab opens the Add-Node palette.
      ImVec2 paletteSpawnPos{0,0};

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
