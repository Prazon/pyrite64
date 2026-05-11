/**
* Particles2D component.
* Pool-style particle system with a static max capacity and an explicit
* spawn() API (no per-frame emitter rate). Gameplay code triggers spawns
* via Particles2D::spawn(obj, x, y, vx, vy, lifeSeconds, sizePx, palette);
* the component owns a heap-allocated array of particles and ticks them
* in update(), draws them in draw().
*
* Each particle: pos (float pair), vel (float pair), age (float seconds),
* life (float seconds), size (uint8 pixels), paletteIdx (uint8 selects an
* entry in the component's 4-color palette). Particles are drawn as
* rdpq_fill_rectangle(size x size). At size==1 that's a single pixel.
*
* The 4-color palette lets a single emitter spawn particles with varying
* tints (Pixic's white sparkle vs black gem dust use different palettes
* per spawn call). Color is constant over a particle's lifetime; cross-
* fading and trail effects can be layered by spawning multiple particles
* with staggered lifetimes from the gameplay side.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct Particles2D
  {
    static constexpr uint32_t ID = 26;

    struct Particle
    {
      float    px;
      float    py;
      float    vx;
      float    vy;
      float    age;
      float    life;
      uint8_t  size;
      uint8_t  paletteIdx;
      uint8_t  _pad0;
      uint8_t  _pad1;
    };
    static_assert(sizeof(Particle) == 28);

    Particle *pool{nullptr};
    uint16_t  maxParticles{64};
    uint16_t  nextSlot{0};        // round-robin spawn pointer
    float     gravityY{0.0f};     // pixels per second^2
    uint8_t   pal[4][4]{
      {255,255,255,255},
      {255,200,0,255},
      {0,255,200,255},
      {200,200,200,255},
    };

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Particles2D);
    }

    static void initDelete(Object& obj, Particles2D* data, void* initData);
    static void update(Object& obj, Particles2D* data, float deltaTime);
    static void draw(Object& obj, Particles2D* data, float deltaTime);

    // Spawn a new particle. Reuses the round-robin slot — at maxParticles
    // saturation this overwrites the oldest. paletteIdx must be 0..3.
    static void spawn(
      Particles2D* data,
      float x, float y,
      float vx, float vy,
      float lifeSeconds,
      uint8_t sizePx,
      uint8_t paletteIdx
    );

    // Clear all live particles.
    static void clear(Particles2D* data);
  };
}
