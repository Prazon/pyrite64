/**
 * @copyright 2026 - Prazon
 * @license MIT
 *
 * Per-UUID material thumbnail cache for the asset browser. Each entry either
 * owns a live MaterialPreviewViewport (rendered this session) or a cached
 * SDL_GPUTexture loaded from <project>/.cache/matThumb/<uuid>.png. Live
 * renders are capped at one per frame so opening a project with many
 * materials doesn't stall the editor; loaded entries skip rendering entirely.
 *
 * On every dirty -> clean transition the entry's framebuffer is read back to
 * RGBA8 and saved as a PNG so subsequent sessions can short-circuit the
 * render. Thumbnails are invalidated when a material is saved, and the PNG
 * is removed when the material asset itself is deleted.
 */
#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>

#include <SDL3/SDL_gpu.h>

#include "imgui.h"

#include "../../../../project/assets/material.h"
#include "../../../../renderer/texture.h"
#include "matPreviewViewport.h"

namespace Editor
{
  class MaterialThumbnailCache
  {
    private:
      struct Entry
      {
        // Live-rendered path: viewport owns its framebuffer and stays around
        // until invalidated. Null once a cached PNG has been loaded.
        std::unique_ptr<MaterialPreviewViewport> viewport{};
        // Loaded-from-disk path: a small RGBA8 GPU texture decoded from the
        // cached PNG. Null on the live-render path.
        std::unique_ptr<Renderer::Texture> loadedTex{};
        bool dirty{true};
        bool everRendered{false};
      };
      std::unordered_map<uint64_t, std::unique_ptr<Entry>> entries{};
      int rendersThisFrame{0};
      static constexpr int MAX_PER_FRAME = 1;

      // Side dimensions for the persisted PNG. The asset browser draws cards
      // at most ~128 px wide; rendering and saving at this resolution
      // matches what's drawn and keeps each PNG well under 50 KB.
      static constexpr int THUMB_PERSIST_PX = 128;

      // Returns true and consumes a frame budget slot if a live render
      // happened. May write a PNG to disk on success.
      bool renderAndPersist(uint64_t uuid, ImVec2 size,
                            const Project::Assets::Material &mat,
                            Entry &entry);

      // Try to load <project>/.cache/matThumb/<uuid>.png if present and not
      // older than the asset's source file. Populates entry.loadedTex on
      // success.
      bool tryLoadFromDisk(uint64_t uuid, Entry &entry);

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

      // Marks the entry dirty so its next fetch re-renders, and removes the
      // on-disk cached PNG so a stale image isn't picked up. Called by
      // MaterialEditor::save() and any other code path that mutates a
      // material's compiled state.
      void invalidate(uint64_t uuid);

      // Drop a single entry (e.g. when an asset is deleted) or every entry
      // (e.g. on project unload). erase() also removes the cached PNG so a
      // recreated material with the same UUID doesn't inherit a stale thumb.
      void erase(uint64_t uuid);
      void clear();
  };
}
