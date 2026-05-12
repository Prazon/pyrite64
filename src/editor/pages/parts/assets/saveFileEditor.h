/**
* @copyright 2026 - Prazon
* @license MIT
*
* Schema editor for a SAVE_FILE (.p64save) asset. Lets the user add/remove/
* rename/retype fields and edit defaults. Persists every mutation to disk and
* threads through Editor::UndoRedo. Mirrors ResourceTypeEditor's two-pane
* layout (field list left, details right).
*/
#pragma once

namespace Project {
  struct AssetManagerEntry;
}

namespace Editor::SaveFileEditor
{
  // Caller must have already verified entry.type == SAVE_FILE and
  // entry.saveFileAsset != nullptr.
  void draw(Project::AssetManagerEntry &entry);
}
