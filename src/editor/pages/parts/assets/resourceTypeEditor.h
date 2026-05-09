/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

namespace Project {
  struct AssetManagerEntry;
}

namespace Editor::ResourceTypeEditor
{
  // Renders the field-schema editor (Add / Rename / Duplicate / Delete) for an
  // editor-authored RESOURCE_TYPE asset. Mutations are persisted to the
  // .p64restype file on the spot, mirroring how the AssetInspector treats
  // RESOURCE_INSTANCE edits.
  //
  // Caller must have already verified entry.type == RESOURCE_TYPE and
  // entry.resourceType != nullptr.
  void draw(Project::AssetManagerEntry &entry);
}
