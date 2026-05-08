/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "viewport3D.h"

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
  constinit uint32_t nextPassId{0};

  constexpr ImGuizmo::OPERATION GIZMO_OPS[3] {
    ImGuizmo::OPERATION::TRANSLATE,
    ImGuizmo::OPERATION::ROTATE,
    ImGuizmo::OPERATION::SCALE
  };
  constinit bool isTransWorld = true;
  constinit bool overRotGizmo = false;

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
      if(ctx.mainSelection.isSelected(child->uuid))continue;

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
  ctx.scene->removeRenderPass(passId);
  ctx.scene->removeCopyPass(passId);
  ctx.scene->removePostRenderCallback(passId);

  if(--spritesRefCount == 0) {
    sprites = nullptr;
  }
}

bool Editor::Viewport3D::alignFocusedObjectToCamera()
{
  auto scene = ctx.project ? ctx.project->getScenes().getLoadedScene() : nullptr;
  // No scene loaded or no object selected --> Abort
  if (!scene || ctx.mainSelection.primary() == 0)return false;

  auto obj = scene->getObjectByUUID(ctx.mainSelection.primary());
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

  auto scene = ctx.project->getScenes().getLoadedScene();
  if (!scene)return;

  ctx.mainSelection.sanitize(scene);

  SDL_GPURenderPass* renderPass3D = SDL_BeginGPURenderPass(
    cmdBuff, fb.getTargetInfo(), fb.getTargetInfoCount(), &fb.getDepthTargetInfo()
  );
  renderScene.getPipeline("n64").bind(renderPass3D);

  dummySkeleton.use(renderPass3D);

  camera.apply(uniGlobal);
  uniGlobal.screenSize = glm::vec2{(float)fb.getWidth(), (float)fb.getHeight()};
  SDL_PushGPUVertexUniformData(cmdBuff, 0, &uniGlobal, sizeof(uniGlobal));
  auto &rootObj = scene->getRootObject();

  if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "3D Objects");

  bool hadDraw = false;
  iterateObjects(rootObj, [&](Project::Object &obj, Project::Component::Entry *comp) {
    if(!comp)
    {
      if(!hadDraw) {
        glm::u8vec4 spriteCol{0xFF, 0xFF, 0xFF, 0xFF};
        if (ctx.mainSelection.isSelected(obj.uuid)) {
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

    if(def.funcDraw3D) {
      def.funcDraw3D(obj, *comp, *this, cmdBuff, renderPass3D);
      hadDraw = true;
    }
  });

  iterateObjects(rootObj, [&](Project::Object &obj, Project::Component::Entry *comp) {
    if(!comp)return;
    auto &def = Project::Component::TABLE[comp->id];

    // @TODO: use flag in component
    if(!showCollMesh && comp->id == 4)return;
    if(!showCollObj && comp->id == 5)return;

    if(def.funcDrawPost3D) {
      def.funcDrawPost3D(obj, *comp, *this, cmdBuff, renderPass3D);
    }
  });

  if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);

  meshLines->recreate(renderScene);
  meshSprites->recreate(renderScene);
  meshBillboards->recreate(renderScene);
  meshPrimitives->recreate(renderScene);

  // SPBF64 fork: solid-shaded primitives. Drawn before lines so the line
  // gizmos (selection outlines) sit on top of the filled surface.
  if (!meshPrimitives->vertLines.empty()) {
    if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "3D Primitives");
    renderScene.getPipeline("primitive").bind(renderPass3D);
    objPrimitives.draw(renderPass3D, cmdBuff);
    if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);
  }

  if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "3D Lines");
  renderScene.getPipeline("lines").bind(renderPass3D);

  if(showGrid)objGrid.draw(renderPass3D, cmdBuff);
  objLines.draw(renderPass3D, cmdBuff);

  // hack to get thicker lines with AA, just draw again with a 1px offset in screen-space
  if(ctx.prefs.renderFactorAA > 1.0f) {
    auto oldMat = uniGlobal.projMat[2];
    uniGlobal.projMat[2][0] += 1.0f / uniGlobal.screenSize.x;
    uniGlobal.projMat[2][1] -= 1.0f / uniGlobal.screenSize.y;
    SDL_PushGPUVertexUniformData(cmdBuff, 0, &uniGlobal, sizeof(uniGlobal));

    if(showGrid)objGrid.draw(renderPass3D, cmdBuff);
    objLines.draw(renderPass3D, cmdBuff);

    uniGlobal.projMat[2] = oldMat;
    SDL_PushGPUVertexUniformData(cmdBuff, 0, &uniGlobal, sizeof(uniGlobal));
  }
  if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);

  if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "3D Sprites");

  renderScene.getPipeline("sprites").bind(renderPass3D);

  sprites->bind(renderPass3D);
  objSprites.draw(renderPass3D, cmdBuff);

  if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);

  // SPBF64 fork: textured billboard quads — one draw per submitted billboard
  // so each can bind its own texture/uniform.
  if (!submittedBillboards.empty()) {
    if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "3D Billboards");

    renderScene.getPipeline("billboard").bind(renderPass3D);

    // Re-push UniformGlobal in case the lines AA pass perturbed it
    SDL_PushGPUVertexUniformData(cmdBuff, 0, &uniGlobal, sizeof(uniGlobal));

    for (const auto &bb : submittedBillboards) {
      // Per-billboard uniform: size+pivot, uv rect, mode (worldPerPixel etc.)
      struct {
        glm::vec4 sizeAndPivot;
        glm::vec4 uvRect;
        glm::vec4 mode;
      } params{ bb.sizeAndPivot, bb.uvRect, bb.mode };
      SDL_PushGPUVertexUniformData(cmdBuff, 1, &params, sizeof(params));

      SDL_GPUTextureSamplerBinding binding{
        .texture = bb.texture,
        .sampler = nullptr,
      };
      // Use the editor's default linear sampler (set up in main.cpp).
      binding.sampler = texSamplerRepeat;
      SDL_BindGPUFragmentSamplers(renderPass3D, 0, &binding, 1);

      meshBillboards->draw(renderPass3D, bb.indexOffset, 6);
    }

    if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);
  }

  SDL_EndGPURenderPass(renderPass3D);

  // SPBF64 fork: Picture-in-Picture preview through the selected Comp::Camera.
  // Reuses the meshes/buffers populated above (primitives + billboards already
  // recreated, so they're safe to draw again with a different uniform). Models
  // and animated models are re-issued via funcDraw3D so they pick up the new
  // projection. Lines/sprites/grid are intentionally skipped — gizmos shouldn't
  // appear in what is conceptually the runtime camera's view.
  if (previewSpec.active && fbPreview.getTexture() != nullptr)
  {
    if(ctx.debugMode)SDL_PushGPUDebugGroup(cmdBuff, "Camera Preview Pass");

    SDL_GPURenderPass* previewPass = SDL_BeginGPURenderPass(
      cmdBuff, fbPreview.getTargetInfo(), fbPreview.getTargetInfoCount(), &fbPreview.getDepthTargetInfo()
    );

    // Build view + projection from the camera spec.
    float aspect = previewSpec.aspect;
    if (aspect <= 0.0f) {
      aspect = previewSpec.vpSize.y > 0
        ? (float)previewSpec.vpSize.x / (float)previewSpec.vpSize.y
        : 1.0f;
    }
    glm::vec3 forward = previewSpec.rot * glm::vec3{0,0,-1};
    glm::vec3 upDir   = previewSpec.rot * glm::vec3{0,1,0};
    uniGlobalPreview.projMat = glm::perspective(
      glm::radians(previewSpec.fov), aspect, previewSpec.nearD, previewSpec.farD
    );
    uniGlobalPreview.cameraMat = glm::lookAt(previewSpec.pos, previewSpec.pos + forward, upDir);
    uniGlobalPreview.screenSize = glm::vec2{(float)fbPreview.getWidth(), (float)fbPreview.getHeight()};
    uniGlobalPreview.spriteSize = uniGlobal.spriteSize;

    renderScene.getPipeline("n64").bind(previewPass);
    dummySkeleton.use(previewPass);
    SDL_PushGPUVertexUniformData(cmdBuff, 0, &uniGlobalPreview, sizeof(uniGlobalPreview));

    // Re-issue immediate-draw renderable components only. compModel / compAnimModel
    // do not push into the shared mesh buffers; they call obj3D.draw() against the
    // current render pass, so calling them again here just retargets to fbPreview.
    iterateObjects(rootObj, [&](Project::Object &obj, Project::Component::Entry *comp) {
      if (!comp) return;
      if (comp->id != 1 /* Model (Static) */ && comp->id != 10 /* Model (Animated) */) return;
      auto &def = Project::Component::TABLE[comp->id];
      if (def.funcDraw3D) def.funcDraw3D(obj, *comp, *this, cmdBuff, previewPass);
    });

    // Solid primitives — meshPrimitives is already uploaded from the main pass.
    if (!meshPrimitives->vertLines.empty()) {
      renderScene.getPipeline("primitive").bind(previewPass);
      objPrimitives.draw(previewPass, cmdBuff);
    }

    // Textured billboards — same indices as the main pass, just reapplied with
    // the preview uniform.
    if (!submittedBillboards.empty()) {
      renderScene.getPipeline("billboard").bind(previewPass);
      SDL_PushGPUVertexUniformData(cmdBuff, 0, &uniGlobalPreview, sizeof(uniGlobalPreview));
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
        SDL_BindGPUFragmentSamplers(previewPass, 0, &binding, 1);
        meshBillboards->draw(previewPass, bb.indexOffset, 6);
      }
    }

    SDL_EndGPURenderPass(previewPass);
    if(ctx.debugMode)SDL_PopGPUDebugGroup(cmdBuff);
  }
}

void Editor::Viewport3D::onCopyPass(SDL_GPUCommandBuffer* cmdBuff, SDL_GPUCopyPass *copyPass) {
  //vertBuff->upload(*copyPass);

  if(!ctx.project)return;
  auto scene = ctx.project->getScenes().getLoadedScene();
  if (!scene)return;

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
  if (pickedObjID.isRequested()) {
    pickedObjID.setResult(fb.readObjectID(
      mousePosClick.x * ctx.prefs.renderFactorAA,
      mousePosClick.y * ctx.prefs.renderFactorAA
    ));
  }
}

void Editor::Viewport3D::draw()
{
  auto &gizStyle = ImViewGuizmo::GetStyle();
  gizStyle.scale = 0.5f * ImGui::Theme::zoomFactor;
  gizStyle.circleRadius = 19.0f;
  gizStyle.labelSize = 1.9f / ImGui::Theme::zoomFactor;
  gizStyle.labelColor = IM_COL32(0,0,0,0xFF);

  camera.update();

  auto scene = ctx.project->getScenes().getLoadedScene();
  if (!scene)return;

  ctx.scene->clearLights();
  auto &rootObj = scene->getRootObject();

  iterateObjects(rootObj, [&](Project::Object &obj, Project::Component::Entry *comp) {
    if(!comp)return;
    auto &def = Project::Component::TABLE[comp->id];
    if(def.funcUpdate)def.funcUpdate(obj, *comp);
  });

  fb.setClearColor(scene->conf.clearColor.value);

  // SPBF64 fork: detect a selected Comp::Camera and capture its projection
  // params for the Picture-in-Picture preview pass. First selected camera wins.
  previewSpec.active = false;
  for (uint32_t selUUID : ctx.mainSelection.all()) {
    auto selObj = scene->getObjectByUUID(selUUID);
    if (!selObj) continue;
    auto srcObj = selObj.get();
    if (selObj->isPrefabInstance()) {
      auto prefab = ctx.project->getAssets().getPrefabByUUID(selObj->uuidPrefab.value);
      if (prefab) srcObj = &prefab->obj;
    }
    for (auto &comp : srcObj->components) {
      if (comp.id != 3 /* Camera */) continue;
      auto spec = Project::Component::Camera::extractSpec(*selObj, comp);
      previewSpec.active = true;
      previewSpec.pos = spec.pos;
      previewSpec.rot = spec.rot;
      previewSpec.fov = spec.fov;
      previewSpec.nearD = spec.nearD;
      previewSpec.farD = spec.farD;
      previewSpec.aspect = spec.aspect;
      previewSpec.vpSize = spec.vpSize;
      previewSpec.name = selObj->name;
      break;
    }
    if (previewSpec.active) break;
  }
  fbPreview.setClearColor(scene->conf.clearColor.value);

  if(pickedObjID.hasResult())
  {
    uint32_t newUUID = pickedObjID.consume();
    auto newObj = scene->getObjectByUUID(newUUID);
    if(newObj && !newObj->selectable) {
      newUUID = 0;
    }

    if (newUUID == 0) {
      if (!pickAdditive) {
        ctx.mainSelection.clear();
      }
    } else {
      if (pickAdditive) {
        ctx.mainSelection.toggle(newUUID);
      } else {
        ctx.mainSelection.set(newUUID);
      }
    }
  }
  auto obj = scene->getObjectByUUID(ctx.mainSelection.primary());

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

  // SPBF64 fork: size the PiP framebuffer to a sensible thumbnail (256px wide),
  // matching the camera's intrinsic aspect ratio. AA factor mirrors the main fb.
  if (previewSpec.active) {
    float pipAspect = previewSpec.aspect;
    if (pipAspect <= 0.0f) {
      pipAspect = previewSpec.vpSize.y > 0
        ? (float)previewSpec.vpSize.x / (float)previewSpec.vpSize.y
        : 16.0f / 9.0f;
    }
    constexpr float pipDisplayW = 256.0f;
    int pipW = (int)(pipDisplayW * ctx.prefs.renderFactorAA);
    int pipH = (int)((pipDisplayW / pipAspect) * ctx.prefs.renderFactorAA);
    if (pipW < 32) pipW = 32;
    if (pipH < 32) pipH = 32;
    fbPreview.resize(pipW, pipH);
  }

  auto &io = ImGui::GetIO();
  float deltaTime = io.DeltaTime;

  ImVec2 gizPos{currPos.x + currSize.x - 50_px, currPos.y + 104_px};

  // mouse pos
  ImVec2 screenPos = ImGui::GetCursorScreenPos();
  mousePos = {ImGui::GetMousePos().x, ImGui::GetMousePos().y};
  mousePos.x -= screenPos.x;
  mousePos.y -= vpOffsetY;

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

  bool hasSelection = !ctx.mainSelection.all().empty();
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
        ctx.mainSelection.clear();
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
          ctx.mainSelection.add(objIter.uuid);
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
      mouseHeldRight ? ImGuiMouseCursor_None : ImGuiMouseCursor_Arrow
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
      if (Editor::SelectionUtils::deleteSelectedObjects(*scene, ctx.mainSelection)) {
        deletedSelection = true;
      }
      obj = nullptr;
    }

    isCameraFlying = mouseHeldRight;

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
        if (ImGui::IsKeyPressed(ctx.prefs.keymap.focusObject))camera.focusSelection(ctx);
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

    if(!isMouseDown && newMouseDown) {
      mousePosStart = mousePos;
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

  // SPBF64 fork: Picture-in-Picture preview thumbnail in the bottom-right
  // corner of the viewport when a Camera component is selected. Drawn before
  // the per-component overlays so SpriteBillboard/etc icons sit on top of it
  // if they happen to project into the corner.
  if (previewSpec.active && fbPreview.getTexture() != nullptr) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float pipDisplayW = (float)fbPreview.getWidth() / ctx.prefs.renderFactorAA;
    float pipDisplayH = (float)fbPreview.getHeight() / ctx.prefs.renderFactorAA;
    constexpr float margin = 12.0f;
    constexpr float labelH = 18.0f;
    ImVec2 pipMin{
      currPos.x + currSize.x - pipDisplayW - margin,
      currPos.y + currSize.y - pipDisplayH - margin - labelH
    };
    ImVec2 pipMax{ pipMin.x + pipDisplayW, pipMin.y + pipDisplayH };
    ImVec2 labelMin{ pipMin.x, pipMin.y - labelH };
    ImVec2 labelMax{ pipMax.x, pipMin.y };

    ImU32 borderCol = IM_COL32(0xFF, 0xFF, 0xFF, 0xC0);
    ImU32 labelBg   = IM_COL32(0x10, 0x10, 0x10, 0xD0);
    ImU32 labelFg   = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);

    dl->AddRectFilled(labelMin, labelMax, labelBg);
    dl->AddImage(ImTextureID(fbPreview.getTexture()), pipMin, pipMax);
    dl->AddRect(labelMin, pipMax, borderCol, 0.0f, 0, 1.5f);

    std::string label = std::string(ICON_MDI_VIDEO_VINTAGE " ") + previewSpec.name;
    ImVec2 labelTextPos{ labelMin.x + 6.0f, labelMin.y + 2.0f };
    dl->AddText(labelTextPos, labelFg, label.c_str());
  }

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

          ctx.mainSelection.set(newObj->uuid);
        }
      }
    }
    ImGui::EndDragDropTarget();
  }

  isMouseHover = ImGui::IsItemHovered();

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
    auto selectedObjects = Editor::SelectionUtils::collectSelectedObjects(*scene, ctx.mainSelection);
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
}
