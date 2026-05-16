/**
* @copyright 2026 - Nolan Baker
* @license MIT
*/
#pragma once

#include "settingsShell.h"

namespace Editor
{
  class PreferenceOverlay
  {
    private:
      SettingsShellState shellState{};

    public:
      // Renders the UE5-style Preferences window body. Changes auto-apply and
      // are persisted by the caller (editorScene) when the serialized prefs
      // differ, so this never returns a "close" request.
      void draw();
  };
}
