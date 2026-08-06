/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <memory>
#include <string>
#include "json.hpp"

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
      bool detached{false};

      // Picture-in-Picture preview of a selected Comp::Camera. Runs a second
      // render pass into `fbPreview` and is composited into the main viewport
      // as a bottom-right thumbnail.
      Renderer::Framebuffer fbPreview{};
      Renderer::UniformGlobal previewUniGlobal{};
      bool showCameraPreview{false};
      uint32_t previewCameraUUID{0};   // selected scene object whose transform drives the preview
      uint32_t previewSrcUUID{0};      // object whose components own the Camera entry (== previewCameraUUID for non-prefab)
      glm::vec2 previewScreenSize{};
      // When the selected object carries a PathFollow that resolves a Path,
      // the PiP rides the spline at the inspector scrubber distance instead
      // of the camera's authored pose. previewFollowUUID is that object.
      bool previewPathFollow{false};
      uint32_t previewFollowUUID{0};

      bool isMouseHover{false};
      bool isMouseDown{false};
      // Per-instance "mouse is over the camera-rotation gizmo" — was a
      // file-static, but with multiple viewports (main editor + prefab
      // editor's docked viewport) the static was getting stomped between
      // draws, so the inactive viewport's gizmo would light up when the
      // mouse hovered over the *other* viewport.
      bool overRotGizmo{false};
      Utils::RequestVal<uint32_t> pickedObjID{};
      bool pickAdditive{false};
      bool selectionPending{false};
      bool selectionDragging{false};
      bool cameraDragActive{false};
      // SDL_Window we enabled relative mouse mode on at drag start, so we can
      // disable it on the same window even if ImGui's current viewport
      // changes (e.g. user releases the button after the viewport flickered).
      void* cameraDragWindow{nullptr};
      bool cameraDragFlush{false};

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
      bool showIcons{true};
      bool iconsVisible{true}; 
      bool cleanPreview{false};

      // Mirror the editor view onto a scene camera (0 = free fly).
      uint64_t boundCameraUUID{0};
      bool useCameraRes{false};
      // Free-fly camera ortho toggle. Keep separate so a bound camera's projection doesn't overwrite.
      bool freeFlyOrtho{false};
      float fbScale{1.0f}; // framebuffer pixels per displayed pixel (used for object picking)

      int gizmoOp{0};
      bool gizmoTransformActive{false};
      bool inputActive{false};    // this viewport captured the camera drag (started inside it)
      bool viewGizmoOwned{false}; // this viewport owns the active orientation-cube drag
      bool navLocked{false};      // viewport lock mode: cursor captured, right-click to toggle

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

      // Renders the scene into `targetFb` using `targetUni` as the global
      // uniform. When drawEditorHelpers is true, gizmos / lines / sprites /
      // collision helpers / object-id sprites are drawn; when false, only
      // gameplay-visible content is rendered (used by the camera preview).
      void renderScenePass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene,
                           Renderer::Framebuffer &targetFb, Renderer::UniformGlobal &targetUni,
                           bool drawEditorHelpers);

      // Walks the current selection looking for the first object with a
      // Camera component and prepares preview state (UUIDs + framebuffer).
      // No-op if nothing in the selection has a Camera.
      void updateCameraPreviewState(const ImVec2 &currSize, Project::Scene *scene);

      // Draws the PiP overlay (frame + camera image + label) at the bottom
      // right of the viewport. No-op when showCameraPreview is false.
      void drawCameraPreviewOverlay(const ImVec2 &currPos, const ImVec2 &currSize);

      // Toggles dragging which hides the cursor for infinite movement
      void setCameraDrag(bool active);

    public:
      uint32_t winId{0};

      Viewport3D();
      Viewport3D(Project::Scene& scene, Project::Selection& selection);
      ~Viewport3D();

      void detach();

      // Stable per-instance ImGui window title; the "###" id keeps docking state across renames.
      std::string getWindowTitle() const {
        if (winId == 0) return "3D-Viewport";
        return "3D-Viewport " + std::to_string(winId + 1) + "###vp" + std::to_string(winId);
      }

      bool isViewHovered() const { return isMouseHover; }

      nlohmann::json saveState() const;
      void loadState(const nlohmann::json &j);

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
