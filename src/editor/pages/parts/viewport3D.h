/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <memory>
#include <string>

#include "../../../renderer/camera.h"
#include "../../../renderer/vertBuffer.h"
#include "../../../renderer/framebuffer.h"
#include "../../../renderer/mesh.h"
#include "../../../renderer/object.h"
#include "../../../renderer/skeleton.h"
#include "../../../utils/container.h"

namespace Project { class Scene; class Selection; }

namespace Editor
{
  // SPBF64 fork: returns the Selection that applies to whichever Viewport3D
  // is currently rendering its content. Set by ViewportSelectionScope inside
  // Viewport3D's draw / render-pass paths; read by component draw functions
  // (compModel, compSpriteBillboard, etc.) when they want to draw selection
  // highlights. Falls back to ctx.mainSelection when no viewport is active,
  // so non-viewport call sites get the main scene's selection.
  Project::Selection& activeViewportSelection();

  class ViewportSelectionScope
  {
    Project::Selection* prev{nullptr};
    public:
      explicit ViewportSelectionScope(Project::Selection& sel);
      ~ViewportSelectionScope();
      ViewportSelectionScope(const ViewportSelectionScope&) = delete;
      ViewportSelectionScope& operator=(const ViewportSelectionScope&) = delete;
  };

  class Viewport3D
  {
    private:
      Renderer::UniformGlobal uniGlobal{};
      Renderer::Skeleton dummySkeleton;
      Renderer::Framebuffer fb{};
      Renderer::Camera camera{};
      uint32_t passId{};

      // SPBF64 fork: Picture-in-Picture preview of the selected Comp::Camera.
      // Runs a second render pass against `fbPreview` driven by `previewSpec`
      // and is composited into the main viewport as a corner thumbnail.
      Renderer::Framebuffer fbPreview{};
      Renderer::UniformGlobal uniGlobalPreview{};
      struct PreviewCamSpec {
        bool active{false};
        glm::vec3 pos{};
        glm::quat rot{0,0,0,1};
        float fov{65.0f};       // degrees
        float nearD{100.0f};
        float farD{4000.0f};
        float aspect{0.0f};     // 0 means "derive from vpSize"
        glm::ivec2 vpSize{320, 240};
        std::string name{};
      };
      PreviewCamSpec previewSpec{};

      bool isMouseHover{false};
      bool isMouseDown{false};
      Utils::RequestVal<uint32_t> pickedObjID{};
      bool pickAdditive{false};
      bool selectionPending{false};
      bool selectionDragging{false};
      bool cameraDragActive{false};

      float moveSpeedModifier{1.0f};
      float vpOffsetY{};
      glm::vec2 cursorLockPos{};
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

      // SPBF64 fork: scene + selection this viewport drives. nullptr means
      // "use the project's currently-loaded scene + ctx.mainSelection" — that
      // path keeps the main editor's 3D viewport behaving as before. Set by
      // the (Scene&, Selection&) ctor for the prefab editor's viewport.
      Project::Scene* boundScene{nullptr};
      Project::Selection* boundSelection{nullptr};

      // Render-pass / copy-pass / post-render callbacks fire from ctx.scene
      // whether or not the host window's body actually called draw() this
      // frame. This flag gates them: draw() sets it; the passes skip when
      // it's false. Without this, closing a docked PrefabEditor (where
      // Begin() returns false and the body is skipped) still wrote to the
      // viewport's framebuffer one last time, and the GPU texture release
      // one frame later raced the in-flight GPU command buffer → crash.
      // Mirrors AssetPreviewViewport's drewThisFrame.
      bool drewThisFrame{false};

      Project::Scene* getScene() const;
      Project::Selection& getSelection() const;

      void onRenderPass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene);
      void onCopyPass(SDL_GPUCommandBuffer* cmdBuff, SDL_GPUCopyPass *copyPass);
      void onPostRender(Renderer::Scene& renderScene);

    public:
      Viewport3D();
      Viewport3D(Project::Scene& scene, Project::Selection& selection);
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
