/**
* Primitive component (added by SPBF64 fork).
* Renders a procedural lit 3D shape (box, sphere, cylinder, capsule, cone,
* pyramid, plane) at the object's transform. The mesh is generated at
* component init time from the shape parameters baked by the editor.
*/
#pragma once
#include <libdragon.h>
#include <t3d/t3d.h>
#include "scene/object.h"
#include "lib/matrixManager.h"

namespace P64::Comp
{
  struct Primitive
  {
    static constexpr uint32_t ID = 13;

    enum class ShapeType : uint8_t {
      Box      = 0,
      Sphere   = 1,
      Cylinder = 2,
      Capsule  = 3,
      Cone     = 4,
      Pyramid  = 5,
      Plane    = 6,
    };

    ShapeType shape{ShapeType::Box};
    uint8_t   layerIdx{0};
    uint8_t   color[4]{0xFF, 0xFF, 0xFF, 0xFF};
    float     halfExtend[3]{16.0f, 16.0f, 16.0f};

    // Generated at init.
    T3DVertPacked *verts{nullptr};
    uint16_t       vertCount{0};
    rspq_block_t  *drawBlock{nullptr};
    RingMat4FP matFP{};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Primitive);
    }

    static void initDelete(Object& obj, Primitive* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] Primitive* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, Primitive* data, float deltaTime);
  };
}
