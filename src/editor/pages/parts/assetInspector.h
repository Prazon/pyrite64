/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>

namespace Editor::AssetInspector
{
  // Render the per-asset settings + preview strip for `assetUUID`.
  // Hosted as the right-side panel inside each per-asset editor window
  // (ImageEditor, ModelEditor, FontEditor, etc.).
  void draw(uint64_t assetUUID);
}
