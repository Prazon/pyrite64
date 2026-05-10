/**
* SPBF64 fork: see header.
*/
#include "assetPreviewViewport.h"

#include "../../../../context.h"
#include "../../../../renderer/scene.h"
#include "../../../../project/assets/model3d.h"

#include "../../../../shader/defines.h"

#include "imgui.h"
#include "glm/gtc/matrix_transform.hpp"

namespace
{
  // Vertex positions in N64Mesh are stored as raw i16 values; the AABB on
  // Renderer::Mesh divides them by 65536 (see mesh.cpp). To position our
  // camera in the same space the mesh actually renders in, we multiply
  // back by 65536.
  constexpr float MESH_RENDER_SCALE = 65536.0f;

  constinit uint32_t nextPassId{1'000'000};
}

Editor::AssetPreviewViewport::AssetPreviewViewport()
  : dummySkeleton{ctx.gpu}
{
  passId = ++nextPassId;
  ctx.scene->addRenderPass(passId, [this](SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene) {
    onRenderPass(cmdBuff, renderScene);
  });
  ctx.scene->addCopyPass(passId, [this](SDL_GPUCommandBuffer* /*cmdBuff*/, SDL_GPUCopyPass *copyPass) {
    dummySkeleton.update(*copyPass);
  });

  fb.setClearColor({0.12f, 0.12f, 0.14f, 1.0f});
}

Editor::AssetPreviewViewport::~AssetPreviewViewport()
{
  if (ctx.scene) {
    ctx.scene->removeRenderPass(passId);
    ctx.scene->removeCopyPass(passId);
  }
}

void Editor::AssetPreviewViewport::setMesh(uint64_t assetUUID,
                                           std::shared_ptr<Renderer::N64Mesh> mesh,
                                           const Project::Assets::Model3D *model)
{
  meshPtr = std::move(mesh);
  modelRef = model;
  hasMesh = (meshPtr != nullptr) && (modelRef != nullptr);
  framed = false;
  lastAssetUUID = assetUUID;

  if (hasMesh) {
    renderObj.setMesh(meshPtr);
  } else {
    renderObj.removeMesh();
  }

  // Default to an isometric-ish 3/4 view so a fresh preview reads as a
  // sculpted shape rather than a flat silhouette. The user can orbit from
  // here; subsequent reframes (resize, mesh swap) preserve the rotation
  // unless the binding actually changes.
  glm::quat yaw   = glm::angleAxis(glm::radians( 35.0f), glm::vec3(0, 1, 0));
  glm::quat pitch = glm::angleAxis(glm::radians(-25.0f), glm::vec3(1, 0, 0));
  camera.rot     = yaw * pitch;
  camera.rotBase = camera.rot;
}

void Editor::AssetPreviewViewport::clear()
{
  meshPtr.reset();
  modelRef = nullptr;
  hasMesh = false;
  framed = false;
  renderObj.removeMesh();
}

void Editor::AssetPreviewViewport::onRenderPass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene)
{
  if (!drewThisFrame) return;
  drewThisFrame = false;
  if (fb.getTexture() == nullptr) return;
  if (!hasMesh || !meshPtr || !modelRef) return;

  // Lazy GPU upload — assets that have never been used in a scene won't have
  // their N64Mesh recreated yet.
  if (!meshPtr->isLoaded()) {
    meshPtr->recreate(renderScene);
    // First-frame upload happens in a deferred copy pass; the mesh won't
    // actually draw until next frame.
  }

  SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(
    cmdBuff, fb.getTargetInfo(), fb.getTargetInfoCount(), &fb.getDepthTargetInfo()
  );
  renderScene.getPipeline("n64").bind(rp);
  dummySkeleton.use(rp);

  camera.apply(uniGlobal);
  uniGlobal.screenSize = glm::vec2{(float)fb.getWidth(), (float)fb.getHeight()};
  SDL_PushGPUVertexUniformData(cmdBuff, 0, &uniGlobal, sizeof(uniGlobal));

  renderObj.uniform.modelMat = glm::identity<glm::mat4>();
  renderObj.setObjectID(0);
  // Asset previews don't depend on scene lighting — bypass it via the
  // shader's NO_LIGHT path so the model is visible regardless of whether the
  // currently-loaded scene has any lights placed. n64Mesh::draw propagates
  // this bit through `flagsGlobal` (see below).
  renderObj.uniform.mat.flags = T3D_FLAG_NO_LIGHT;

  // matInstance==nullptr is the asset-preview path through n64Mesh::draw —
  // it still does static texture lookup + material conversion from the
  // model's own materials, just without dynamic-slot overrides.
  static const std::vector<uint32_t> EMPTY_PARTS{};
  Renderer::N64Mesh::ObjectRef ref{
    .partsIndices = EMPTY_PARTS,
    .model = modelRef,
    .matInstance = nullptr,
    .obj = dummyObj,
    .isCollision = false,
  };
  renderObj.draw(rp, cmdBuff, &ref);

  SDL_EndGPURenderPass(rp);
}

void Editor::AssetPreviewViewport::setFraming(float margin, float minFrac, float maxFrac)
{
  frameMargin  = std::max(0.5f, margin);
  minZoomFrac  = std::max(0.0f, minFrac);
  maxZoomFrac  = std::max(0.0f, maxFrac);
  framed       = false; // reframe with the new margin next render
}

void Editor::AssetPreviewViewport::clampCameraDistance()
{
  if (minZoomFrac <= 0.0f && maxZoomFrac <= 0.0f) return;
  if (!hasMesh || !meshPtr || !meshPtr->isLoaded()) return;

  auto aabb = meshPtr->getAABB();
  glm::vec3 halfExt = aabb.getHalfExtend() * MESH_RENDER_SCALE;
  float radius = glm::length(halfExt);
  if (radius < 0.001f) return;

  glm::vec3 toCam = camera.pos - camera.pivot;
  float dist = glm::length(toCam);
  if (dist < 0.001f) return;

  float minDist = (minZoomFrac > 0.0f) ? radius * minZoomFrac : 0.0f;
  float maxDist = (maxZoomFrac > 0.0f) ? radius * maxZoomFrac : 1e30f;

  if (dist < minDist) {
    camera.pos = camera.pivot + (toCam / dist) * minDist;
    camera.zoomSpeed = 0.0f;
  } else if (dist > maxDist) {
    camera.pos = camera.pivot + (toCam / dist) * maxDist;
    camera.zoomSpeed = 0.0f;
  }
}

void Editor::AssetPreviewViewport::renderHeadless(ImVec2 size)
{
  if (size.x < 64.0f) size.x = 64.0f;
  if (size.y < 64.0f) size.y = 64.0f;
  size.x = floorf(size.x);
  size.y = floorf(size.y);

  fb.resize((uint32_t)size.x, (uint32_t)size.y);
  camera.screenSize = {size.x, size.y};

  if (hasMesh && meshPtr && !meshPtr->isLoaded()) {
    framed = false;
  }
  if (hasMesh && !framed && meshPtr && meshPtr->isLoaded()) {
    auto aabb = meshPtr->getAABB();
    glm::vec3 center  = aabb.getCenter()    * MESH_RENDER_SCALE;
    glm::vec3 halfExt = aabb.getHalfExtend() * MESH_RENDER_SCALE;
    float radius = glm::length(halfExt);
    if (radius > 0.001f) {
      camera.focus(center, std::max(radius * frameMargin, 100.0f));
      framed = true;
    }
  }
  clampCameraDistance();

  camera.update();
  clampCameraDistance();
  drewThisFrame = true;
}

SDL_GPUTexture* Editor::AssetPreviewViewport::getTexture() const
{
  return fb.getTexture();
}

bool Editor::AssetPreviewViewport::readPixels(std::vector<uint8_t> &out,
                                              uint32_t &outW, uint32_t &outH)
{
  outW = fb.getWidth();
  outH = fb.getHeight();
  if (outW == 0 || outH == 0) return false;
  return fb.readPixels(out);
}

void Editor::AssetPreviewViewport::draw(ImVec2 size)
{
  if (size.x < 64.0f) size.x = 64.0f;
  if (size.y < 64.0f) size.y = 64.0f;
  size.x = floorf(size.x);
  size.y = floorf(size.y);

  fb.resize((uint32_t)size.x, (uint32_t)size.y);
  camera.screenSize = {size.x, size.y};

  // If the mesh got rebuilt (e.g. after asset reload) the loaded flag flips
  // back to false; re-frame the camera once it's ready again.
  if (hasMesh && meshPtr && !meshPtr->isLoaded()) {
    framed = false;
  }

  // Auto-frame the first time we have a loaded mesh whose AABB is non-zero.
  if (hasMesh && !framed && meshPtr && meshPtr->isLoaded()) {
    auto aabb = meshPtr->getAABB();
    glm::vec3 center = aabb.getCenter() * MESH_RENDER_SCALE;
    glm::vec3 halfExt = aabb.getHalfExtend() * MESH_RENDER_SCALE;
    float radius = glm::length(halfExt);
    if (radius > 0.001f) {
      // FOV is 70°; place camera ~2.4 radii away so the bounding sphere
      // fits comfortably with some margin.
      camera.focus(center, std::max(radius * frameMargin, 100.0f));
      framed = true;
    }
  }
  clampCameraDistance();

  ImVec2 cursor = ImGui::GetCursorScreenPos();
  cursor.x = floorf(cursor.x);
  cursor.y = floorf(cursor.y);
  ImGui::SetCursorScreenPos(cursor);

  auto tex = fb.getTexture();
  if (tex) {
    ImGui::Image(ImTextureID(tex), size);
  } else {
    ImGui::Dummy(size);
  }

  bool hovered = ImGui::IsItemHovered();
  auto &io = ImGui::GetIO();

  // Dolly on wheel (only when hovering the preview).
  if (hovered && io.MouseWheel != 0.0f) {
    camera.zoomSpeed += io.MouseWheel * 250.0f;
  }

  // Orbit on LMB drag. Camera::orbitDelta wants the cumulative screen delta
  // since drag-start (it captures rotBase the first time isRotating is false
  // and keeps applying qx*rotBase*qy each frame). Feeding it per-frame
  // deltas resets rotBase every frame and makes the rotation snap back.
  bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if (hovered && lmb && !isOrbiting) {
    isOrbiting = true;
    mouseStart = {io.MousePos.x, io.MousePos.y};
  }
  if (isOrbiting) {
    if (lmb) {
      glm::vec2 dragDelta{io.MousePos.x - mouseStart.x,
                          io.MousePos.y - mouseStart.y};
      camera.orbitDelta(dragDelta);
    } else {
      camera.stopRotateDelta();
      isOrbiting = false;
    }
  }

  camera.update();
  clampCameraDistance();
  drewThisFrame = true;
}
