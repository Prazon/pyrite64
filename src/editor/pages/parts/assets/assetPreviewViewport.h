/**
* SPBF64 fork: tiny isolated 3D viewport for previewing a single mesh
* inside an asset editor window. Intentionally avoids the gizmo / picking /
* lighting / overlay machinery that lives in Editor::Viewport3D — this
* renders just the model, with an orbit camera, against a neutral background.
*/
#pragma once
#include <memory>

#include "../../../../renderer/camera.h"
#include "../../../../renderer/framebuffer.h"
#include "../../../../renderer/n64Mesh.h"
#include "../../../../renderer/object.h"
#include "../../../../renderer/skeleton.h"
#include "../../../../renderer/uniforms.h"
#include "../../../../project/scene/object.h"

namespace Project::Assets { struct Model3D; }
namespace Project { class AssetManager; }

namespace Editor
{
  class AssetPreviewViewport
  {
    private:
      Renderer::UniformGlobal uniGlobal{};
      Renderer::Framebuffer   fb{};
      Renderer::Camera        camera{};
      Renderer::Skeleton      dummySkeleton;

      // Renderer::Object owns shared_ptr<N64Mesh>. We hold a separate ptr
      // to the asset's mesh3D so we can call recreate() lazily.
      std::shared_ptr<Renderer::N64Mesh> meshPtr{};
      Renderer::Object        renderObj{};

      // Reference target for N64Mesh::ObjectRef (not actually read for the
      // matInstance==nullptr code path; see n64Mesh.cpp).
      Project::Object         dummyObj{};

      const Project::Assets::Model3D *modelRef{nullptr};
      uint64_t                lastAssetUUID{0};

      uint32_t  passId{};
      bool      hasMesh{false};
      bool      framed{false};
      bool      isOrbiting{false};
      bool      drewThisFrame{false};
      glm::vec2 mouseStart{};

      // Distance multipliers measured in mesh-AABB radii. frameMargin is
      // the auto-frame distance; minZoomFrac / maxZoomFrac are the dolly
      // bounds. A zero zoom-frac disables that side of the clamp (model
      // previews want free zoom; material previews override these).
      float     frameMargin{2.4f};
      float     minZoomFrac{0.0f};
      float     maxZoomFrac{0.0f};

      void onRenderPass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene);
      void clampCameraDistance();

    public:
      AssetPreviewViewport();
      ~AssetPreviewViewport();

      AssetPreviewViewport(const AssetPreviewViewport&) = delete;
      AssetPreviewViewport& operator=(const AssetPreviewViewport&) = delete;

      void setMesh(uint64_t assetUUID,
                   std::shared_ptr<Renderer::N64Mesh> mesh,
                   const Project::Assets::Model3D *model);
      void clear();

      void draw(ImVec2 size);

      // Renders into the framebuffer at the requested size without consuming
      // ImGui layout. Skips orbit/zoom interaction. Used by thumbnail
      // consumers that just want the rendered texture, not an inline image.
      void renderHeadless(ImVec2 size);

      // Tighten or relax the auto-frame distance and dolly bounds. All values
      // are in mesh-AABB radii. Pass 0 to either zoom-frac to disable that
      // side of the clamp (the default — same behaviour as before).
      void setFraming(float margin, float minFrac, float maxFrac);

      // Texture handle for the most recent render. Stable across frames
      // (SDL_GPU textures persist), so thumbnail consumers can keep
      // displaying it without triggering a fresh render.
      SDL_GPUTexture* getTexture() const;
  };
}
