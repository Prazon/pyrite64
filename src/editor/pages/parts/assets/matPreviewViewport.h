/**
 * @copyright 2026 - Prazon
 * @license MIT
 *
 * Tiny offscreen 3D viewport that previews a single Project::Assets::Material
 * applied to a polygonal host cube. Wraps AssetPreviewViewport (which already
 * handles framebuffer, camera, and the n64 render pass) and adds:
 *   1. Lazy-loaded host mesh from data/preview/material_host.glb.
 *   2. setMaterial() that overrides every entry in the host's materials map,
 *      so the next render uses the supplied material's prim/env/CC/textures.
 *
 * The host is a cube on purpose. Pyrite64 is a polygonal N64-style engine, so
 * a sphere would lie about how the material reads on real geometry.
 */
#pragma once
#include <memory>

#include "imgui.h"

#include "../../../../project/assets/material.h"
#include "../../../../project/assets/model3d.h"
#include "../../../../renderer/n64Mesh.h"
#include "assetPreviewViewport.h"

namespace Editor
{
  class MaterialPreviewViewport
  {
    private:
      AssetPreviewViewport viewport{};

      // The cube. Loaded on first draw (parseGLTF + N64Mesh::fromT3DM). Owned
      // here so we can mutate the materials map between renders.
      std::unique_ptr<Project::Assets::Model3D> hostModel{};
      std::shared_ptr<Renderer::N64Mesh> hostMesh{};
      bool hostLoaded{false};
      bool hostFailed{false};

      // Stamped onto the host every render. Buffered so callers can hand us
      // a material before ensureHost() succeeds; once the host loads, the
      // pending material is applied automatically.
      Project::Assets::Material pendingMat{};
      bool hasPending{false};

      bool ensureHost();
      void applyPendingIfReady();

    public:
      MaterialPreviewViewport() = default;

      MaterialPreviewViewport(const MaterialPreviewViewport&) = delete;
      MaterialPreviewViewport& operator=(const MaterialPreviewViewport&) = delete;

      void setMaterial(const Project::Assets::Material &mat);
      void draw(ImVec2 size);

      // Headless render path for the thumbnail cache. Triggers a render at
      // the supplied size and returns the framebuffer texture for the most
      // recent render. Returns nullptr if the host hasn't loaded yet (caller
      // should fall back to a glyph until a later frame).
      SDL_GPUTexture* renderHeadless(ImVec2 size);
      SDL_GPUTexture* getTexture() const;
  };
}
