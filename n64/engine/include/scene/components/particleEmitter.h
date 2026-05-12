/**
* @copyright 2026 - Prazon
* @license MIT
*
* Particle-emitter component. References a .p64ptx asset that the editor has
* compiled into an inline EmitterConf blob in the scene data. Owns a single
* PTX::EmitterFromAsset and drives it from this Object's world position.
*/
#pragma once
#include <libdragon.h>
#include "assets/assetManager.h"
#include "renderer/particles/ptxEmitter.h"
#include "scene/object.h"

namespace P64::Comp
{
  struct ParticleEmitter
  {
    static constexpr uint32_t ID = 30;

    PTX::EmitterFromAsset *emitter{nullptr};
    uint8_t  autoPlay{1};
    uint8_t  worldSpace{1};
    uint8_t  pad0{0};
    uint8_t  pad1{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(ParticleEmitter);
    }

    static void initDelete(Object& obj, ParticleEmitter* data, void* initData);
    static void update(Object& obj, ParticleEmitter* data, float deltaTime);
    static void draw(Object& obj, ParticleEmitter* data, float deltaTime);
  };
}
