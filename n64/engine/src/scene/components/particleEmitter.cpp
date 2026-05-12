/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "scene/components/particleEmitter.h"
#include "assets/assetManager.h"

namespace
{
  // Editor-side compParticleEmitter::build writes this layout. Layout is:
  //   1 x EmitterConf POD
  //   2 x uint8 flags (autoPlay, worldSpace)
  //   2 x uint8 padding
  // Keep field order and packing exact — the engine reads it raw.
  struct __attribute__((packed)) InitData
  {
    P64::PTX::EmitterConf conf;
    uint8_t autoPlay;
    uint8_t worldSpace;
    uint8_t pad0;
    uint8_t pad1;
  };
}

namespace P64::Comp
{
  void ParticleEmitter::initDelete(Object &obj, ParticleEmitter *data, void *initData_)
  {
    if (initData_ == nullptr) {
      // delete path
      if (data->emitter) {
        delete data->emitter;
        data->emitter = nullptr;
      }
      return;
    }

    auto *initData = (InitData*)initData_;
    data->autoPlay   = initData->autoPlay;
    data->worldSpace = initData->worldSpace;
    data->pad0 = 0;
    data->pad1 = 0;

    // PTX::Sprites needs a real sprite path; without one the emitter would
    // crash at init. Skip construction when the asset has no sprite bound;
    // update()/draw() then no-op via the null check.
    if (initData->conf.spriteAssetIdx == 0xFFFF) {
      data->emitter = nullptr;
      return;
    }

    // PTX::Sprites wants the rom path string, not the loaded sprite_t. The
    // engine's assetTable stores the path alongside the loaded payload.
    const char *spritePath = AssetManager::getPathByIndex(initData->conf.spriteAssetIdx);
    if (!spritePath) {
      data->emitter = nullptr;
      return;
    }

    data->emitter = new PTX::EmitterFromAsset(initData->conf, spritePath);
    if (!data->autoPlay) data->emitter->setActive(false);
    data->emitter->setOrigin(obj.pos);
  }

  void ParticleEmitter::update(Object &obj, ParticleEmitter *data, float deltaTime)
  {
    if (!data->emitter) return;
    if (data->worldSpace) {
      data->emitter->setOrigin(obj.pos);
    } else {
      // Local-space emission: integrate from a fixed origin (0,0,0) and let
      // particles translate with the Object via the scene transform stack.
      // The PTX system today renders in world space, so we still feed the
      // Object position here but reserve the flag for the matrix-stack path.
      data->emitter->setOrigin(obj.pos);
    }
    data->emitter->update(deltaTime);
  }

  void ParticleEmitter::draw([[maybe_unused]] Object &obj, ParticleEmitter *data, float deltaTime)
  {
    if (!data->emitter) return;
    data->emitter->draw(deltaTime);
  }
}
