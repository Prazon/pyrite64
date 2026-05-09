/**
 * @copyright 2026 - Prazon
 * @license MIT
 *
 * Per-UUID thumbnail cache for MODEL_3D assets, mirroring
 * MaterialThumbnailCache. Each entry either owns a live AssetPreviewViewport
 * pointed at the asset's mesh3D / Model3D, or a small SDL_GPUTexture decoded
 * from <project>/.cache/modelThumb/<uuid>.png. Live renders are capped at
 * one per frame so opening a project with many model assets doesn't stall
 * the editor; loaded entries skip rendering entirely.
 *
 * On every dirty -> clean transition the framebuffer is read back to RGBA8
 * and saved as a PNG so subsequent sessions can short-circuit the render.
 * The PNG is removed when the model asset itself is deleted; an asset whose
 * source mtime advances past the cached PNG is treated as stale and
 * re-rendered.
 */
#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>

#include <SDL3/SDL_gpu.h>

#include "imgui.h"

#include "../../../../project/assetManager.h"
#include "../../../../renderer/texture.h"
#include "assetPreviewViewport.h"

namespace Editor
{
  class ModelThumbnailCache
  {
    private:
      struct Entry
      {
        std::unique_ptr<AssetPreviewViewport> viewport{};
        std::unique_ptr<Renderer::Texture> loadedTex{};
        bool dirty{true};
        bool everRendered{false};
        // Once the live viewport has been bound to a mesh, remember whose
        // mesh raw-pointer we used so we can re-bind if the asset reloads
        // and the underlying N64Mesh handle changes.
        void *boundMeshRaw{nullptr};
      };
      std::unordered_map<uint64_t, std::unique_ptr<Entry>> entries{};
      int rendersThisFrame{0};
      static constexpr int MAX_PER_FRAME = 1;
      static constexpr int THUMB_PERSIST_PX = 128;

      bool tryLoadFromDisk(uint64_t uuid, Entry &entry);
      bool renderAndPersist(const Project::AssetManagerEntry &asset, Entry &entry);

    public:
      void newFrame() { rendersThisFrame = 0; }

      // Returns the cached texture for `asset`, rendering if dirty (subject
      // to the per-frame budget). Returns nullptr until the first render
      // completes, in which case the asset card should fall back to its
      // glyph icon.
      SDL_GPUTexture* fetch(const Project::AssetManagerEntry &asset);

      // Marks the entry dirty so its next fetch re-renders, and removes the
      // on-disk cached PNG so a stale image isn't picked up.
      void invalidate(uint64_t uuid);

      // Drop a single entry (asset deleted) or all entries (project unloaded).
      void erase(uint64_t uuid);
      void clear();
  };
}
