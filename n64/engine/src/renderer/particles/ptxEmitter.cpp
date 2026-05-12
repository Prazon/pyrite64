/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "renderer/particles/ptxEmitter.h"

#include <math.h>
#include <string.h>

namespace
{
  // Tiny LCG seeded by Object/component identity. Reproducible per-system
  // without pulling in libdragon's RNG.
  inline uint32_t lcgNext(uint32_t &s) {
    s = s * 1664525u + 1013904223u;
    return s;
  }
  inline float frand01(uint32_t &s) {
    return (lcgNext(s) >> 8) * (1.0f / (float)(1u << 24));
  }
  inline float frandRange(uint32_t &s, float a, float b) {
    return a + (b - a) * frand01(s);
  }

  inline uint8_t lerpU8(uint8_t a, uint8_t b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int v = (int)((float)a + ((float)b - (float)a) * t);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
  }
}

namespace P64::PTX
{
  EmitterFromAsset::EmitterFromAsset(const EmitterConf &cfg, const char *spritePath)
    : conf(cfg)
  {
    Sprites::Conf sc{
      .maxSize    = conf.maxParticles,
      .isRotating = conf.isRotating,
      .noRng      = conf.noRng,
    };
    sprites = new Sprites(spritePath ? spritePath : "rom:/", sc);

    if (conf.maxParticles > 0) {
      states = (State*)malloc_uncached(sizeof(State) * conf.maxParticles);
      memset(states, 0, sizeof(State) * conf.maxParticles);
    }

    lcg ^= (uint32_t)(uintptr_t)this;
  }

  EmitterFromAsset::~EmitterFromAsset()
  {
    if (states) free_uncached(states);
    if (sprites) delete sprites;
  }

  void EmitterFromAsset::reset()
  {
    if (sprites) sprites->clear();
    spawnAccum = 0.0f;
    emitterAge = 0.0f;
    bursted = false;
  }

  void EmitterFromAsset::spawnOneImpl()
  {
    if (!sprites) return;
    auto &sys = sprites->system;
    if (sys.isFull()) return;

    uint32_t s = lcg;

    // Shape offset relative to emitter origin.
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    switch (conf.shape) {
      case 0: break; // point
      case 1: {       // sphere (uniform interior)
        float r = conf.sphereRadius * frand01(s);
        float theta = frand01(s) * 6.2831853f;
        float phi   = (frand01(s) * 2.0f - 1.0f);  // cos(phi)
        float sinPhi = sqrtf(1.0f - phi * phi);
        ox = r * sinPhi * cosf(theta);
        oy = r * phi;
        oz = r * sinPhi * sinf(theta);
      } break;
      case 2: {       // box
        ox = (frand01(s) * 2.0f - 1.0f) * conf.boxExtentX;
        oy = (frand01(s) * 2.0f - 1.0f) * conf.boxExtentY;
        oz = (frand01(s) * 2.0f - 1.0f) * conf.boxExtentZ;
      } break;
      case 3: {       // disc on XZ plane (normal ignored for now)
        float r = conf.discRadius * sqrtf(frand01(s));
        float a = frand01(s) * 6.2831853f;
        ox = cosf(a) * r;
        oz = sinf(a) * r;
      } break;
    }

    float speed = frandRange(s, conf.startVelSpeedMin, conf.startVelSpeedMax);
    float dirX = conf.startVelDirX;
    float dirY = conf.startVelDirY;
    float dirZ = conf.startVelDirZ;
    float dirLen = sqrtf(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (dirLen > 1e-5f) {
      dirX /= dirLen; dirY /= dirLen; dirZ /= dirLen;
    }

    State newState{};
    newState.vx = dirX * speed;
    newState.vy = dirY * speed;
    newState.vz = dirZ * speed;
    newState.age = 0.0f;
    newState.lifetime  = frandRange(s, conf.lifetimeMin, conf.lifetimeMax);
    newState.startScale = frandRange(s, conf.startScaleMin, conf.startScaleMax);
    newState.seed = s;

    lcg = s;

    fm_vec3_t spawnPos{origin.x + ox, origin.y + oy, origin.z + oz};
    color_t col{
      conf.startColorR, conf.startColorG, conf.startColorB, conf.startColorA
    };
    sprites->add(spawnPos, newState.seed, col, newState.startScale);

    states[sys.count - 1] = newState;
  }

  void EmitterFromAsset::update(float dt)
  {
    if (!sprites || dt <= 0.0f) return;
    emitterAge += dt;

    bool emitterActive = active && (conf.loop || (emitterAge < conf.duration));
    if (emitterActive) {
      if (!bursted && conf.burstCount > 0) {
        for (uint32_t i = 0; i < conf.burstCount; ++i) {
          if (sprites->system.isFull()) break;
          spawnOneImpl();
        }
        bursted = true;
      }
      if (conf.spawnRate > 0.0f) {
        spawnAccum += dt * conf.spawnRate;
        while (spawnAccum >= 1.0f) {
          if (sprites->system.isFull()) { spawnAccum = 0.0f; break; }
          spawnOneImpl();
          spawnAccum -= 1.0f;
        }
      }
    }

    auto &sys = sprites->system;
    auto buff = sys.getBufferS16();
    float gx = conf.gravityX;
    float gy = conf.gravityY;
    float gz = conf.gravityZ;
    float dragScale = expf(-conf.drag * dt);

    for (uint32_t i = 0; i < sys.count; ) {
      State &s = states[i];
      s.age += dt;
      if (s.age >= s.lifetime) {
        sys.removeParticle(i);
        if (i < sys.count) {
          states[i] = states[sys.count];
        }
        continue;
      }

      s.vx = s.vx * dragScale + gx * dt;
      s.vy = s.vy * dragScale + gy * dt;
      s.vz = s.vz * dragScale + gz * dt;

      auto p = tpx_buffer_s16_get_pos(buff, i);
      p[0] = (int16_t)((float)p[0] + s.vx * dt);
      p[1] = (int16_t)((float)p[1] + s.vy * dt);
      p[2] = (int16_t)((float)p[2] + s.vz * dt);

      float t = (s.lifetime > 1e-5f) ? (s.age / s.lifetime) : 1.0f;

      if (conf.sizeOverLife) {
        float scale = s.startScale * (1.0f - t);
        int8_t newSize = (int8_t)(scale * 120.0f);
        if (newSize < 0) newSize = 0;
        *tpx_buffer_s16_get_size(buff, i) = newSize;
      }
      if (conf.colorOverLife) {
        auto rgba = tpx_buffer_s16_get_rgba(buff, i);
        rgba[0] = lerpU8(conf.startColorR, conf.endColorR, t);
        rgba[1] = lerpU8(conf.startColorG, conf.endColorG, t);
        rgba[2] = lerpU8(conf.startColorB, conf.endColorB, t);
        rgba[3] = lerpU8(conf.startColorA, conf.endColorA, t);
      }

      ++i;
    }
  }

  void EmitterFromAsset::draw(float dt)
  {
    if (sprites) sprites->draw(dt);
  }
}
