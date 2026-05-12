/**
* @copyright 2026 - Prazon
* @license MIT
*
* Floating editor window for a .p64ptx particle-system asset. Pattern mirrors
* MaterialEditor: one instance per open asset keyed by UUID in
* EditorScene::particleSystemEditors. The asset payload lives on the
* AssetManagerEntry as a Project::Assets::ParticleSystemAsset.
*
* The preview pane runs a host-side CPU simulation that mirrors the engine's
* PTX::EmitterFromAsset sim loop, so what authors see in the editor matches
* what the runtime will draw on device. The preview surface is a simple 2D
* viewport — no host-side T3D needed — because particles in this fork are
* sprite billboards and read identically in screen space.
*/
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"

#include "../../../../project/assets/particleSystemAsset.h"

namespace Editor
{
  class ParticleSystemEditor
  {
    private:
      uint64_t assetUUID{};
      std::string winName{};

      Project::Assets::ParticleSystemAsset working{};
      std::string savedState{};
      bool isInit{false};
      bool forceFocusNextFrame{true};

      ImGuiID firstDockTarget{0};
      bool   firstDockApplied{false};
      bool   firstDockFrame{true};

      // Left pane (preview) / right pane (inspector) split.
      float previewSplitFrac{0.45f};
      bool  splitDragging{false};

      struct LiveParticle {
        float x, y;        // window-space, relative to preview center
        float vx, vy;
        float age;
        float lifetime;
        float startScale;
        uint32_t seed;
      };

      std::vector<LiveParticle> particles{};
      float spawnAccum{0.0f};
      float simTime{0.0f};
      bool  paused{false};
      bool  bursted{false};       // burst-once flag (for non-looping previews)

    public:
      explicit ParticleSystemEditor(uint64_t particleAssetUUID);

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

    private:
      void resetSim();
      void stepSim(float dt);
      void spawnOne();
      void drawInspector();
      void drawPreview(ImVec2 size);
  };
}
