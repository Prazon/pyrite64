/**
* Particles2D implementation.
* Pool is malloc'd once at init from the configured maxParticles. update()
* walks every slot, advances live particles (age < life), and decrements
* their position by vel * dt. draw() emits one rdpq_fill_rectangle per
* live particle.
*
* This is intentionally minimal — gameplay-driven sparkles (Pixic's fx
* list) need a "spawn at xy with vx/vy/life" entry point, not a rate-
* based emitter. If a rate emitter is needed later, layer a separate
* component on top that calls spawn() periodically.
*/
#include "scene/components/particles2D.h"
#include "scene/scene.h"
#include "renderer/drawLayer.h"

#include <malloc.h>
#include <string.h>

namespace
{
  struct __attribute__((packed)) InitData
  {
    uint16_t maxParticles;
    uint16_t _pad0;
    float    gravityY;
    uint8_t  pal[4][4];   // 16 bytes
  };
  static_assert(sizeof(InitData) == 24);
}

namespace P64::Comp
{
  void Particles2D::initDelete(Object &obj, Particles2D* data, void* initData_)
  {
    if (initData_ == nullptr) {
      // destroy
      if (data->pool) { free(data->pool); data->pool = nullptr; }
      return;
    }

    auto *src = (InitData*)initData_;
    data->maxParticles = src->maxParticles ? src->maxParticles : 1;
    data->gravityY     = src->gravityY;
    data->nextSlot     = 0;
    memcpy(data->pal, src->pal, sizeof(data->pal));

    size_t bytes = (size_t)data->maxParticles * sizeof(Particle);
    data->pool = (Particle*)malloc(bytes);
    if (data->pool) {
      memset(data->pool, 0, bytes);
      // age==life keeps them inactive until spawn() bumps a slot.
      for (uint16_t i = 0; i < data->maxParticles; i++) {
        data->pool[i].age  = 1.0f;
        data->pool[i].life = 0.0f;
      }
    }
    (void)obj;
  }

  void Particles2D::spawn(
    Particles2D* data,
    float x, float y,
    float vx, float vy,
    float lifeSeconds,
    uint8_t sizePx,
    uint8_t paletteIdx
  ) {
    if (!data || !data->pool || data->maxParticles == 0) return;
    if (lifeSeconds <= 0.0f) return;
    uint16_t slot = data->nextSlot;
    data->nextSlot = (uint16_t)((slot + 1) % data->maxParticles);

    Particle &p = data->pool[slot];
    p.px         = x;
    p.py         = y;
    p.vx         = vx;
    p.vy         = vy;
    p.age        = 0.0f;
    p.life       = lifeSeconds;
    p.size       = sizePx ? sizePx : 1;
    p.paletteIdx = paletteIdx & 3;
  }

  void Particles2D::clear(Particles2D* data)
  {
    if (!data || !data->pool) return;
    for (uint16_t i = 0; i < data->maxParticles; i++) {
      data->pool[i].age  = 1.0f;
      data->pool[i].life = 0.0f;
    }
  }

  void Particles2D::update([[maybe_unused]] Object &obj, Particles2D* data, float deltaTime)
  {
    if (!data->pool) return;
    float dt = deltaTime;
    float gdt = data->gravityY * dt;
    uint16_t n = data->maxParticles;
    for (uint16_t i = 0; i < n; i++) {
      Particle &p = data->pool[i];
      if (p.age >= p.life) continue;
      p.age += dt;
      p.vy  += gdt;
      p.px  += p.vx * dt;
      p.py  += p.vy * dt;
    }
  }

  void Particles2D::draw([[maybe_unused]] Object &obj, Particles2D* data,
                         [[maybe_unused]] float deltaTime)
  {
    if (!data->pool) return;
    uint16_t n = data->maxParticles;

    // The standard mode is faster to set once than per-particle and works
    // for solid-color fills.
    rdpq_set_mode_fill({255, 255, 255, 255});

    uint8_t curPal = 0xFF;
    for (uint16_t i = 0; i < n; i++) {
      Particle &p = data->pool[i];
      if (p.age >= p.life) continue;
      if (p.paletteIdx != curPal) {
        rdpq_set_mode_fill({data->pal[p.paletteIdx][0],
                            data->pal[p.paletteIdx][1],
                            data->pal[p.paletteIdx][2],
                            data->pal[p.paletteIdx][3]});
        curPal = p.paletteIdx;
      }
      int x = (int)p.px;
      int y = (int)p.py;
      int s = (int)p.size;
      rdpq_fill_rectangle(x, y, x + s, y + s);
    }
  }
}
