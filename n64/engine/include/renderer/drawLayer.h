/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>

#include "scene/components/collBody.h"

namespace P64::DrawLayer
{
  struct Conf
  {
    static constexpr uint32_t FLAG_Z_WRITE = 1 << 0;
    static constexpr uint32_t FLAG_Z_COMPARE = 1 << 1;

    enum class FogMode : uint8_t
    {
      NONE = 0,
      CLEAR_COLOR,
      CUSTOM_COLOR,
      UNCHANGED_COLOR,
    };

    uint32_t flags{};
    uint32_t blender{};

    color_t fogColor{};
    float fogMin{};
    float fogMax{};
    FogMode fogMode{};
    uint8_t lightMode{};

    uint8_t padding1{};
    uint8_t padding2{};

  };

  struct Setup
  {
    uint8_t layerCount3D{};
    uint8_t layerCountPtx{};
    uint8_t layerCount2D{};
    uint8_t padding{};
    Conf layerConf[16]{};
  };

  void init(Setup &setup);

  void use(uint32_t idx);

  inline void use3D(uint32_t idx) { use(idx); }
  void usePtx(uint32_t idx = 0);
  void use2D(uint32_t idx = 0);

  inline void useDefault() { use(0); }


  void draw(uint32_t layerIdx);

  void draw3D();
  void drawPtx();
  void draw2D();

  void nextFrame();
  void reset();

  // 2D primitive draw helpers (fork).

  // Blend mode for textured 2D primitives. Alpha is the default to match
  // Godot/GameMaker, where sprites alpha-blend against the framebuffer.
  enum class Blend2D : uint8_t { Alpha = 0, None = 1, Additive = 2 };

  // Configure rdpq for a textured 2D blit so the PRIM colour (set via
  // rdpq_set_prim_color) modulates both the texture RGB and alpha, with the
  // given filter and blender. Standard mode alone installs a TEX0-only
  // combiner and no blender, so any tint or alpha would otherwise be silently
  // discarded on device. Call this, then rdpq_set_prim_color(tint), then blit.
  void beginSprite2D(Blend2D blend = Blend2D::Alpha, bool bilinear = false, uint8_t alphaCompare = 0);
}
