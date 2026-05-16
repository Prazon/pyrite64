/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "viewport3D.h"

#include <algorithm>
#include "imgui.h"
#include "../../imgui/theme.h"
#include "ImGuizmo.h"
#include "ImViewGuizmo.h"
#include "../../../context.h"
#include "../../../renderer/mesh.h"
#include "../../../renderer/object.h"
#include "../../../renderer/scene.h"
#include "../../../renderer/uniforms.h"
#include "../../../utils/meshGen.h"
#include "../../../utils/colors.h"
#include "../../../project/component/components.h"
#include "../../../project/scene/scene.h"
#include "../../../project/selection.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "SDL3/SDL_gpu.h"
#include "IconsMaterialDesignIcons.h"
#include "../../undoRedo.h"
#include "../../selectionUtils.h"

#include "../../../utils/logger.h"

// Declared in main.cpp at global scope; needed by the billboard pass to bind
// a sampler when drawing per-component textured billboards.
extern SDL_GPUSampler *texSamplerRepeat;

namespace
{
  // SPBF64 fork: thread-local pointer to the Selection that applies to the
  // currently-rendering Viewport3D. Set by ViewportSelectionScope; read by
  // Editor::activeViewportSelection() (which components call when drawing
  // selection highlights). Per-thread so background submission threads don't
  // race with the editor thread. nullptr => fall back to ctx.mainSelection.
  thread_local Project::Selection* activeVPSel{nullptr};

  constinit uint32_t nextPassId{0};

  // Component IDs referenced by the camera-preview pass. Mirrors the IDs
  // declared in components.h's TABLE; kept here so the magic numbers don't
  // litter the render loop.
  constexpr int COMPONENT_ID_CAMERA = 3;
  constexpr int COMPONENT_ID_MODEL_STATIC = 1;
  constexpr int COMPONENT_ID_MODEL_ANIMATED = 10;

  // Camera-preview thumbnail sizing. Scales with the host viewport but
  // clamps to a useful minimum and a target fraction so it never devours
  // the editor view.
  constexpr float PREVIEW_SIZE_FACTOR = 0.3f;
  constexpr float PREVIEW_MIN_WIDTH = 160.0f;
  constexpr float PREVIEW_MIN_HEIGHT = 120.0f;
  constexpr float PREVIEW_MIN_SIZE = 64.0f;
  constexpr float PREVIEW_VIEWPORT_PADDING = 24.0f;
  constexpr float PREVIEW_MIN_ASPECT = 0.25f;
  constexpr float PREVIEW_DEFAULT_ASPECT = 16.0f / 9.0f;

  // Returns the first Camera entry on `obj`, or nullptr if none.
  Project::Component::Entry* getCameraComponent(Project::Object &obj)
  {
    for (auto &comp : obj.components) {
      if (comp.id == COMPONENT_ID_CAMERA) return &comp;
    }
    return nullptr;
  }

  constexpr int COMPONENT_ID_PATH_FOLLOW = 31;

  // Depth-first search for a Camera on obj or any descendant. Returns the
  // owning object and sets outComp. Lets a PathFollow object preview through
  // a Camera rigged on a child (the common camera-on-a-mount setup).
  Project::Object* findCameraInTree(Project::Object &obj,
                                    Project::Component::Entry*& outComp)
  {
    if (auto *c = getCameraComponent(obj)) { outComp = c; return &obj; }
    for (auto &child : obj.children) {
      if (!child) continue;
      if (auto *owner = findCameraInTree(*child, outComp)) return owner;
    }
    return nullptr;
  }

  bool hasPathFollow(Project::Object &obj)
  {
    for (auto &comp : obj.components) {
      if (comp.id == COMPONENT_ID_PATH_FOLLOW) return true;
    }
    return false;
  }

  constexpr ImGuizmo::OPERATION GIZMO_OPS[3] {
    ImGuizmo::OPERATION::TRANSLATE,
    ImGuizmo::OPERATION::ROTATE,
    ImGuizmo::OPERATION::SCALE
  };
  constinit bool isTransWorld = true;

  // A toggleable "connected" button (like in toolbars)
  bool ConnectedToggleButton(const char* text, bool active, bool first, bool last, ImVec2 size = ImVec2(20, 20))
  {
    ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Remove spacing so buttons touch
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushID(text); // ensure unique id for InvisibleButton

    // Create an invisible button to get interaction & layout
    bool pressed = ImGui::InvisibleButton("##invis", size);
    ImGui::PopID();

    // Get item rect
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();

    // Choose background color based on active / hovered / held
    ImU32 col;
    if (active) {
        col = ImGui::GetColorU32(ImGuiCol_ButtonActive);
    } else if (ImGui::IsItemActive()) {
        col = ImGui::GetColorU32(ImGuiCol_ButtonActive); // pressed
    } else if (ImGui::IsItemHovered()) {
        col = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    } else {
        col = ImGui::GetColorU32(ImGuiCol_Button);
    }

    // Corner rounding amount
    float rounding = style.FrameRounding;
    if (rounding <= 0.0f) rounding = 0.0f;

    // Decide which corners to round (ImDrawFlags_RoundCornersXXX)
    ImDrawFlags round_flags = ImDrawFlags_RoundCornersNone;
    if (first && last) {
        // single button -> round all corners
        round_flags = ImDrawFlags_RoundCornersAll;
    } else if (first) {
        round_flags = (ImDrawFlags)(ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft);
    } else if (last) {
        round_flags = (ImDrawFlags)(ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomRight);
    } else {
        round_flags = ImDrawFlags_RoundCornersNone;
    }

    // Draw filled background with chosen rounded corners
    draw_list->AddRectFilled(a, b, col, rounding, (int)round_flags);

    // Optional border
    if (style.FrameBorderSize > 0.0f) {
        ImU32 border_col = ImGui::GetColorU32(ImGuiCol_Border);
        draw_list->AddRect(a, b, border_col, rounding, (int)round_flags, style.FrameBorderSize);
    }

    // Draw the label text centered inside the rect
    ImVec2 text_size = ImGui::CalcTextSize(text);
    ImVec2 text_pos = ImVec2( (a.x + b.x - text_size.x) * 0.5f, (a.y + b.y - text_size.y) * 0.5f );

    // respect style.FramePadding vertically/horizontally if size.x == 0 (auto width)
    // but invisible button size could be 0 -> then rect height is determined by style.FramePadding
    draw_list->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), text);

    // Restore spacing style
    ImGui::PopStyleVar();

    // If not last, put next button on same line with zero spacing
    if (!last) ImGui::SameLine(0, 0);

    return pressed;
  }

  std::shared_ptr<Renderer::Texture> sprites{};
  uint32_t spritesRefCount{0};

  void iterateObjects(
    Project::Object& parent,
    std::function<void(Project::Object&, Project::Component::Entry*)> callback
  )
  {
    for(auto& child : parent.children)
    {
      if(!child->enabled)continue;

      auto srcObj = child.get();
      if(child->isPrefabInstance()) {
        auto prefab = ctx.project->getAssets().getPrefabByUUID(child->uuidPrefab.value);
        if(prefab)srcObj = &prefab->obj;
      }

      for(auto &comp : srcObj->components) {
        callback(*child, &comp);
      }
      callback(*child, nullptr);

      iterateObjects(*child, callback);
    }
  }

  void applyDeltaToChildren(
    Project::Object &obj,
    const std::unordered_map<uint64_t, glm::vec3> &relPosMap,
    const glm::mat4 &mat
  ) {
    for(auto& child : obj.children)
    {
      // if child itself is selected, skip (already transformed with it)
      // SPBF64 fork: route through the active viewport's selection so that
      // gizmo-drag of a selection in the prefab editor doesn't drag children
      // by the main scene's selection.
      if(Editor::activeViewportSelection().isSelected(child->uuid))continue;

      auto it = relPosMap.find(child->uuid);
      if(it == relPosMap.end())continue;
      child->pos.resolve(child->propOverrides) = mat * glm::vec4(it->second, 1.0f);
    }
  }

  /**
   * Ensures a property has an override entry before writing to it on an object instance.
   * @tparam T Value type stored by the property.
   * @param obj Object whose override map will be updated.
   * @param prop Property that may need an override entry.
   */
  template<typename T>
  void ensurePropertyOverride(Project::Object *obj, Property<T> &prop)
  {
    if (obj->propOverrides.find(prop.id) == obj->propOverrides.end()) {
      obj->addPropOverride(prop);
    }
  }
}

Editor::Viewport3D::Viewport3D()
  : dummySkeleton{ctx.gpu}
{
  if(spritesRefCount == 0) {
    sprites = std::make_shared<Renderer::Texture>(ctx.gpu, "data/img/icons/sprites.png");
  }
  ++spritesRefCount;

  passId = ++nextPassId;
  ctx.scene->addRenderPass(passId, [this](SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene) {
    onRenderPass(cmdBuff, renderScene);
  });
  ctx.scene->addCopyPass(passId, [this](SDL_GPUCommandBuffer* cmdBuff, SDL_GPUCopyPass *copyPass) {
    // Gate the lambda on drewThisFrame: dummySkeleton.update uploads to a
    // GPU storage buffer; running it on a soon-to-be-destroyed viewport
    // races the destructor's buffer release on the next frame.
    if (!drewThisFrame) return;
    dummySkeleton.update(*copyPass);
    onCopyPass(cmdBuff, copyPass);
  });
  ctx.scene->addPostRenderCallback(passId, [this](Renderer::Scene& renderScene) {
    onPostRender(renderScene);
  });

  meshGrid = std::make_shared<Renderer::Mesh>();
  Utils::Mesh::generateGrid(*meshGrid, 20);
  meshGrid->recreate(*ctx.scene);
  objGrid.setMesh(meshGrid);
  objGrid.setScale(50);

  meshLines = std::make_shared<Renderer::Mesh>();
  objLines.setMesh(meshLines);

  meshSprites = std::make_shared<Renderer::Mesh>();
  objSprites.setMesh(meshSprites);

  meshBillboards = std::make_shared<Renderer::Mesh>();

  meshPrimitives = std::make_shared<Renderer::Mesh>();
  objPrimitives.setMesh(meshPrimitives);
}

// SPBF64 fork: bound-scene ctor used by PrefabEditor (and any future per-asset
// editor that wants a 3D preview of its own scene). Behaves identically to
// the default ctor except getScene()/getSelection() resolve to the bound
// instances instead of falling back to the project's loaded scene.
Editor::Viewport3D::Viewport3D(Project::Scene& scene, Project::Selection& selection)
  : Viewport3D{}
{
  boundScene = &scene;
  boundSelection = &selection;
}

Project::Scene* Editor::Viewport3D::getScene() const
{
  if (boundScene) return boundScene;
  return ctx.project ? ctx.project->getScenes().getLoadedScene() : nullptr;
}

Project::Selection& Editor::Viewport3D::getSelection() const
{
  return boundSelection ? *boundSelection : ctx.mainSelection;
}

Editor::ViewportSelectionScope::ViewportSelectionScope(Project::Selection& sel)
{
  prev = activeVPSel;
  activeVPSel = &sel;
}

Editor::ViewportSelectionScope::~ViewportSelectionScope()
{
  activeVPSel = prev;
}

Project::Selection& Editor::activeViewportSelection()
{
  return activeVPSel ? *activeVPSel : ctx.mainSelection;
}

void Editor::Viewport3D::addBillboardQuad(const glm::vec3 &worldPos, uint32_t objectId,
                                          SDL_GPUTexture *texture,
                                          const glm::vec4 &sizeAndPivot,
                                          const glm::vec4 &uvRect,
                                          const glm::vec4 &mode)
{
  if (!texture) return;

  auto &mesh = *meshBillboards;
  uint16_t baseIdx = (uint16_t)mesh.vertLines.size();
  uint32_t indexOffset = (uint32_t)mesh.indices.size();

  // 4 verts, identical data; vertex shader uses gl_VertexIndex % 4 for corner.
  Renderer::LineVertex v{};
  v.pos = worldPos;
  v.objectId = objectId;
  v.color = {0xFF, 0xFF, 0xFF, 0xFF};
  for (int i = 0; i < 4; ++i) {
    mesh.vertLines.push_back(v);
  }
  // Two triangles: 0,1,2 and 0,2,3
  mesh.indices.push_back(baseIdx + 0);
  mesh.indices.push_back(baseIdx + 1);
  mesh.indices.push_back(baseIdx + 2);
  mesh.indices.push_back(baseIdx + 0);
  mesh.indices.push_back(baseIdx + 2);
  mesh.indices.push_back(baseIdx + 3);

  submittedBillboards.push_back({
    .indexOffset = indexOffset,
    .texture = texture,
    .sizeAndPivot = sizeAndPivot,
    .uvRect = uvRect,
    .mode = mode,
  });
}

Editor::Viewport3D::~Viewport3D() {
  if (ctx.scene) {
    ctx.scene->removeRenderPass(passId);
    ctx.scene->removeCopyPass(passId);
    ctx.scene->removePostRenderCallback(passId);
  }

  if(--spritesRefCount == 0) {
    sprites = nullptr;
  }
}

bool Editor::Viewport3D::alignFocusedObjectToCamera()
{
  auto* scene = getScene();
  // No scene loaded or no object selected --> Abort
  if (!scene || getSelection().primary() == 0)return false;

  auto obj = scene->getObjectByUUID(getSelection().primary());
  // Cannot get selected object --> Abort
  if (!obj)return false;

  // Prefab instances store transform edits in overrides, so create them before writing position or rotation
  if (obj->isPrefabInstance() && !obj->isPrefabEdit) {
    ensurePropertyOverride(obj.get(), obj->pos);
    ensurePropertyOverride(obj.get(), obj->rot);
  }

  // Read current transform to preserve child offsets after moving the parent to the editor camera
  glm::vec3 skew{0.0f};
  glm::vec4 persp{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec3 objScale = obj->scale.resolve(obj->propOverrides);
  glm::quat objRot = obj->rot.resolve(obj->propOverrides);
  glm::vec3 objPos = obj->pos.resolve(obj->propOverrides);

  // Rebuild current object matrix so child transforms can be converted into the old local space
  auto oldObjMatrix = glm::recompose(objScale, objRot, objPos, skew, persp);

  // Cache each child in the local space of the old transform so they can be rebuilt relative to the new one
  std::unordered_map<uint64_t, glm::vec3> relPosMap{};
  for (auto& child : obj->children)
  {
    relPosMap[child->uuid] = glm::inverse(oldObjMatrix) * glm::vec4(
      child->pos.resolve(child->propOverrides), 1.0f
    );
  }

  // Copy editor camera transform to focused object
  obj->pos.resolve(obj->propOverrides) = camera.pos;
  obj->rot.resolve(obj->propOverrides) = glm::normalize(camera.rot);

  // Recompose new object matrix so child world positions can be updated consistently
  auto newObjMatrix = glm::recompose(
    obj->scale.resolve(obj->propOverrides),
    obj->rot.resolve(obj->propOverrides),
    obj->pos.resolve(obj->propOverrides),
    skew,
    persp
  );

  // Re-apply cached child offsets relative to new parent transform
  applyDeltaToChildren(*obj, relPosMap, newObjMatrix);

  // Add to history
  UndoRedo::getHistory().markChanged("Align object to camera");
  
  return true;
}

void Editor::Viewport3D::renderScenePass(
  SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene,
  Renderer::Framebuffer &targetFb, Renderer::UniformGlobal &targetUni,
  bool drawEditorHelpers
)
{
  auto* scene = getScene();
  if (!scene) return;

  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
    cmdBuff, targetFb.getTargetInfo(), targetFb.getTargetInfoCount(), &targetFb.getDepthTargetInfo()
  );
  renderScene.getPipeline("n64").bind(pass);
  dummySkeleton.use(pass);
  SDL_PushGPUVertexUniformData(cmdBuff, 0, &targetUni, sizeof(targetUni));
  auto &rootObj = scene->getRootObject();

  if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, drawEditorHelpers ? "3D Objects" : "Camera Preview Objects");

  bool hadDraw = false;
  iterateObjects(rootObj, [&](Project::Object &obj, Project::Component::Entry *comp) {
    if(!comp)
    {
      if(drawEditorHelpers && !hadDraw) {
        glm::u8vec4 spriteCol{0xFF, 0xFF, 0xFF, 0xFF};
        if (getSelection().isSelected(obj.uuid)) {
          spriteCol = Utils::Colors::kSelectionTint;
        }
        Utils::Mesh::addSprite(*getSprites(), obj.pos.resolve(obj.propOverrides), obj.uuid, 2, spriteCol);
      }
      hadDraw = false;
      return;
    }
    auto &def = Project::Component::TABLE[comp->id];

    // @TODO: use flag in component
    if(!showCollMesh && comp->id == 4)return;
    if(!showCollObj && comp->id == 5)return;
    // Camera preview shows only models; gameplay sprites/primitives are
    // re-issued below from buffers the main pass already populated.
    if(!drawEditorHelpers && comp->id != COMPONENT_ID_MODEL_STATIC && comp->id != COMPONENT_ID_MODEL_ANIMATED) return;

    if(def.funcDraw3D) {
      def.funcDraw3D(obj, *comp, *this, cmdBuff, pass);
      hadDraw = true;
    }
  });

  if (drawEditorHelpers) {
    iterateObjects(rootObj, [&](Project::Object &obj, Project::Component::Entry *comp) {
      if(!comp)return;
      auto &def = Project::Component::TABLE[comp->id];

      // @TODO: use flag in component
      if(!showCollMesh && comp->id == 4)return;
      if(!showCollObj && comp->id == 5)return;

      if(def.funcDrawPost3D) {
        def.funcDrawPost3D(obj, *comp, *this, cmdBuff, pass);
      }
    });
  }

  if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);

  if (drawEditorHelpers) {
    meshLines->recreate(renderScene);
    meshSprites->recreate(renderScene);
    meshBillboards->recreate(renderScene);
    meshPrimitives->recreate(renderScene);
  }

  // Solid-shaded primitives. Drawn before lines so the line gizmos
  // (selection outlines) sit on top of the filled surface.
  if (!meshPrimitives->vertLines.empty()) {
    if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "3D Primitives");
    renderScene.getPipeline("primitive").bind(pass);
    objPrimitives.draw(pass, cmdBuff);
    if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);
  }

  if (drawEditorHelpers) {
    if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "3D Lines");
    renderScene.getPipeline("lines").bind(pass);

    if(showGrid)objGrid.draw(pass, cmdBuff);
    objLines.draw(pass, cmdBuff);

    // hack to get thicker lines with AA, just draw again with a 1px offset in screen-space
    if(ctx.prefs.renderFactorAA > 1.0f) {
      auto oldMat = targetUni.projMat[2];
      targetUni.projMat[2][0] += 1.0f / targetUni.screenSize.x;
      targetUni.projMat[2][1] -= 1.0f / targetUni.screenSize.y;
      SDL_PushGPUVertexUniformData(cmdBuff, 0, &targetUni, sizeof(targetUni));

      if(showGrid)objGrid.draw(pass, cmdBuff);
      objLines.draw(pass, cmdBuff);

      targetUni.projMat[2] = oldMat;
      SDL_PushGPUVertexUniformData(cmdBuff, 0, &targetUni, sizeof(targetUni));
    }
    if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);

    if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "3D Sprites");
    renderScene.getPipeline("sprites").bind(pass);
    sprites->bind(pass);
    objSprites.draw(pass, cmdBuff);
    if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);
  }

  // Textured billboard quads — drawn one-at-a-time so each can bind its own
  // texture. Uses the buffers populated during the main pass.
  if (!submittedBillboards.empty()) {
    if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "3D Billboards");

    renderScene.getPipeline("billboard").bind(pass);

    // Re-push UniformGlobal in case the lines AA pass perturbed it.
    SDL_PushGPUVertexUniformData(cmdBuff, 0, &targetUni, sizeof(targetUni));

    for (const auto &bb : submittedBillboards) {
      struct {
        glm::vec4 sizeAndPivot;
        glm::vec4 uvRect;
        glm::vec4 mode;
      } params{ bb.sizeAndPivot, bb.uvRect, bb.mode };
      SDL_PushGPUVertexUniformData(cmdBuff, 1, &params, sizeof(params));

      SDL_GPUTextureSamplerBinding binding{
        .texture = bb.texture,
        .sampler = texSamplerRepeat,
      };
      SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

      meshBillboards->draw(pass, bb.indexOffset, 6);
    }

    if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);
  }

  SDL_EndGPURenderPass(pass);
}

void Editor::Viewport3D::onRenderPass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene)
{
  if(fb.getTexture() == nullptr)return;
  meshLines->vertLines.clear();
  meshLines->indices.clear();

  meshSprites->vertLines.clear();
  meshSprites->indices.clear();

  meshBillboards->vertLines.clear();
  meshBillboards->indices.clear();
  submittedBillboards.clear();

  meshPrimitives->vertLines.clear();
  meshPrimitives->indices.clear();

  auto* scene = getScene();
  if (!scene)return;

  // Skip when the host window didn't draw this frame — fb is either
  // unsized (Viewport3D just constructed; draw() hasn't run yet) or
  // about-to-be-destroyed (host editor closed; defer-erase next frame).
  // Either way, running this pass would write to invalid GPU textures.
  if (!drewThisFrame) return;
  if (fb.getWidth() == 0 || fb.getHeight() == 0) return;

  // Bind this viewport's selection so component draw paths see the right
  // selection (e.g. compModel's selection-tint highlight).
  ViewportSelectionScope vpSelScope(getSelection());

  getSelection().sanitize(scene);

  camera.apply(uniGlobal);
  uniGlobal.screenSize = glm::vec2{(float)fb.getWidth(), (float)fb.getHeight()};
  renderScenePass(cmdBuff, renderScene, fb, uniGlobal, true);

  // PiP camera preview: re-render the scene through the selected camera.
  if (!showCameraPreview || fbPreview.getTexture() == nullptr) return;

  auto previewObj = scene->getObjectByUUID(previewCameraUUID);
  if (!previewObj) return;

  Project::Object* srcObj = previewObj.get();
  if (previewSrcUUID != previewCameraUUID && previewObj->isPrefabInstance()) {
    auto prefab = ctx.project->getAssets().getPrefabByUUID(previewObj->uuidPrefab.value);
    if (prefab) srcObj = &prefab->obj;
  }
  auto* cameraComp = getCameraComponent(*srcObj);
  if (!cameraComp) return;

  // PathFollow override: ride the spline at the inspector scrub distance.
  // The path sample is the follower's world pose; the Camera's authored
  // offset relative to the follower is composed back on so a rigged child
  // camera previews exactly as it will at runtime.
  glm::vec3 ovPos; glm::quat ovRot;
  bool haveOverride = false;
  if (previewPathFollow && previewFollowUUID) {
    if (auto followObj = scene->getObjectByUUID(previewFollowUUID)) {
      Project::Component::Path::SampleFrame sf{};
      if (Project::Component::PathFollow::previewFollowerFrame(*followObj, sf)) {
        glm::vec3 z = -glm::normalize(sf.fwd);
        glm::vec3 x = glm::cross(glm::normalize(sf.up), z);
        if (glm::length(x) < 1e-5f) x = glm::vec3{1, 0, 0};
        x = glm::normalize(x);
        glm::vec3 y = glm::cross(z, x);
        glm::mat4 mPath = glm::translate(glm::mat4(1.0f), sf.pos)
          * glm::mat4(glm::mat3(x, y, z));

        auto trsNoScale = [](Project::Object &o) {
          return glm::translate(glm::mat4(1.0f), o.pos.resolve(o.propOverrides))
               * glm::toMat4(glm::normalize(o.rot.resolve(o.propOverrides)));
        };
        glm::mat4 mFollower = trsNoScale(*followObj);
        glm::mat4 mCam      = trsNoScale(*previewObj);
        glm::mat4 mCamWorld = mPath * (glm::inverse(mFollower) * mCam);

        ovPos = glm::vec3(mCamWorld[3]);
        ovRot = glm::normalize(glm::quat_cast(glm::mat3(mCamWorld)));
        haveOverride = true;
      }
    }
  }

  if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "Camera Preview Pass");
  Project::Component::Camera::applyToGlobalUniforms(
    *previewObj, *cameraComp, previewUniGlobal,
    (float)fbPreview.getWidth(), (float)fbPreview.getHeight(),
    haveOverride ? &ovPos : nullptr,
    haveOverride ? &ovRot : nullptr
  );
  // Keep PiP billboards/sprites at the same world-scale as the main viewport.
  previewUniGlobal.spriteSize = uniGlobal.spriteSize;
  renderScenePass(cmdBuff, renderScene, fbPreview, previewUniGlobal, false);
  if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);
}

void Editor::Viewport3D::updateCameraPreviewState(const ImVec2 &currSize, Project::Scene *scene)
{
  showCameraPreview = false;
  previewCameraUUID = 0;
  previewSrcUUID = 0;
  previewScreenSize = {};
  previewPathFollow = false;
  previewFollowUUID = 0;

  if (!scene) return;

  // Walk the selection for the first object that has a Camera on it or any
  // descendant. For prefab instances only a root camera is supported (its
  // subtree isn't addressable as scene objects), matching prior behavior.
  Project::Object* srcObj = nullptr;
  Project::Component::Entry* cameraComp = nullptr;
  uint32_t transformUUID = 0; // scene object whose transform drives the PiP
  for (uint32_t selUUID : getSelection().all()) {
    auto selObj = scene->getObjectByUUID(selUUID);
    if (!selObj) continue;
    bool isPrefab = selObj->isPrefabInstance();
    Project::Object* candidate = selObj.get();
    if (isPrefab) {
      auto prefab = ctx.project->getAssets().getPrefabByUUID(selObj->uuidPrefab.value);
      if (prefab) candidate = &prefab->obj;
    }
    Project::Component::Entry* comp = nullptr;
    Project::Object* camOwner = findCameraInTree(*candidate, comp);
    if (!comp) continue;
    if (isPrefab && camOwner != candidate) continue; // deep prefab cam: skip

    srcObj = camOwner;
    cameraComp = comp;
    // Non-prefab child camera: its own scene transform drives the PiP.
    // Prefab/root: the instance transform (existing behavior).
    transformUUID = (!isPrefab) ? camOwner->uuid : selObj->uuid;
    // The follower is the selected object itself when it carries a
    // PathFollow that resolves a Path (checked lazily at render time).
    if (hasPathFollow(*selObj)) {
      previewPathFollow = true;
      previewFollowUUID = selObj->uuid;
    }
    break;
  }

  if (!cameraComp) return;

  // Fit preview into viewport while preserving the camera's aspect ratio.
  float previewMaxWidth = std::max(currSize.x * PREVIEW_SIZE_FACTOR, PREVIEW_MIN_WIDTH);
  previewMaxWidth = std::min(previewMaxWidth, std::max(currSize.x - PREVIEW_VIEWPORT_PADDING, PREVIEW_MIN_SIZE));

  float aspect = Project::Component::Camera::getAspectRatio(*srcObj, *cameraComp, PREVIEW_DEFAULT_ASPECT);
  aspect = std::max(aspect, PREVIEW_MIN_ASPECT);

  glm::vec2 previewSize{ previewMaxWidth, previewMaxWidth / aspect };

  float previewMaxHeight = std::max(currSize.y * PREVIEW_SIZE_FACTOR, PREVIEW_MIN_HEIGHT);
  previewMaxHeight = std::min(previewMaxHeight, std::max(currSize.y - PREVIEW_VIEWPORT_PADDING, PREVIEW_MIN_SIZE));
  if (previewSize.y > previewMaxHeight) {
    previewSize.y = previewMaxHeight;
    previewSize.x = previewSize.y * aspect;
  }

  if (previewSize.x < PREVIEW_MIN_SIZE || previewSize.y < PREVIEW_MIN_SIZE) return;

  showCameraPreview = true;
  previewCameraUUID = transformUUID;
  previewSrcUUID = srcObj->uuid;
  previewScreenSize = previewSize;

  glm::vec2 previewRenderSize = previewSize * ctx.prefs.renderFactorAA;
  fbPreview.setClearColor(scene->conf.clearColor.value);
  fbPreview.resize((int)previewRenderSize.x, (int)previewRenderSize.y);
}

void Editor::Viewport3D::drawCameraPreviewOverlay(const ImVec2 &currPos, const ImVec2 &currSize)
{
  if (!showCameraPreview || fbPreview.getTexture() == nullptr) return;

  auto* scene = getScene();
  if (!scene) return;
  auto previewObj = scene->getObjectByUUID(previewCameraUUID);
  if (!previewObj) return;

  ImVec2 framePadding = ImGui::GetStyle().WindowPadding;
  ImVec2 margin = framePadding;
  constexpr float labelH = 18.0f;

  ImVec2 framePos{
    currPos.x + currSize.x - previewScreenSize.x - margin.x - (framePadding.x * 2.0f),
    currPos.y + currSize.y - previewScreenSize.y - margin.y - (framePadding.y * 2.0f)
  };
  ImVec2 frameEnd{
    framePos.x + previewScreenSize.x + (framePadding.x * 2.0f),
    framePos.y + previewScreenSize.y + (framePadding.y * 2.0f)
  };
  ImVec2 imgPos{ framePos.x + framePadding.x, framePos.y + framePadding.y };
  ImVec2 imgEnd{ imgPos.x + previewScreenSize.x, imgPos.y + previewScreenSize.y };
  ImVec2 labelMin{ framePos.x, framePos.y - labelH };
  ImVec2 labelMax{ frameEnd.x, framePos.y };

  ImU32 borderCol = IM_COL32(0xFF, 0xFF, 0xFF, 0xC0);
  ImU32 labelBg   = IM_COL32(0x10, 0x10, 0x10, 0xD0);
  ImU32 labelFg   = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);

  auto drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(framePos, frameEnd, ImGui::GetColorU32(ImGuiCol_WindowBg), ImGui::GetStyle().WindowRounding);
  drawList->AddRectFilled(labelMin, labelMax, labelBg);
  drawList->AddImage(ImTextureID(fbPreview.getTexture()), imgPos, imgEnd);
  drawList->AddRect(labelMin, frameEnd, borderCol, 0.0f, 0, 1.5f);

  std::string label = std::string(ICON_MDI_VIDEO_VINTAGE " ") + previewObj->name;
  ImVec2 labelTextPos{ labelMin.x + 6.0f, labelMin.y + 2.0f };
  drawList->AddText(labelTextPos, labelFg, label.c_str());
}

void Editor::Viewport3D::onCopyPass(SDL_GPUCommandBuffer* cmdBuff, SDL_GPUCopyPass *copyPass) {
  //vertBuff->upload(*copyPass);

  if(!ctx.project)return;
  auto* scene = getScene();
  if (!scene)return;

  // Pairs with the drewThisFrame guard in onRenderPass.
  if (!drewThisFrame) return;
  if (fb.getWidth() == 0 || fb.getHeight() == 0) return;

  ViewportSelectionScope vpSelScope(getSelection());

  if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "Object Copy-Pass");

  auto &rootObj = scene->getRootObject();
  iterateObjects(rootObj, [&](Project::Object &obj, Project::Component::Entry *comp) {
    if(!comp)return;
    auto &def = Project::Component::TABLE[comp->id];
    if(def.funcDrawCopyPass) {
      def.funcDrawCopyPass(obj, *comp, *this, cmdBuff, copyPass);
    }
  });

  if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);
}

void Editor::Viewport3D::onPostRender(Renderer::Scene &renderScene) {
  // Pairs with the drewThisFrame guard in onRenderPass.
  if (!drewThisFrame) return;

  if (fb.getWidth() != 0 && fb.getHeight() != 0 && pickedObjID.isRequested()) {
    pickedObjID.setResult(fb.readObjectID(
      mousePosClick.x * ctx.prefs.renderFactorAA,
      mousePosClick.y * ctx.prefs.renderFactorAA
    ));
  }

  // Last callback of the frame for this viewport — consume the flag so
  // next frame's pre-draw callbacks see it as false unless draw() is
  // called again to set it true.
  drewThisFrame = false;
}

void Editor::Viewport3D::draw()
{
  auto &gizStyle = ImViewGuizmo::GetStyle();
  gizStyle.scale = 0.5f * ImGui::Theme::zoomFactor;
  gizStyle.circleRadius = 19.0f;
  gizStyle.labelSize = 1.9f / ImGui::Theme::zoomFactor;
  gizStyle.labelColor = IM_COL32(0,0,0,0xFF);

  camera.update();

  auto* scene = getScene();
  if (!scene)return;

  // SPBF64 fork: bind this viewport's selection for the duration of draw so
  // gizmo / picking / selection-marquee interactions all read+write the
  // bound Selection rather than ctx.mainSelection.
  ViewportSelectionScope vpSelScope(getSelection());

  ctx.scene->clearLights();
  auto &rootObj = scene->getRootObject();

  iterateObjects(rootObj, [&](Project::Object &obj, Project::Component::Entry *comp) {
    if(!comp)return;
    auto &def = Project::Component::TABLE[comp->id];
    if(def.funcUpdate)def.funcUpdate(obj, *comp);
  });

  fb.setClearColor(scene->conf.clearColor.value);

  if(pickedObjID.hasResult())
  {
    uint32_t newUUID = pickedObjID.consume();
    auto newObj = scene->getObjectByUUID(newUUID);
    if(newObj && !newObj->selectable) {
      newUUID = 0;
    }

    if (newUUID == 0) {
      if (!pickAdditive) {
        getSelection().clear();
      }
    } else {
      if (pickAdditive) {
        getSelection().toggle(newUUID);
      } else {
        getSelection().set(newUUID);
      }
    }
  }
  auto obj = scene->getObjectByUUID(getSelection().primary());

  float BAR_HEIGHT = 26_px;

  auto currSize = ImGui::GetContentRegionAvail();

  auto currPos = ImGui::GetWindowPos();
  if (currSize.x < 64_px)currSize.x = 64_px;
  if (currSize.y < 64_px)currSize.y = 64_px;
  currSize.y -= BAR_HEIGHT;

  currSize.x = floorf(currSize.x);
  currSize.y = floorf(currSize.y);

  // Since we can't use MSAA directly, just render at higher res here
  auto renderSize = currSize * ctx.prefs.renderFactorAA;

  fb.resize((int)renderSize.x, (int)renderSize.y);
  camera.screenSize = {renderSize.x, renderSize.y};

  // Resolve PiP camera + size the preview framebuffer (selection-gated).
  updateCameraPreviewState(currSize, scene);

  auto &io = ImGui::GetIO();
  float deltaTime = io.DeltaTime;

  ImVec2 gizPos{currPos.x + currSize.x - 50_px, currPos.y + 104_px};

  // mouse pos
  ImVec2 screenPos = ImGui::GetCursorScreenPos();
  if (cameraDragActive) {
    // SDL relative mode is on (enabled below at drag start). SDL hides the
    // cursor and reports raw motion deltas via GetRelativeMouseState. We
    // skip the io.WantSetMousePos warp dance — that fights multi-viewport
    // because the warp coords/window-target ambiguity makes the cursor
    // jitter and the camera barely move.
    float dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);
    mousePos.x += dx;
    mousePos.y += dy;
  } else {
    mousePos = {ImGui::GetMousePos().x, ImGui::GetMousePos().y};
    mousePos.x -= screenPos.x;
    mousePos.y -= vpOffsetY;
  }

  if (!ctx.prefs.mouseWheelModifiesSpeed) moveSpeedModifier = 1.0f;
  float moveSpeed = (ctx.prefs.moveSpeed * moveSpeedModifier) * deltaTime;

  bool mouseHeldLeft = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool mouseHeldRight = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  bool mouseHeldMiddle = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  bool newMouseDown = mouseHeldLeft || mouseHeldMiddle || mouseHeldRight;
  bool isCameraFlying = false;
  bool isAltDown = ImGui::GetIO().KeyAlt;
  bool isShiftDown = ImGui::GetIO().KeyShift;
  if(isShiftDown)moveSpeed *= 4.0f;

  bool hasSelection = !getSelection().all().empty();
  bool overGizmo = hasSelection && ImGuizmo::IsOver();

  bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

  if (!overGizmo && isMouseHover && leftClicked && !isAltDown && !overRotGizmo) {
    selectionPending = true;
    selectionDragging = false;
    selectionStart = mousePos;
    selectionEnd = mousePos;
  }

  if (selectionPending && leftDown) {
    selectionEnd = mousePos;
    if (!selectionDragging) {
      glm::vec2 delta = selectionEnd - selectionStart;
      if (glm::length(delta) > 4.0f) {
        selectionDragging = true;
        pickAdditive = ImGui::GetIO().KeyCtrl;
      }
    }
  }

  if (selectionPending && leftReleased) {
    bool additiveSelect = ImGui::GetIO().KeyCtrl;
    if (selectionDragging) {
      glm::vec2 rectMin = glm::min(selectionStart, selectionEnd);
      glm::vec2 rectMax = glm::max(selectionStart, selectionEnd);
      glm::vec2 viewportSize{currSize.x, currSize.y};
      rectMin = glm::clamp(rectMin, glm::vec2{0,0}, viewportSize);
      rectMax = glm::clamp(rectMax, glm::vec2{0,0}, viewportSize);

      if (!additiveSelect) {
        getSelection().clear();
      }

      auto &rootObj = scene->getRootObject();
      glm::vec4 viewport{0.0f, 0.0f, currSize.x, currSize.y};
      iterateObjects(rootObj, [&](Project::Object &objIter, Project::Component::Entry *comp) {
        if (comp) return;
        if (!objIter.selectable) return;

        glm::vec3 worldPos = objIter.pos.resolve(objIter.propOverrides);
        glm::vec3 proj = glm::project(worldPos, uniGlobal.cameraMat, uniGlobal.projMat, viewport);
        if (proj.z < 0.0f || proj.z > 1.0f) return;

        glm::vec2 screenPos{proj.x, currSize.y - proj.y};
        if (screenPos.x >= rectMin.x && screenPos.x <= rectMax.x
            && screenPos.y >= rectMin.y && screenPos.y <= rectMax.y) {
          getSelection().add(objIter.uuid);
        }
      });
    } else {
      pickedObjID.request();
      mousePosClick = mousePos;
      pickAdditive = additiveSelect;
    }
    selectionPending = false;
    selectionDragging = false;
  }

  if(isMouseHover)
  {
    ImGui::SetMouseCursor(
      cameraDragActive ? ImGuiMouseCursor_None : ImGuiMouseCursor_Arrow
    );
  }

  if(!ImGui::GetIO().WantTextInput)
  {
    if(ImGui::IsKeyPressed(ctx.prefs.keymap.toggleOrtho))
    {
      camera.isOrtho = !camera.isOrtho;
    }

    // Handle object deletion when Delete is pressed while the viewport is focused and an object is selected
    bool deletedSelection = false;
    if (ImGui::IsWindowFocused() && obj && ImGui::IsKeyPressed(ctx.prefs.keymap.deleteObject)) {
      UndoRedo::getHistory().markChanged("Delete Object");
      if (Editor::SelectionUtils::deleteSelectedObjects(*scene, getSelection())) {
        deletedSelection = true;
      }
      obj = nullptr;
    }

    // Right-click only counts as "flying" for THIS viewport when the cursor
    // is actually over it (or a drag is already active from this one). Without
    // this gate, right-clicking a panel elsewhere (e.g. a function row in the
    // PrefabEditor) makes every Viewport3D in the frame enter the wheel-input
    // block below and steal scroll/keyboard input intended for the row.
    isCameraFlying = mouseHeldRight && (isMouseHover || cameraDragActive);

    if (deletedSelection) {
      hasSelection = false;
    }

    if (newMouseDown) {
      glm::vec3 moveDir = {0,0,0};
      if (ImGui::IsKeyDown(ctx.prefs.keymap.moveForward))moveDir.z = -moveSpeed;
      if (ImGui::IsKeyDown(ctx.prefs.keymap.moveBack))moveDir.z = moveSpeed;
      if (ImGui::IsKeyDown(ctx.prefs.keymap.moveLeft))moveDir.x = -moveSpeed;
      if (ImGui::IsKeyDown(ctx.prefs.keymap.moveRight))moveDir.x = moveSpeed;
      if (ImGui::IsKeyDown(ctx.prefs.keymap.moveDown))moveDir.y = -moveSpeed;
      if (ImGui::IsKeyDown(ctx.prefs.keymap.moveUp))moveDir.y = moveSpeed;

      if(moveDir != glm::vec3{0,0,0}) {
        camera.velocity = camera.rot * moveDir;
      }
    } else {
      if(!ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
      {
        if (ImGui::IsKeyDown(ctx.prefs.keymap.gizmoTranslate))gizmoOp = 0;
        if (ImGui::IsKeyDown(ctx.prefs.keymap.gizmoRotate))gizmoOp = 1;
        if (ImGui::IsKeyDown(ctx.prefs.keymap.gizmoScale))gizmoOp = 2;
        if (ImGui::IsKeyPressed(ctx.prefs.keymap.focusObject))camera.focusSelection(*scene, getSelection());
      }
    }
  }

  if ((isMouseHover || isCameraFlying) && !overRotGizmo) {
    //multitouch trackpads don't generate touch or pinch events on windows
    //instead, we have to rely on the fact that trackpads move in fractional amounts
    glm::vec2 wheel = glm::vec2(io.MouseWheelH, io.MouseWheel);
    bool usesWheel = wheel != glm::vec2{0,0};
    
    if(usesWheel)
    {
      // We override the normal mouse wheel functionality if the preference is set + mouse is held
      // (...a more robust handling of editor state would probably also help with controlling parts of the 
      // viewport while the mouse is moving out of the window's focus)
      if(ctx.prefs.mouseWheelModifiesSpeed && mouseHeldRight) {
        moveSpeedModifier = std::clamp(moveSpeedModifier + (wheel.y * 0.125f), 0.125f, 4.0f);
      } else {
        if (std::fmod(std::abs(wheel.x), 1.0f) == 0 && std::fmod(std::abs(wheel.y), 1.0f) == 0) {
        //actual wheel or pinch gesture
        float wheelSpeed = (isShiftDown ? 4.0f : 1.0f) * ctx.prefs.zoomSpeed;
        camera.zoomSpeed += wheel.y * wheelSpeed;
        } else {
          if (ctx.prefs.invertWheelY) wheel.y *= -1;
          //two finger swipe on trackpad
          if (isShiftDown) {
            camera.moveDelta(wheel * ctx.prefs.panSpeed);
          } else {
            camera.orbitDelta(wheel * ctx.prefs.lookSpeed);
          }
        }
      }
    }

    // Drag-start MUST require this viewport to actually be hovered. mouseHeld*
    // is global state, so without this gate every Viewport3D in the frame
    // (main 3D-Viewport + prefab editor's nested viewport) would race to
    // claim SDL_GetRelativeMouseState — the first one to drain wins, the
    // others see zero delta and stay still. That's why right-click rotation
    // worked in scene viewport but not prefab: scene drew first and stole
    // the deltas. Middle-click bypassed this because isCameraFlying gated
    // only on right; the inactive viewport entered the block via
    // isCameraFlying for right-click but had isMouseHover==false here.
    if(!isMouseDown && newMouseDown && isMouseHover) {
      mousePosStart = mousePos;
      bool wantCapture = (isAltDown && mouseHeldLeft) || mouseHeldMiddle || mouseHeldRight;
      if (wantCapture && !cameraDragActive) {
        ImVec2 absPos = ImGui::GetMousePos();
        cursorLockPos = {absPos.x, absPos.y};
        // Drain SDL's accumulated relative motion so the first per-frame read
        // doesn't include any cursor movement from before the drag started.
        SDL_GetRelativeMouseState(nullptr, nullptr);
        // Multi-viewport-safe drag: enable SDL relative mouse mode on the
        // SDL window the cursor is currently over (which may be a child
        // platform viewport when this Viewport3D belongs to an asset editor
        // floating in its own OS window). SDL hides the cursor and gives us
        // raw motion deltas — no OS-cursor warp needed.
        SDL_Window* dragWin = nullptr;
        if (auto *vp = ImGui::GetWindowViewport()) {
          dragWin = (SDL_Window*)vp->PlatformHandle;
        }
        if (!dragWin) dragWin = ctx.window;
        SDL_SetWindowRelativeMouseMode(dragWin, true);
        cameraDragWindow = dragWin;
        cameraDragActive = true;
      }
    }
    isMouseDown = newMouseDown;
  }

  currPos = ImGui::GetCursorPos();

  //ImGui::Text("Viewport: %f | %f | %08X", mousePos.x, mousePos.y, ctx.mainSelection.primary());

  constexpr const char* const GIZMO_LABELS[3] = {ICON_MDI_CURSOR_MOVE, ICON_MDI_ROTATE_360, ICON_MDI_ARROW_EXPAND};
  constexpr const char* const GIZMO_TOOLTIPS[3] = {"Translate", "Rotate", "Scale"};
  for (int i=0; i<3; ++i) {
    if (ConnectedToggleButton(
      GIZMO_LABELS[i],
      gizmoOp == i,
      i == 0, i == 2,
      ImVec2(32_px,24_px)
    )) {
      gizmoOp = i;
    }
    ImGui::SetItemTooltip("%s", GIZMO_TOOLTIPS[i]);
  }

  ImGui::SameLine();

  if (ConnectedToggleButton(ICON_MDI_WEB, isTransWorld, true, true, ImVec2(32_px,24_px))) {
    isTransWorld = !isTransWorld;
  }
  ImGui::SetItemTooltip("Show %s Space", isTransWorld ? "Local" : "World");

  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12_px);

  if(ConnectedToggleButton(ICON_MDI_GRID, showGrid, true, true, ImVec2(32_px, 24_px))) {
    showGrid = !showGrid;
  }
  ImGui::SetItemTooltip("%s Grid", showGrid ? "Hide" : "Show");

  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 4_px);
  if(ConnectedToggleButton(ICON_MDI_LANDSLIDE_OUTLINE, showCollMesh, true, true, ImVec2(32_px, 24_px))) {
    showCollMesh = !showCollMesh;
  }
  ImGui::SetItemTooltip("%s Collision Mesh", showCollMesh ? "Hide" : "Show");

  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 4_px);
  if(ConnectedToggleButton(ICON_MDI_CYLINDER, showCollObj, true, true, ImVec2(32_px,24_px))) {
    showCollObj = !showCollObj;
  }
  ImGui::SetItemTooltip("%s Collision Bodies", showCollObj ? "Hide" : "Show");

  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12_px);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3_px);
  ImGui::Text("Cam Speed: %.2fx", moveSpeedModifier);

  ImGui::SetCursorPosY(currPos.y + BAR_HEIGHT);

  auto dragDelta = mousePos - mousePosStart;
  if (isMouseDown) {
    ImGui::ClearActiveID();
    if (isAltDown && mouseHeldLeft) {
      camera.stopMoveDelta();
      camera.orbitDelta(dragDelta);
    } else if (mouseHeldMiddle) {
      camera.stopRotateDelta();
      camera.moveDelta(-dragDelta * 3.0f);
    } else if (mouseHeldRight) {
      camera.stopMoveDelta();
      camera.lookDelta(dragDelta);
    }
  } else {
    camera.stopRotateDelta();
    camera.stopMoveDelta();
    if (cameraDragActive && cameraDragWindow) {
      // Restore normal cursor; cursor reappears at its pre-drag screen pos.
      SDL_SetWindowRelativeMouseMode((SDL_Window*)cameraDragWindow, false);
      cameraDragWindow = nullptr;
    }
    cameraDragActive = false;
    mousePosStart = mousePos = {0,0};
  }
  if (!newMouseDown)isMouseDown = false;

  currPos = ImGui::GetCursorScreenPos();
  currPos.x = floorf(currPos.x);
  currPos.y = floorf(currPos.y);
  ImGui::SetCursorScreenPos(currPos);

  vpOffsetY = currPos.y;

  auto tex = fb.getTexture();
  ImGui::Image(ImTextureID(tex), {
    (float)fb.getWidth() / ctx.prefs.renderFactorAA,
    (float)fb.getHeight() / ctx.prefs.renderFactorAA
  });

  // Capture hover + drag-drop target against the viewport image NOW, before
  // any per-component overlay submits its own ImGui items (e.g. Path's
  // per-control-point InvisibleButtons). IsItemHovered / BeginDragDropTarget
  // both reference the last-submitted item, so deferring this until after
  // overlays would steal hover/drop from the viewport and break right-click
  // camera fly while a Path is selected for authoring.
  isMouseHover = ImGui::IsItemHovered();

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET"))
    {
      uint64_t prefabUUID = *((uint64_t*)payload->Data);
      auto prefab = ctx.project->getAssets().getPrefabByUUID(prefabUUID);
      if(prefab) {
        UndoRedo::getHistory().markChanged("Add Prefab");
        auto newObj = scene->addPrefabInstance(prefabUUID);
        if (newObj) {
          // place in front of camera view
          glm::vec3 camForward = camera.rot * glm::vec3{0,0,-1};
          glm::vec3 camPos = camera.pos;
          newObj->pos.resolve(newObj->propOverrides) = camPos + camForward * 150.0f;

          getSelection().set(newObj->uuid);
        }
      }
    }
    ImGui::EndDragDropTarget();
  }

  // Picture-in-Picture camera preview thumbnail (bottom-right). Drawn before
  // the per-component overlays so SpriteBillboard/etc icons sit on top of it
  // if they happen to project into the corner.
  drawCameraPreviewOverlay(currPos, currSize);

  // SPBF64 fork: per-component screen-space overlays drawn after framebuffer.
  // Lets components like SpriteBillboard render their actual texture as a
  // billboard preview at the projected world position.
  {
    ImDrawList *overlayList = ImGui::GetWindowDrawList();
    glm::vec4 vpRect{currPos.x, currPos.y, currSize.x, currSize.y};
    iterateObjects(rootObj, [&](Project::Object &objIter, Project::Component::Entry *comp) {
      if (!comp) return;
      if (comp->id < 0 || (size_t)comp->id >= Project::Component::TABLE.size()) return;
      auto &def = Project::Component::TABLE[comp->id];
      if (!def.funcDrawOverlay) return;
      def.funcDrawOverlay(objIter, *comp, *this, overlayList,
                          uniGlobal.cameraMat, uniGlobal.projMat, vpRect);
    });
  }

  if (selectionDragging) {
    glm::vec2 rectMin = glm::min(selectionStart, selectionEnd);
    glm::vec2 rectMax = glm::max(selectionStart, selectionEnd);
    glm::vec2 viewportSize{currSize.x, currSize.y};

    rectMin = glm::clamp(rectMin, glm::vec2{0,0}, viewportSize);
    rectMax = glm::clamp(rectMax, glm::vec2{0,0}, viewportSize);

    ImVec2 rectStartScreen{currPos.x + rectMin.x, currPos.y + rectMin.y};
    ImVec2 rectEndScreen{currPos.x + rectMax.x, currPos.y + rectMax.y};
    auto drawList = ImGui::GetWindowDrawList();
    ImU32 fillCol = ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.15f);
    ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.85f);
    drawList->AddRectFilled(rectStartScreen, rectEndScreen, fillCol);
    drawList->AddRect(rectStartScreen, rectEndScreen, borderCol, 0.0f, 0, 1.5f);
  }

  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  ImGuizmo::SetDrawlist(draw_list);
  ImGuizmo::SetRect(currPos.x, currPos.y, currSize.x, currSize.y);

  if (hasSelection) {
    auto selectedObjects = Editor::SelectionUtils::collectSelectedObjects(*scene, getSelection());
    if (!selectedObjects.empty()) {
      obj = scene->getObjectByUUID(selectedObjects.back()->uuid);

      glm::mat4 gizmoMat{};
      glm::vec3 skew{0,0,0};
      glm::vec4 persp{0,0,0,1};

      bool isMultiSelect = selectedObjects.size() > 1;
      bool isOverride = false;

      glm::vec3 center{0.0f, 0.0f, 0.0f};
      if (!isMultiSelect) {
        glm::vec3 scale = obj->scale.resolve(obj->propOverrides, &isOverride);
        for (int i = 0; i < 3; i++) if (glm::abs(scale[i]) < 0.0001f) scale[i] = 0.0001f;
        gizmoMat = glm::recompose(
          scale,
          obj->rot.resolve(obj->propOverrides),
          obj->pos.resolve(obj->propOverrides),
          skew, persp);
      } else {
        for (auto *selObj : selectedObjects) {
          center += selObj->pos.resolve(selObj->propOverrides);
        }
        center /= (float)selectedObjects.size();
        gizmoMat = glm::recompose(
          glm::vec3{1.0f},
          glm::quat{1,0,0,0},
          center,
          skew,
          persp
        );
      }

      glm::mat4 oldGizmoMat = gizmoMat;

      glm::vec3 snap(10.0f);
      if (gizmoOp == 1) { // rotate
        snap = glm::vec3(90.0f / 4.0f);
      } else if (gizmoOp == 2) { // scale
        snap = glm::vec3(0.125f);
      }
      bool isSnap = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
      bool isOnlySelf = ImGui::IsKeyDown(ImGuiKey_LeftShift);

      // snap object to absolute grid
      if(ImGui::IsKeyDown(ImGuiKey_LeftShift) && ImGui::IsKeyPressed(ctx.prefs.keymap.snapObject))
      {
        glm::vec3 pos = obj->pos.resolve(obj->propOverrides);
        pos.x = std::round(pos.x / snap.x) * snap.x;
        pos.y = std::round(pos.y / snap.y) * snap.y;
        pos.z = std::round(pos.z / snap.z) * snap.z;
        obj->pos.resolve(obj->propOverrides) = pos;
      }

      if(ImGuizmo::Manipulate(
        glm::value_ptr(uniGlobal.cameraMat),
        glm::value_ptr(uniGlobal.projMat),
        GIZMO_OPS[gizmoOp],
        isTransWorld ? ImGuizmo::MODE::WORLD : ImGuizmo::MODE::LOCAL,
        glm::value_ptr(gizmoMat),
        nullptr,
        isSnap ? glm::value_ptr(snap) : nullptr
      )) {
        gizmoTransformActive = true;

        if (!isMultiSelect) {
          if(!obj->uuidPrefab.value || isOverride)
          {
            std::unordered_map<uint64_t, glm::vec3> relPosMap{};
            if(!isOnlySelf)
            {
              auto oldObjMat = glm::recompose(
                obj->scale.resolve(obj->propOverrides),
                obj->rot.resolve(obj->propOverrides),
                obj->pos.resolve(obj->propOverrides),
                skew, persp);

              for(auto& child : obj->children)
              {
                relPosMap[child->uuid] = glm::inverse(oldObjMat) * glm::vec4(
                  child->pos.resolve(child->propOverrides), 1.0f
                );
              }
            }

            glm::decompose(
              gizmoMat,
              obj->scale.resolve(obj->propOverrides),
              obj->rot.resolve(obj->propOverrides),
              obj->pos.resolve(obj->propOverrides),
              skew, persp
            );

            if(!isOnlySelf)
            {
              applyDeltaToChildren(*obj, relPosMap, gizmoMat);
            }
          }
        } else {
          auto deltaMat = gizmoMat * glm::inverse(oldGizmoMat);

          if (gizmoOp == 2) {
            glm::vec3 gizScaleOld{1.0f};
            glm::vec3 gizScaleNew{1.0f};
            glm::vec3 gizPosOld{0.0f};
            glm::vec3 gizPosNew{0.0f};
            glm::quat gizRotOld{};
            glm::quat gizRotNew{};
            glm::vec3 gizSkew{0.0f};
            glm::vec4 gizPersp{0.0f, 0.0f, 0.0f, 1.0f};

            glm::decompose(oldGizmoMat, gizScaleOld, gizRotOld, gizPosOld, gizSkew, gizPersp);
            glm::decompose(gizmoMat, gizScaleNew, gizRotNew, gizPosNew, gizSkew, gizPersp);

            auto safeDiv = [](float a, float b) {
              return (std::abs(b) > 0.000001f) ? (a / b) : 1.0f;
            };
            glm::vec3 scaleDelta{
              safeDiv(gizScaleNew.x, gizScaleOld.x),
              safeDiv(gizScaleNew.y, gizScaleOld.y),
              safeDiv(gizScaleNew.z, gizScaleOld.z)
            };

            for (auto *selObj : selectedObjects) {
              if (selObj->isPrefabInstance() && !selObj->isPrefabEdit) {
                ensurePropertyOverride(selObj, selObj->pos);
                ensurePropertyOverride(selObj, selObj->rot);
                ensurePropertyOverride(selObj, selObj->scale);
              }

              auto &objPos = selObj->pos.resolve(selObj->propOverrides);
              auto &objScale = selObj->scale.resolve(selObj->propOverrides);
              auto &objRot = selObj->rot.resolve(selObj->propOverrides);

              glm::vec3 oldPos = objPos;
              glm::vec3 oldScale = objScale;
              glm::quat oldRot = objRot;

              std::unordered_map<uint64_t, glm::vec3> relPosMap{};
              if(!isOnlySelf)
              {
                auto oldObjMat = glm::recompose(oldScale, oldRot, oldPos, skew, persp);

                for(auto& child : selObj->children)
                {
                  relPosMap[child->uuid] = glm::inverse(oldObjMat) * glm::vec4(
                    child->pos.resolve(child->propOverrides), 1.0f
                  );
                }
              }

              objPos = center + ((oldPos - center) * scaleDelta);
              objScale = oldScale * scaleDelta;

              if(!isOnlySelf)
              {
                auto newObjMat = glm::recompose(objScale, objRot, objPos, skew, persp);
                applyDeltaToChildren(*selObj, relPosMap, newObjMat);
              }
            }
          } else {
            for (auto *selObj : selectedObjects) {
              if (selObj->isPrefabInstance() && !selObj->isPrefabEdit) {
                ensurePropertyOverride(selObj, selObj->pos);
                ensurePropertyOverride(selObj, selObj->rot);
                ensurePropertyOverride(selObj, selObj->scale);
              }

              std::unordered_map<uint64_t, glm::vec3> relPosMap{};
              if(!isOnlySelf)
              {
                auto oldObjMat = glm::recompose(
                  selObj->scale.resolve(selObj->propOverrides),
                  selObj->rot.resolve(selObj->propOverrides),
                  selObj->pos.resolve(selObj->propOverrides),
                  skew, persp);

                for(auto& child : selObj->children)
                {
                  relPosMap[child->uuid] = glm::inverse(oldObjMat) * glm::vec4(
                    child->pos.resolve(child->propOverrides), 1.0f
                  );
                }
              }

              auto oldObjMat = glm::recompose(
                selObj->scale.resolve(selObj->propOverrides),
                selObj->rot.resolve(selObj->propOverrides),
                selObj->pos.resolve(selObj->propOverrides),
                skew, persp);
              auto newObjMat = deltaMat * oldObjMat;

              glm::decompose(
                newObjMat,
                selObj->scale.resolve(selObj->propOverrides),
                selObj->rot.resolve(selObj->propOverrides),
                selObj->pos.resolve(selObj->propOverrides),
                skew, persp
              );

              if(!isOnlySelf) {
                applyDeltaToChildren(*selObj, relPosMap, newObjMat);
              }
            }
          }
        }
      }
    }
  }

  // If the gizmo was active but is no longer being used, end the transform snapshot
  if (gizmoTransformActive && (!ImGuizmo::IsUsing() || !obj)) {
    UndoRedo::getHistory().markChanged("Transform Object");
    gizmoTransformActive = false;
  }

  glm::vec3 posOffset = camera.pos - camera.pivot;
  float camDist = glm::length(posOffset);
  if (ImViewGuizmo::Rotate(posOffset, camera.rot, gizPos, camDist)) {
    camera.pos = camera.pivot + posOffset;
  }
  overRotGizmo = ImViewGuizmo::IsOver();

  // Tell the deferred render-pass / copy-pass / post-render callbacks they
  // should run this frame. Reset by onPostRender after the frame's GPU
  // work is submitted.
  drewThisFrame = true;
}
