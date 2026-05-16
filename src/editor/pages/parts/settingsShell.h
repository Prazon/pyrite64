/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <string>
#include <vector>
#include <functional>

namespace Editor
{
  // One left-pane entry. `section` groups entries under a header in the tree
  // (UE5 groups categories under "Project" / "Engine" / "Editor" ...).
  struct SettingsCategory
  {
    std::string id;                 // stable key, also the persisted selection
    std::string label;              // shown in the tree + as the page header
    const char *icon = "";          // ICON_MDI_* (may be empty)
    std::string section;            // tree group header
    std::function<void()> draw;     // page body; uses ImTable::addPref etc.
  };

  struct SettingsShellState
  {
    std::string selectedId;
    std::string search;
  };

  // UE5-style settings layout: a search box, a left category tree grouped by
  // section, and a right content pane for the selected category. The active
  // search string is pushed into ImTable's row filter so pref rows narrow
  // live. Consumes GetContentRegionAvail(); the caller owns the window chrome
  // and any footer (Save button / dirty marker).
  void drawSettingsShell(const char *idStr,
                         const std::vector<SettingsCategory> &cats,
                         SettingsShellState &st);
}
