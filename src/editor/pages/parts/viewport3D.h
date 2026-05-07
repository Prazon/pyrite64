/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <memory>

#include "../../../renderer/camera.h"
#include "../../../renderer/vertBuffer.h"
#include "../../../renderer/framebuffer.h"
#include "../../../renderer/mesh.h"
#include "../../../renderer/object.h"
#include "../../../renderer/skeleton.h"
#include "../../../utils/container.h"

namespace Editor
{
  class Viewport3D
  {
    private:
      Renderer::UniformGlobal uniGlobal{};
      Renderer::Skeleton dummySkeleton;
      Renderer::Framebuffer fb{};
      Renderer::Camera camera{};
      uint32_t passId{};

      bool isMouseHover{false};
      bool isMouseDown{false};
      Utils::RequestVal<uint32_t> pickedObjID{};
      bool pickAdditive{false};
      bool selectionPending{false};
      bool selectionDragging{false};

      float moveSpeedModifier{1.0f};
      float vpOffsetY{};
      glm::vec2 mousePos{};
      glm::vec2 mousePosStart{};
      glm::vec2 mousePosClick{};
      glm::vec2 selectionStart{};
      glm::vec2 selectionEnd{};

      std::shared_ptr<Renderer::Mesh> meshGrid{};
      Renderer::Object objGrid{};

      std::shared_ptr<Renderer::Mesh> meshLines{};
      Renderer::Object objLines{};

      std::shared_ptr<Renderer::Mesh> meshSprites{};
      Renderer::Object objSprites{};

      // SPBF64 fork: textured billboards are batched into a shared mesh and
      // drawn one-at-a-time in onRenderPass so each can bind its own texture.
      std::shared_ptr<Renderer::Mesh> meshBillboards{};
      struct SubmittedBillboard {
        uint32_t indexOffset;
        SDL_GPUTexture *texture;
        glm::vec4 sizeAndPivot;  // .xy cell w/h, .zw pivot x/y (in cell pixels)
        glm::vec4 uvRect;        // .xy uv0, .zw uv1
        glm::vec4 mode;          // .x worldPerPixel, others reserved
      };
      std::vector<SubmittedBillboard> submittedBillboards{};

      // SPBF64 fork: solid-shaded primitives (Box / Sphere / etc.) submitted
      // into a shared triangle mesh, rendered once via the "primitive"
      // pipeline. Per-vertex shading is pre-baked on the CPU.
      std::shared_ptr<Renderer::Mesh> meshPrimitives{};
      Renderer::Object objPrimitives{};

      bool showGrid{true};
      bool showCollMesh{false};
      bool showCollObj{true};

      int gizmoOp{0};
      bool gizmoTransformActive{false};

      void onRenderPass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene);
      void onCopyPass(SDL_GPUCommandBuffer* cmdBuff, SDL_GPUCopyPass *copyPass);
      void onPostRender(Renderer::Scene& renderScene);

    public:
      Viewport3D();
      ~Viewport3D();

      std::shared_ptr<Renderer::Mesh> getLines() {
        return meshLines;
      }

      std::shared_ptr<Renderer::Mesh> getSprites() {
        return meshSprites;
      }

      std::shared_ptr<Renderer::Mesh> getBillboards() {
        return meshBillboards;
      }

      std::shared_ptr<Renderer::Mesh> getPrimitives() {
        return meshPrimitives;
      }

      // Append a billboard quad to the shared billboard mesh and record its
      // texture/uniform parameters. Called from a component's draw3D.
      void addBillboardQuad(const glm::vec3 &worldPos, uint32_t objectId,
                            SDL_GPUTexture *texture,
                            const glm::vec4 &sizeAndPivot,
                            const glm::vec4 &uvRect,
                            const glm::vec4 &mode);

      /**
       * Moves the focused object to the position of the 3D viewport camera and with the same rotation.
       */
      bool alignFocusedObjectToCamera();

      void draw();
  };
}
