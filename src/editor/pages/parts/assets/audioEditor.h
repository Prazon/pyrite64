/**
* Per-asset window for AUDIO and MUSIC_XM assets. Mirrors ImageEditor
* lifecycle: held in EditorScene's audioEditors map. Hosts AssetInspector
* on the right so sample-rate, compression and force-mono are editable
* per-asset without a global "Asset" tab in the scene editor.
*/
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

namespace Editor
{
  class AudioEditor
  {
    private:
      uint64_t assetUUID{};
      std::string winName{};

      float splitFrac{0.32f};
      bool  splitDragging{false};

      bool forceFocusNextFrame{true};
      bool firstDockFrame{true};

    public:
      explicit AudioEditor(uint64_t assetUUID) : assetUUID(assetUUID) {}

      bool draw(ImGuiID defDockId);
      void focus() const;
  };
}
