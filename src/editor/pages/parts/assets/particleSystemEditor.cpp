/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "particleSystemEditor.h"

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "imgui_internal.h"
#include "IconsMaterialDesignIcons.h"

#include "assetEditorDocking.h"
#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"
#include "../../../imgui/helper.h"
#include "../../editorScene.h"

namespace
{
  constexpr ImVec2 DEF_WIN_SIZE{900, 560};

  // Tiny LCG used to derive deterministic seed-driven offsets for noRng
  // mode. Mirrors the engine path's intent without committing to a specific
  // distribution shape.
  inline float frand01(uint32_t &state) {
    state = state * 1664525u + 1013904223u;
    return (float)(state >> 8) * (1.0f / (float)(1u << 24));
  }
  inline float frandRange(uint32_t &state, float a, float b) {
    return a + (b - a) * frand01(state);
  }

  ImU32 lerpColorU32(const ImVec4 &a, const ImVec4 &b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    ImVec4 c{
      a.x + (b.x - a.x) * t,
      a.y + (b.y - a.y) * t,
      a.z + (b.z - a.z) * t,
      a.w + (b.w - a.w) * t,
    };
    return ImGui::ColorConvertFloat4ToU32(c);
  }
}

Editor::ParticleSystemEditor::ParticleSystemEditor(uint64_t particleAssetUUID)
  : assetUUID(particleAssetUUID)
{
  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || !asset->particleAsset) return;
  working    = *asset->particleAsset;
  savedState = working.serialize();
  particles.reserve(working.maxParticles);
}

std::string Editor::ParticleSystemEditor::getName() const
{
  if (!ctx.project) return "Particles";
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  return asset ? asset->name : std::string{"Particles"};
}

bool Editor::ParticleSystemEditor::isDirty() const
{
  return working.serialize() != savedState;
}

void Editor::ParticleSystemEditor::resetSim()
{
  particles.clear();
  spawnAccum = 0.0f;
  simTime = 0.0f;
  bursted = false;
}

void Editor::ParticleSystemEditor::spawnOne()
{
  if (particles.size() >= working.maxParticles) return;

  uint32_t seed = (uint32_t)((uintptr_t)this ^ (uint32_t)(simTime * 1024.0f) ^ (uint32_t)particles.size());

  LiveParticle p{};
  p.age = 0.0f;
  p.lifetime  = frandRange(seed, working.lifetimeMin, working.lifetimeMax);
  p.startScale = frandRange(seed, working.startScaleMin, working.startScaleMax);
  p.seed = seed;

  // Shape origin in preview pixels relative to the preview center. The
  // device-side equivalent samples a 3D point and adds it to the Object
  // position; here we project onto the preview's XY plane for visual
  // intuition only.
  float ox = 0.0f, oy = 0.0f;
  switch (working.shape) {
    case Project::Assets::ParticleSystemAsset::SHAPE_POINT: break;
    case Project::Assets::ParticleSystemAsset::SHAPE_SPHERE: {
      float r = working.sphereRadius * frand01(seed);
      float a = frand01(seed) * 6.2831853f;
      ox = std::cos(a) * r;
      oy = std::sin(a) * r;
    } break;
    case Project::Assets::ParticleSystemAsset::SHAPE_BOX: {
      ox = (frand01(seed) * 2.0f - 1.0f) * working.boxExtents.x;
      oy = (frand01(seed) * 2.0f - 1.0f) * working.boxExtents.y;
    } break;
    case Project::Assets::ParticleSystemAsset::SHAPE_DISC: {
      float r = working.discRadius * std::sqrt(frand01(seed));
      float a = frand01(seed) * 6.2831853f;
      ox = std::cos(a) * r;
      oy = std::sin(a) * r;
    } break;
  }
  p.x = ox;
  p.y = oy;

  float speed = frandRange(seed, working.startVelSpeedMin, working.startVelSpeedMax);
  // Use only XY of startVelDir for the 2D preview. Z is informational here.
  float dirX = working.startVelDir.x;
  float dirY = working.startVelDir.y;
  float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
  if (dirLen > 1e-5f) { dirX /= dirLen; dirY /= dirLen; }
  // ImGui Y grows downward — invert so positive Y in asset reads as "up".
  p.vx = dirX * speed;
  p.vy = -dirY * speed;

  particles.push_back(p);
}

void Editor::ParticleSystemEditor::stepSim(float dt)
{
  if (paused || dt <= 0.0f) return;
  simTime += dt;

  bool emitterActive = working.loop || (simTime < working.duration);

  if (emitterActive) {
    if (!bursted && working.burstCount > 0) {
      for (uint32_t i = 0; i < working.burstCount; ++i) spawnOne();
      bursted = true;
    }
    if (working.spawnRate > 0.0f) {
      spawnAccum += dt * working.spawnRate;
      while (spawnAccum >= 1.0f) {
        spawnOne();
        spawnAccum -= 1.0f;
      }
    }
  }

  // Integrate. Gravity Y is flipped because preview Y is screen-down.
  float gx = working.gravity.x;
  float gy = -working.gravity.y;
  float dragScale = std::exp(-working.drag * dt);

  for (auto it = particles.begin(); it != particles.end(); ) {
    it->age += dt;
    if (it->age >= it->lifetime) {
      *it = particles.back();
      particles.pop_back();
      continue;
    }
    it->vx = it->vx * dragScale + gx * dt;
    it->vy = it->vy * dragScale + gy * dt;
    it->x += it->vx * dt;
    it->y += it->vy * dt;
    ++it;
  }
}

void Editor::ParticleSystemEditor::drawPreview(ImVec2 size)
{
  ImGui::BeginChild("##ptxPreviewSurface", size, ImGuiChildFlags_Borders);

  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 center{ origin.x + size.x * 0.5f, origin.y + size.y * 0.6f };
  ImDrawList *dl = ImGui::GetWindowDrawList();

  // Background grid for spatial reference
  ImU32 gridCol = IM_COL32(60, 60, 60, 120);
  for (float x = 0; x < size.x; x += 32.0f) {
    dl->AddLine({origin.x + x, origin.y}, {origin.x + x, origin.y + size.y}, gridCol);
  }
  for (float y = 0; y < size.y; y += 32.0f) {
    dl->AddLine({origin.x, origin.y + y}, {origin.x + size.x, origin.y + y}, gridCol);
  }
  // Emitter pos marker
  dl->AddCircle(center, 4.0f, IM_COL32(255, 200, 0, 200), 0, 1.5f);

  SDL_GPUTexture *spriteTex = nullptr;
  float texW = 16.0f, texH = 16.0f;
  if (working.spriteUUID != 0) {
    auto *e = ctx.project->getAssets().getEntryByUUID(working.spriteUUID);
    if (e && e->texture) {
      spriteTex = e->texture->getGPUTex();
      texW = (float)e->texture->getWidth();
      texH = (float)e->texture->getHeight();
    }
  }

  ImVec4 startCol{working.startColor.x, working.startColor.y, working.startColor.z, working.startColor.w};
  ImVec4 endCol  {working.endColor.x,   working.endColor.y,   working.endColor.z,   working.endColor.w};

  for (const auto &p : particles) {
    float t = (p.lifetime > 1e-5f) ? (p.age / p.lifetime) : 1.0f;
    float scale = p.startScale;
    if (working.sizeOverLife) scale *= (1.0f - t);
    float r = std::max(2.0f, 8.0f * scale);

    ImU32 col = working.colorOverLife
      ? lerpColorU32(startCol, endCol, t)
      : ImGui::ColorConvertFloat4ToU32(startCol);

    ImVec2 pos{center.x + p.x, center.y + p.y};
    if (spriteTex) {
      ImVec2 a{pos.x - r, pos.y - r};
      ImVec2 b{pos.x + r, pos.y + r};
      dl->AddImage(ImTextureID(spriteTex), a, b, {0,0}, {1,1}, col);
    } else {
      dl->AddCircleFilled(pos, r, col);
    }
  }
  (void)texW; (void)texH;

  // Particle count readout
  ImGui::SetCursorScreenPos({origin.x + 6.0f, origin.y + 4.0f});
  ImGui::TextDisabled("alive: %d / %u   t=%.2fs", (int)particles.size(), working.maxParticles, simTime);

  ImGui::EndChild();
}

void Editor::ParticleSystemEditor::drawInspector()
{
  auto &w = working;

  if (ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto &assets = ctx.project->getAssets();
    auto *current = (w.spriteUUID != 0) ? assets.getEntryByUUID(w.spriteUUID) : nullptr;
    const char *preview = current ? current->name.c_str() : "<None>";
    if (ImGui::BeginCombo("Sprite", preview)) {
      if (ImGui::Selectable("<None>", w.spriteUUID == 0)) w.spriteUUID = 0;
      for (const auto &e : assets.getTypeEntries(Project::FileType::IMAGE)) {
        bool sel = (e.conf.uuid == w.spriteUUID);
        if (ImGui::Selectable(e.name.c_str(), sel)) w.spriteUUID = e.conf.uuid;
      }
      ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET")) {
        uint64_t dropped = *(uint64_t*)payload->Data;
        auto *e = assets.getEntryByUUID(dropped);
        if (e && e->type == Project::FileType::IMAGE) w.spriteUUID = dropped;
      }
      ImGui::EndDragDropTarget();
    }

    int pt = (int)w.particleType;
    if (ImGui::Combo("Type", &pt, "Color RGBA S8\0Tex RGBA S8\0Color A S16\0Tex A S16\0")) {
      w.particleType = (Project::Assets::ParticleSystemAsset::ParticleType)pt;
    }
    ImGui::Checkbox("Rotating", &w.isRotating);
    ImGui::SameLine();
    ImGui::Checkbox("No RNG", &w.noRng);
    ImGui::SliderFloat("Anim FPS", &w.animFps, 0.0f, 60.0f, "%.1f");
  }

  if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen)) {
    int maxP = (int)w.maxParticles;
    if (ImGui::SliderInt("Max Particles", &maxP, 1, 1024)) {
      w.maxParticles = (uint32_t)maxP;
    }
    ImGui::DragFloat("Spawn Rate (/s)", &w.spawnRate, 0.5f, 0.0f, 2000.0f);
    int bc = (int)w.burstCount;
    if (ImGui::DragInt("Burst Count", &bc, 1.0f, 0, 1024)) w.burstCount = (uint32_t)bc;
    ImGui::Checkbox("Loop", &w.loop);
    if (!w.loop) {
      ImGui::SameLine();
      ImGui::DragFloat("Duration (s)", &w.duration, 0.05f, 0.0f, 60.0f);
    }
  }

  if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
    int sk = (int)w.shape;
    if (ImGui::Combo("Shape", &sk, "Point\0Sphere\0Box\0Disc\0")) {
      w.shape = (Project::Assets::ParticleSystemAsset::ShapeKind)sk;
    }
    switch (w.shape) {
      case Project::Assets::ParticleSystemAsset::SHAPE_POINT: break;
      case Project::Assets::ParticleSystemAsset::SHAPE_SPHERE:
        ImGui::DragFloat("Sphere Radius", &w.sphereRadius, 0.5f, 0.0f, 1000.0f);
        break;
      case Project::Assets::ParticleSystemAsset::SHAPE_BOX:
        ImGui::DragFloat3("Box Extents", &w.boxExtents.x, 0.5f, 0.0f, 1000.0f);
        break;
      case Project::Assets::ParticleSystemAsset::SHAPE_DISC:
        ImGui::DragFloat("Disc Radius", &w.discRadius, 0.5f, 0.0f, 1000.0f);
        ImGui::DragFloat3("Disc Normal", &w.discNormal.x, 0.05f, -1.0f, 1.0f);
        break;
    }
  }

  if (ImGui::CollapsingHeader("Particle", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloatRange2("Lifetime (s)", &w.lifetimeMin, &w.lifetimeMax, 0.05f, 0.0f, 30.0f);
    ImGui::DragFloatRange2("Start Scale",  &w.startScaleMin, &w.startScaleMax, 0.05f, 0.0f, 16.0f);
    ImGui::DragFloat3("Start Vel Dir", &w.startVelDir.x, 0.05f, -1.0f, 1.0f);
    ImGui::DragFloatRange2("Start Speed", &w.startVelSpeedMin, &w.startVelSpeedMax, 0.5f, 0.0f, 1000.0f);
    ImGui::DragFloat3("Gravity", &w.gravity.x, 1.0f, -2000.0f, 2000.0f);
    ImGui::DragFloat("Drag (/s)", &w.drag, 0.05f, 0.0f, 10.0f);
  }

  if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit4("Start Color", &w.startColor.x);
    ImGui::ColorEdit4("End Color",   &w.endColor.x);
    ImGui::Checkbox("Color Over Life", &w.colorOverLife);
    ImGui::SameLine();
    ImGui::Checkbox("Size Over Life",  &w.sizeOverLife);
  }
}

bool Editor::ParticleSystemEditor::draw(ImGuiID defDockId)
{
  if (!ctx.project) return false;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::PARTICLE_SYSTEM) return false;

  std::string title = std::string{ICON_MDI_SHIMMER " "} + getName()
    + (isDirty() ? " *" : "");
  winName = title + "###ParticleSystemEditorWin_" + std::to_string(assetUUID);

  if (firstDockTarget && !firstDockApplied) {
    ImGui::DockBuilderDockWindow(winName.c_str(), firstDockTarget);
    ImGui::SetNextWindowDockID(firstDockTarget, ImGuiCond_Always);
    firstDockApplied = true;
    firstDockFrame = false;
  } else {
    Editor::setupAssetEditorDocking(defDockId, firstDockFrame);
  }

  if (!isInit) {
    isInit = true;
    auto *mvp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
      {
        mvp->Pos.x + (mvp->Size.x - DEF_WIN_SIZE.x) * 0.5f,
        mvp->Pos.y + (mvp->Size.y - DEF_WIN_SIZE.y) * 0.5f,
      },
      ImGuiCond_FirstUseEver
    );
  }
  if (forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  ImGui::Begin(winName.c_str(), &isOpen,
    ImGuiWindowFlags_NoCollapse
    | (isDirty() ? ImGuiWindowFlags_UnsavedDocument : 0));

  if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) save();
  ImGui::SameLine();
  if (ImGui::Button(paused ? ICON_MDI_PLAY " Play" : ICON_MDI_PAUSE " Pause")) paused = !paused;
  ImGui::SameLine();
  if (ImGui::Button(ICON_MDI_REPLAY " Restart")) resetSim();
  ImGui::SameLine();
  ImGui::TextDisabled("(%s)", asset->path.c_str());

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
  }

  // Step the host-side sim with the ImGui frame's delta so the preview
  // mirrors what the engine will do at 60 fps.
  stepSim(ImGui::GetIO().DeltaTime);

  ImVec2 fullAvail = ImGui::GetContentRegionAvail();
  float splitterW = 6_px;
  float leftW = std::max(180_px, (fullAvail.x - splitterW) * previewSplitFrac);
  ImGui::BeginChild("##ptxLeftPane", ImVec2(leftW, 0), ImGuiChildFlags_None);
  drawPreview(ImGui::GetContentRegionAvail());
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::InvisibleButton("##ptxSplit", ImVec2(splitterW, -1));
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    splitDragging = true;
    float dx = ImGui::GetIO().MouseDelta.x;
    if (fullAvail.x > splitterW * 2) {
      previewSplitFrac += dx / (fullAvail.x - splitterW);
      previewSplitFrac = std::clamp(previewSplitFrac, 0.15f, 0.80f);
    }
  } else {
    splitDragging = false;
  }
  if (ImGui::IsItemHovered() || splitDragging) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  {
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    ImU32 col = ImGui::GetColorU32(splitDragging ? ImGuiCol_SeparatorActive : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(
      {(a.x + b.x) * 0.5f - 1.0f, a.y},
      {(a.x + b.x) * 0.5f + 1.0f, b.y},
      col
    );
  }

  ImGui::SameLine();
  ImGui::BeginChild("##ptxInspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
  drawInspector();
  ImGui::EndChild();

  ImGui::End();
  return isOpen;
}

void Editor::ParticleSystemEditor::save()
{
  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || !asset->particleAsset) return;

  *asset->particleAsset = working;
  Utils::FS::saveTextFile(asset->path, asset->particleAsset->serialize());
  savedState = asset->particleAsset->serialize();

  Utils::Logger::log("Saved Particle System: " + asset->name);
}

void Editor::ParticleSystemEditor::discardUnsavedChanges()
{
  working.deserialize(savedState);
}

void Editor::ParticleSystemEditor::focus() const
{
  ImGui::SetWindowFocus(("###ParticleSystemEditorWin_" + std::to_string(assetUUID)).c_str());
}
