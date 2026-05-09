/**
 * @copyright 2026 - Prazon
 * @license MIT
 *
 * Per-UUID material thumbnail cache for the asset browser. Each entry owns
 * its own MaterialPreviewViewport so the framebuffer texture is stable
 * across frames (the asset card just samples it). Re-renders only when an
 * entry is dirty, capped at one render per frame so opening a project with
 * many materials doesn't stall the editor for several frames.
 */
#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>

#include <SDL3/SDL_gpu.h>

#include "imgui.h"

#include "../../../../project/assets/material.h"
#include "matPreviewViewport.h"

namespace Editor
{
  class MaterialThumbnailCache
  {
    private:
      struct Entry
      {
        MaterialPreviewViewport viewport{};
        bool dirty{true};
        bool everRendered{false};
      };
      std::unordered_map<uint64_t, std::unique_ptr<Entry>> entries{};
      int rendersThisFrame{0};
      static constexpr int MAX_PER_FRAME = 1;

    public:
      // Reset per-frame budget. AssetsBrowser calls this once per draw before
      // iterating material cards.
      void newFrame() { rendersThisFrame = 0; }

      // Returns the cached texture for `uuid`, rendering if dirty (subject
      // to the per-frame budget). Returns nullptr until the first render
      // completes, in which case the asset card should fall back to its
      // glyph icon.
      SDL_GPUTexture* fetch(uint64_t uuid, ImVec2 size,
                            const Project::Assets::Material &mat);

      // Marks the entry dirty so its next fetch re-renders. Called by
      // MaterialEditor::save() and any other code path that mutates a
      // material's compiled state.
      void invalidate(uint64_t uuid);

      // Drop a single entry (e.g. when an asset is deleted) or every entry
      // (e.g. on project unload).
      void erase(uint64_t uuid);
      void clear();
  };
}
