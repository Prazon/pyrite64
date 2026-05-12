/**
* @copyright 2026 - Prazon
* @license MIT
*
* Generates <project>/src/p64/saveTable.{h,cpp} from every SAVE_FILE asset in
* the project. Always emits both files (even with no assets), so engine
* main.cpp can unconditionally call P64::saveAutoInit() — the generated
* saveTable.cpp provides a strong override of the engine's weak default.
*/
#pragma once

#include "../project/project.h"

namespace Build
{
  // Returns false on capacity overflow (project total slot count exceeds the
  // 16K EEPROM cap). On success, the generated files are written under
  // <project>/src/p64/saveTable.{h,cpp} regardless of whether any save assets
  // exist (the no-asset case emits stubs that produce a zero-slot Game::Save
  // namespace and a Game::Save::init() that returns true without touching
  // EEPROM).
  bool buildSaveTable(Project::Project &project);
}
