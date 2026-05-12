/**
* @copyright 2026 - Prazon
* @license MIT
*
* Asset-driven sprite particle emitter. Wraps a PTX::Sprites system with a
* CPU simulation step that reads its configuration from a packed EmitterConf
* POD stamped into the scene blob by the editor's compParticleEmitter build
* path. Drop-in replacement for hand-rolled simulateXxx loops in user code.
*/
#pragma once
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/tpx.h>
#include "renderer/particles/ptxSprites.h"

namespace P64::PTX
{
  // Packed POD that mirrors Project::Assets::ParticleSystemAsset 1:1. The
  // editor side stamps an EmitterConf into the component's InitData so the
  // engine can construct the emitter without parsing JSON. Field order and
  // sizes must match compParticleEmitter::build on the editor side.
  //
  // Field-naming follows the asset for ease of grepping. All vec3/vec4
  // are decomposed into scalar floats so the struct stays trivially packed.
  struct __attribute__((packed, aligned(4))) EmitterConf
  {
    uint16_t spriteAssetIdx;   // 0xFFFF = no sprite (renders as a flat-color quad)
    uint8_t  particleType;     // matches PTX::System::Type
    uint8_t  shape;            // 0 point / 1 sphere / 2 box / 3 disc
    uint32_t maxParticles;

    float    spawnRate;        // particles per second; 0 disables continuous emit
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

  static_assert(sizeof(EmitterConf) % 4 == 0, "EmitterConf must be 4-byte aligned");

  // CPU-driven particle emitter that owns a PTX::Sprites system plus a
  // parallel side buffer holding per-particle state the TPX struct can't
  // carry (velocity, age, lifetime, seed). `update(dt)` integrates and
  // expires; `draw(dt)` issues the RDP commands via the underlying system.
  class EmitterFromAsset
  {
    public:
      struct State {
        float vx, vy, vz;
        float age;
        float lifetime;
        float startScale;
        uint32_t seed;
      };

    private:
      EmitterConf conf;
      Sprites *sprites{nullptr};
      State   *states{nullptr};
      fm_vec3_t origin{};
      float spawnAccum{0.0f};
      float emitterAge{0.0f};
      uint32_t lcg{0x9E3779B9u};
      bool bursted{false};
      bool active{true};

    public:
      EmitterFromAsset(const EmitterConf &cfg, const char *spritePath);
      ~EmitterFromAsset();

      // Reset the system: all particles die, emitter age clears, burst rearms.
      void reset();

      // Toggle continuous emission. Existing particles keep simulating.
      void setActive(bool a) { active = a; }
      bool isActive() const { return active; }

      // Origin for newly spawned particles. Components set this each frame
      // from their Object's world position.
      void setOrigin(const fm_vec3_t &p) { origin = p; }

      // Sim + draw. update() should be called once per frame before draw().
      void update(float dt);
      void draw(float dt);

      const EmitterConf& getConf() const { return conf; }
      uint32_t getCount() const { return sprites ? sprites->system.count : 0; }

    private:
      // Spawn one particle at `origin`, sampling shape, lifetime, velocity
      // and start scale through the seeded LCG. Caller is responsible for
      // not calling when sprites->system.isFull().
      void spawnOneImpl();
  };
}
