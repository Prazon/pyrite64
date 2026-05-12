/**
* @copyright 2026 - Prazon
* @license MIT
*
* On-disk schema for a .p64ptx particle system asset. Captures everything an
* engine-side PTX::EmitterFromAsset needs to spawn, simulate and draw a sprite
* particle system without per-instance code in the user project.
*
* The asset is JSON-on-disk and inlined into every component instance that
* references it (no separate rom blob). That mirrors how .p64mat materials
* resolve at editor build time.
*/
#pragma once
#include <cstdint>
#include <string>

#include "json.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

namespace Project::Assets
{
  struct ParticleSystemAsset
  {
    // simModel reserves a slot for a future RSP-driven mode. Today only
    // CpuPerParticle is implemented; the engine looks at this field and
    // currently asserts on anything else, so older assets stay valid as we
    // add modes.
    enum SimModel : int32_t {
      SIM_CPU_PER_PARTICLE = 0,
    };

    // Mirrors PTX::System::Type. Default matches PTX::Sprites today.
    enum ParticleType : int32_t {
      PT_COLOR_RGBA_S8 = 0,
      PT_TEX_RGBA_S8   = 1,
      PT_COLOR_A_S16   = 2,
      PT_TEX_A_S16     = 3,
    };

    enum ShapeKind : int32_t {
      SHAPE_POINT  = 0,
      SHAPE_SPHERE = 1,
      SHAPE_BOX    = 2,
      SHAPE_DISC   = 3,
    };

    uint64_t uuid{0};
    int32_t  version{1};
    SimModel simModel{SIM_CPU_PER_PARTICLE};
    ParticleType particleType{PT_TEX_A_S16};

    uint64_t spriteUUID{0};

    // Emitter
    uint32_t maxParticles{128};
    float    spawnRate{32.0f};     // particles/sec; 0 disables continuous emit
    uint32_t burstCount{0};
    bool     loop{true};
    float    duration{2.0f};       // emitter active duration; ignored when loop
    bool     isRotating{false};    // PTX::Sprites::Conf.isRotating
    bool     noRng{false};         // PTX::Sprites::Conf.noRng

    // Shape
    ShapeKind shape{SHAPE_POINT};
    float     sphereRadius{0.0f};
    glm::vec3 boxExtents{0,0,0};
    float     discRadius{0.0f};
    glm::vec3 discNormal{0,1,0};

    // Particle motion / life
    float lifetimeMin{0.5f};
    float lifetimeMax{1.0f};
    float startScaleMin{1.0f};
    float startScaleMax{1.0f};
    glm::vec3 startVelDir{0, 1, 0};
    float startVelSpeedMin{20.0f};
    float startVelSpeedMax{40.0f};
    glm::vec3 gravity{0, -50.0f, 0};
    float drag{0.0f};              // per-second velocity falloff in [0..1)

    // Color over life
    glm::vec4 startColor{1, 1, 1, 1};
    glm::vec4 endColor{1, 1, 1, 0};
    bool colorOverLife{true};
    bool sizeOverLife{true};

    // Texture animation
    float animFps{15.0f};

    [[nodiscard]] std::string serialize() const;
    void deserialize(const std::string &doc);
  };
}
