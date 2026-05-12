/**
* @copyright 2026 - Prazon
* @license MIT
*
* Editor-side ParticleEmitter component. References a .p64ptx asset by uuid;
* build() resolves the asset, packs its conf into an EmitterConf POD and
* writes it inline so the engine reads it without a separate ROM blob.
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../../utils/colors.h"
#include "../../assetManager.h"
#include "../../assets/particleSystemAsset.h"
#include "../../../editor/pages/parts/viewport3D.h"
#include "../../../editor/pages/editorScene.h"
#include "../../../utils/meshGen.h"

#include "imgui.h"

namespace
{
  // Packed mirror of n64/engine/include/renderer/particles/ptxEmitter.h
  // P64::PTX::EmitterConf. Field order MUST match exactly.
  struct __attribute__((packed, aligned(4))) EmitterConfPacked
  {
    uint16_t spriteAssetIdx;
    uint8_t  particleType;
    uint8_t  shape;
    uint32_t maxParticles;

    float    spawnRate;
    uint32_t burstCount;
    uint8_t  loop;
    uint8_t  isRotating;
    uint8_t  noRng;
    uint8_t  pad0;
    float    duration;

    float    sphereRadius;
    float    boxExtentX, boxExtentY, boxExtentZ;
    float    discRadius;
    float    discNormalX, discNormalY, discNormalZ;

    float    lifetimeMin, lifetimeMax;
    float    startScaleMin, startScaleMax;
    float    startVelDirX, startVelDirY, startVelDirZ;
    float    startVelSpeedMin, startVelSpeedMax;
    float    gravityX, gravityY, gravityZ;
    float    drag;

    uint8_t  startColorR, startColorG, startColorB, startColorA;
    uint8_t  endColorR,   endColorG,   endColorB,   endColorA;
    uint8_t  colorOverLife;
    uint8_t  sizeOverLife;
    uint8_t  pad1;
    uint8_t  pad2;
    float    animFps;
  };
}

namespace Project::Component::ParticleEmitter
{
  struct Data
  {
    PROP_U64(particleAssetUUID);
    PROP_BOOL(autoPlay);
    PROP_BOOL(worldSpace);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->autoPlay.value   = true;
    data->worldSpace.value = true;
    return data;
  }

  nlohmann::json serialize(const Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.particleAssetUUID);
    b.set(data.autoPlay);
    b.set(data.worldSpace);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc)
  {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->particleAssetUUID);
    Utils::JSON::readProp(doc, data->autoPlay, true);
    Utils::JSON::readProp(doc, data->worldSpace, true);
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    uint64_t ptxUUID = data.particleAssetUUID.resolve(obj.propOverrides);

    EmitterConfPacked cfg{};
    cfg.spriteAssetIdx = 0xFFFF;
    cfg.maxParticles   = 0;

    auto *ptxEntry = (ptxUUID != 0)
      ? ::ctx.project->getAssets().getEntryByUUID(ptxUUID)
      : nullptr;

    if (ptxEntry && ptxEntry->type == FileType::PARTICLE_SYSTEM && ptxEntry->particleAsset) {
      const auto &p = *ptxEntry->particleAsset;
      cfg.particleType  = (uint8_t)p.particleType;
      cfg.shape         = (uint8_t)p.shape;
      cfg.maxParticles  = p.maxParticles;
      cfg.spawnRate     = p.spawnRate;
      cfg.burstCount    = p.burstCount;
      cfg.loop          = p.loop ? 1 : 0;
      cfg.isRotating    = p.isRotating ? 1 : 0;
      cfg.noRng         = p.noRng ? 1 : 0;
      cfg.duration      = p.duration;
      cfg.sphereRadius  = p.sphereRadius;
      cfg.boxExtentX    = p.boxExtents.x;
      cfg.boxExtentY    = p.boxExtents.y;
      cfg.boxExtentZ    = p.boxExtents.z;
      cfg.discRadius    = p.discRadius;
      cfg.discNormalX   = p.discNormal.x;
      cfg.discNormalY   = p.discNormal.y;
      cfg.discNormalZ   = p.discNormal.z;
      cfg.lifetimeMin   = p.lifetimeMin;
      cfg.lifetimeMax   = p.lifetimeMax;
      cfg.startScaleMin = p.startScaleMin;
      cfg.startScaleMax = p.startScaleMax;
      cfg.startVelDirX  = p.startVelDir.x;
      cfg.startVelDirY  = p.startVelDir.y;
      cfg.startVelDirZ  = p.startVelDir.z;
      cfg.startVelSpeedMin = p.startVelSpeedMin;
      cfg.startVelSpeedMax = p.startVelSpeedMax;
      cfg.gravityX      = p.gravity.x;
      cfg.gravityY      = p.gravity.y;
      cfg.gravityZ      = p.gravity.z;
      cfg.drag          = p.drag;
      cfg.startColorR   = (uint8_t)(p.startColor.x * 255.0f);
      cfg.startColorG   = (uint8_t)(p.startColor.y * 255.0f);
      cfg.startColorB   = (uint8_t)(p.startColor.z * 255.0f);
      cfg.startColorA   = (uint8_t)(p.startColor.w * 255.0f);
      cfg.endColorR     = (uint8_t)(p.endColor.x * 255.0f);
      cfg.endColorG     = (uint8_t)(p.endColor.y * 255.0f);
      cfg.endColorB     = (uint8_t)(p.endColor.z * 255.0f);
      cfg.endColorA     = (uint8_t)(p.endColor.w * 255.0f);
      cfg.colorOverLife = p.colorOverLife ? 1 : 0;
      cfg.sizeOverLife  = p.sizeOverLife ? 1 : 0;
      cfg.animFps       = p.animFps;

      if (p.spriteUUID != 0) {
        auto res = ctx.assetUUIDToIdx.find(p.spriteUUID);
        if (res != ctx.assetUUIDToIdx.end()) cfg.spriteAssetIdx = res->second;
      }
    } else if (ptxUUID != 0) {
      Utils::Logger::log(
        "ParticleEmitter: particle-system UUID not resolved: " + std::to_string(ptxUUID),
        Utils::Logger::LEVEL_WARN
      );
    }

    // Layout MUST match engine InitData in particleEmitter.cpp.
    // BinaryFile::write template only supports scalar/glm types, so emit
    // each field individually rather than the whole struct in one call.
    ctx.fileObj.write(cfg.spriteAssetIdx);
    ctx.fileObj.write(cfg.particleType);
    ctx.fileObj.write(cfg.shape);
    ctx.fileObj.write(cfg.maxParticles);
    ctx.fileObj.write(cfg.spawnRate);
    ctx.fileObj.write(cfg.burstCount);
    ctx.fileObj.write(cfg.loop);
    ctx.fileObj.write(cfg.isRotating);
    ctx.fileObj.write(cfg.noRng);
    ctx.fileObj.write(cfg.pad0);
    ctx.fileObj.write(cfg.duration);
    ctx.fileObj.write(cfg.sphereRadius);
    ctx.fileObj.write(cfg.boxExtentX);
    ctx.fileObj.write(cfg.boxExtentY);
    ctx.fileObj.write(cfg.boxExtentZ);
    ctx.fileObj.write(cfg.discRadius);
    ctx.fileObj.write(cfg.discNormalX);
    ctx.fileObj.write(cfg.discNormalY);
    ctx.fileObj.write(cfg.discNormalZ);
    ctx.fileObj.write(cfg.lifetimeMin);
    ctx.fileObj.write(cfg.lifetimeMax);
    ctx.fileObj.write(cfg.startScaleMin);
    ctx.fileObj.write(cfg.startScaleMax);
    ctx.fileObj.write(cfg.startVelDirX);
    ctx.fileObj.write(cfg.startVelDirY);
    ctx.fileObj.write(cfg.startVelDirZ);
    ctx.fileObj.write(cfg.startVelSpeedMin);
    ctx.fileObj.write(cfg.startVelSpeedMax);
    ctx.fileObj.write(cfg.gravityX);
    ctx.fileObj.write(cfg.gravityY);
    ctx.fileObj.write(cfg.gravityZ);
    ctx.fileObj.write(cfg.drag);
    ctx.fileObj.write(cfg.startColorR);
    ctx.fileObj.write(cfg.startColorG);
    ctx.fileObj.write(cfg.startColorB);
    ctx.fileObj.write(cfg.startColorA);
    ctx.fileObj.write(cfg.endColorR);
    ctx.fileObj.write(cfg.endColorG);
    ctx.fileObj.write(cfg.endColorB);
    ctx.fileObj.write(cfg.endColorA);
    ctx.fileObj.write(cfg.colorOverLife);
    ctx.fileObj.write(cfg.sizeOverLife);
    ctx.fileObj.write(cfg.pad1);
    ctx.fileObj.write(cfg.pad2);
    ctx.fileObj.write(cfg.animFps);
    ctx.fileObj.write<uint8_t>(data.autoPlay.resolve(obj.propOverrides) ? 1 : 0);
    ctx.fileObj.write<uint8_t>(data.worldSpace.resolve(obj.propOverrides) ? 1 : 0);
    ctx.fileObj.write<uint8_t>(0);
    ctx.fileObj.write<uint8_t>(0);
  }

  // Picker for a .p64ptx asset. Combo + drag-drop target.
  static void drawAssetPicker(uint64_t &uuid)
  {
    auto &assets = ::ctx.project->getAssets();
    auto *current = (uuid != 0) ? assets.getEntryByUUID(uuid) : nullptr;
    const char *preview = current ? current->name.c_str() : "<None>";

    ImGui::PushID("ptxPicker");
    if (ImGui::BeginCombo("Particle System", preview)) {
      if (ImGui::Selectable("<None>", uuid == 0)) uuid = 0;
      for (const auto &e : assets.getTypeEntries(FileType::PARTICLE_SYSTEM)) {
        bool sel = (e.conf.uuid == uuid);
        if (ImGui::Selectable(e.name.c_str(), sel)) uuid = e.conf.uuid;
      }
      ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET")) {
        uint64_t dropped = *(uint64_t*)payload->Data;
        auto *e = assets.getEntryByUUID(dropped);
        if (e && e->type == FileType::PARTICLE_SYSTEM) uuid = dropped;
      }
      ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);

      ImTable::add("Asset");
      uint64_t &uuid = data.particleAssetUUID.resolve(obj);
      drawAssetPicker(uuid);

      ImTable::addObjProp("Auto Play", data.autoPlay);
      ImTable::addObjProp("World Space", data.worldSpace);

      ImTable::end();
    }
  }

  // A small 3D-viewport gizmo so the emitter is pickable when nothing is
  // bound yet. Matches the visual convention used by Light/Audio2D for
  // "this is a thing that lives at obj.pos but has no inherent geometry".
  void draw3D(Object &obj, Entry & /*entry*/, Editor::Viewport3D &vp,
              SDL_GPUCommandBuffer* /*cmdBuff*/, SDL_GPURenderPass* /*pass*/)
  {
    glm::u8vec4 col{0x96, 0xFF, 0x96, 0xFF};
    if (Editor::activeViewportSelection().isSelected(obj.uuid)) {
      col = Utils::Colors::kSelectionTint;
    }
    Utils::Mesh::addSprite(*vp.getSprites(), obj.pos.resolve(obj.propOverrides),
                           obj.uuid, 4, col);
  }
}
