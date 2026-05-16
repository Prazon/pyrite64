/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once

#include "settingsShell.h"

namespace Editor
{
  class ProjectSettings
  {
    private:
      SettingsShellState shellState{};

    public:
      // Returns true when the user pressed Save (caller closes the window,
      // preserving the previous close-on-save behavior).
      bool draw();
  };
}
