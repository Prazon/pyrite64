/**
* Primitive component implementation (added by SPBF64 fork).
* At init time the editor's baked metadata (shape + halfExtend + color +
* layer) drives generation of a procedural T3DVertPacked mesh and a
* recorded rspq display list. draw() pushes the object's transform and
* runs the cached display list.
*/
#include "scene/components/primitive.h"
#include "renderer/drawLayer.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>
#include <string.h>

namespace
{
  // Binary layout matching what compPrimitive.cpp::build writes.
  // Keep field order/padding in sync with the editor side.
  struct __attribute__((packed)) InitData
  {
    uint8_t  shape;       // P64::Comp::Primitive::ShapeType
    uint8_t  layerIdx;
    uint8_t  r, g, b, a;
    uint8_t  pad[2];      // align(4)
    float    halfX, halfY, halfZ;
  };
  static_assert(sizeof(InitData) == 20);

  // Pack a unit-length normal into the 5/6/5 layout that t3d/n64 expects.
  // (Same scheme used by t3d_vert_pack_normal.)
  uint16_t packNormal(float nx, float ny, float nz)
  {
    int x = (int)(nx * 15.0f); if (x < -16) x = -16; if (x > 15) x = 15;
    int y = (int)(ny * 31.0f); if (y < -32) y = -32; if (y > 31) y = 31;
    int z = (int)(nz * 15.0f); if (z < -16) z = -16; if (z > 15) z = 15;
    return (uint16_t)(((x & 0x1F) << 11) | ((y & 0x3F) << 5) | (z & 0x1F));
  }

  struct VertScratch {
    int16_t  px, py, pz;
    uint16_t norm;
  };

  void writeVert(T3DVertPacked *packed, uint32_t idx, const VertScratch &v, uint32_t rgba)
  {
    T3DVertPacked &p = packed[idx >> 1];
    if ((idx & 1) == 0) {
      p.posA[0] = v.px; p.posA[1] = v.py; p.posA[2] = v.pz;
      p.rgbaA   = rgba;
      p.normA   = v.norm;
    } else {
      p.posB[0] = v.px; p.posB[1] = v.py; p.posB[2] = v.pz;
      p.rgbaB   = rgba;
      p.normB   = v.norm;
    }
  }

  // Allocate uncached T3DVertPacked storage to hold `vertCount` verts.
  T3DVertPacked* allocVerts(uint16_t vertCount) {
    uint32_t pairs = (vertCount + 1) >> 1;
    return (T3DVertPacked*)malloc_uncached(sizeof(T3DVertPacked) * pairs);
  }

  // ---------------- Per-shape generators ----------------
  // Each fills *outVerts (allocated by caller-helper after computing count)
  // and emits an rspq block of t3d_vert_load + t3d_tri_draw + t3d_tri_sync
  // calls. Since t3d_vert_load can hold up to ~70 verts, all shapes here are
  // sized to fit a single load per draw segment.

  void genBox(float hx, float hy, float hz, uint32_t rgba,
              T3DVertPacked **outVerts, uint16_t *outVertCount,
              rspq_block_t **outBlock)
  {
    // 6 faces × 4 verts = 24 verts (per-face normals require duplication).
    constexpr uint16_t COUNT = 24;
    auto *verts = allocVerts(COUNT);
    *outVerts = verts;
    *outVertCount = COUNT;

    struct Face { float nx, ny, nz; int16_t v[4][3]; };
    int16_t X = (int16_t)hx, Y = (int16_t)hy, Z = (int16_t)hz;
    // Pre-negated copies — unary minus on int16_t promotes to int and would
    // narrow when stored back into the int16_t v[4][3] brace-initializer
    // under -Werror=narrowing.
    int16_t NX = (int16_t)-X, NY = (int16_t)-Y, NZ = (int16_t)-Z;
    Face faces[6] = {
      // +Y top
      {0,1,0, {{NX, Y,NZ},{ X, Y,NZ},{ X, Y, Z},{NX, Y, Z}}},
      // -Y bottom
      {0,-1,0,{{NX,NY, Z},{ X,NY, Z},{ X,NY,NZ},{NX,NY,NZ}}},
      // +X right
      {1,0,0, {{ X,NY,NZ},{ X, Y,NZ},{ X, Y, Z},{ X,NY, Z}}},
      // -X left
      {-1,0,0,{{NX,NY, Z},{NX, Y, Z},{NX, Y,NZ},{NX,NY,NZ}}},
      // +Z front
      {0,0,1, {{NX,NY, Z},{ X,NY, Z},{ X, Y, Z},{NX, Y, Z}}},
      // -Z back
      {0,0,-1,{{NX, Y,NZ},{ X, Y,NZ},{ X,NY,NZ},{NX,NY,NZ}}},
    };

    uint32_t idx = 0;
    for (auto &f : faces) {
      uint16_t n = packNormal(f.nx, f.ny, f.nz);
      for (int c = 0; c < 4; ++c) {
        writeVert(verts, idx++, {f.v[c][0], f.v[c][1], f.v[c][2], n}, rgba);
      }
    }

    rspq_block_begin();
    for (int face = 0; face < 6; ++face) {
      int base = face * 4;
      t3d_vert_load(&verts[base / 2], 0, 4);
      t3d_tri_draw(0, 1, 2);
      t3d_tri_draw(2, 3, 0);
      t3d_tri_sync();
    }
    *outBlock = rspq_block_end();
  }

  void genPlane(float hx, float hz, uint32_t rgba,
                T3DVertPacked **outVerts, uint16_t *outVertCount,
                rspq_block_t **outBlock)
  {
    constexpr uint16_t COUNT = 4;
    auto *verts = allocVerts(COUNT);
    *outVerts = verts;
    *outVertCount = COUNT;

    int16_t X = (int16_t)hx, Z = (int16_t)hz;
    int16_t NX = (int16_t)-X, NZ = (int16_t)-Z;
    uint16_t n = packNormal(0, 1, 0);
    writeVert(verts, 0, {NX, 0, NZ, n}, rgba);
    writeVert(verts, 1, { X, 0, NZ, n}, rgba);
    writeVert(verts, 2, { X, 0,  Z, n}, rgba);
    writeVert(verts, 3, {NX, 0,  Z, n}, rgba);

    rspq_block_begin();
    t3d_vert_load(verts, 0, 4);
    t3d_tri_draw(0, 1, 2);
    t3d_tri_draw(2, 3, 0);
    t3d_tri_sync();
    *outBlock = rspq_block_end();
  }

  void genPyramid(float hx, float hy, float hz, uint32_t rgba,
                  T3DVertPacked **outVerts, uint16_t *outVertCount,
                  rspq_block_t **outBlock)
  {
    // 4 base verts + 4 unique tip verts (one per side, normal differs) +
    // 4 base verts (down-facing) for the bottom quad = 12 verts.
    constexpr uint16_t COUNT = 16;
    auto *verts = allocVerts(COUNT);
    *outVerts = verts;
    *outVertCount = COUNT;

    int16_t X = (int16_t)hx, Y = (int16_t)hy, Z = (int16_t)hz;
    int16_t NX = (int16_t)-X, NY = (int16_t)-Y, NZ = (int16_t)-Z;
    // Side faces: each has a tip + 2 base corners.
    struct Side { float nx, ny, nz; int16_t a[3], b[3]; };
    // Tip is at (0, +Y, 0). Base lies on -Y plane.
    // Side normals tilt outward + up.
    float invLen = 1.0f / sqrtf(hy*hy + hx*hx);
    float invLenZ = 1.0f / sqrtf(hy*hy + hz*hz);
    Side sides[4] = {
      // +Z side: base goes from (-X,-Y,Z) to (X,-Y,Z); tip up.
      {0,           hz*invLenZ, hy*invLenZ, {NX,NY, Z}, { X,NY, Z}},
      // +X side
      {hx*invLen,   hy*invLen,   0,         { X,NY, Z}, { X,NY,NZ}},
      // -Z side
      {0,           hz*invLenZ, -hy*invLenZ,{ X,NY,NZ}, {NX,NY,NZ}},
      // -X side
      {-hx*invLen,  hy*invLen,   0,         {NX,NY,NZ}, {NX,NY, Z}},
    };

    uint32_t idx = 0;
    int16_t tipPos[3] = {0, Y, 0};
    for (auto &s : sides) {
      uint16_t n = packNormal(s.nx, s.ny, s.nz);
      writeVert(verts, idx++, {tipPos[0], tipPos[1], tipPos[2], n}, rgba);
      writeVert(verts, idx++, {s.a[0], s.a[1], s.a[2], n}, rgba);
      writeVert(verts, idx++, {s.b[0], s.b[1], s.b[2], n}, rgba);
    }
    // Base quad (4 verts, down-facing normal).
    uint16_t nDown = packNormal(0, -1, 0);
    writeVert(verts, idx++, {NX,NY, Z, nDown}, rgba);
    writeVert(verts, idx++, { X,NY, Z, nDown}, rgba);
    writeVert(verts, idx++, { X,NY,NZ, nDown}, rgba);
    writeVert(verts, idx++, {NX,NY,NZ, nDown}, rgba);

    rspq_block_begin();
    t3d_vert_load(verts, 0, COUNT);
    // 4 side triangles
    for (int s = 0; s < 4; ++s) {
      int b = s * 3;
      t3d_tri_draw(b + 0, b + 1, b + 2);
    }
    // base quad (CCW from -Y POV)
    t3d_tri_draw(12, 14, 13);
    t3d_tri_draw(12, 15, 14);
    t3d_tri_sync();
    *outBlock = rspq_block_end();
  }

  // Parametric-ring helper used by sphere/cylinder/cone/capsule.
  // Produces a ring of `segs` verts at given y/radius with given normal.
  void pushRing(T3DVertPacked *verts, uint32_t &idx, int segs,
                float radius, float y, float nyMul, uint32_t rgba)
  {
    for (int i = 0; i < segs; ++i) {
      float a = 2.0f * (float)M_PI * (float)i / (float)segs;
      float c = cosf(a), s = sinf(a);
      float nx = c, nz = s;
      float ny = nyMul;
      // Renormalize when ny is non-zero (cone slant case)
      float invLen = 1.0f / sqrtf(nx*nx + ny*ny + nz*nz);
      uint16_t n = packNormal(nx*invLen, ny*invLen, nz*invLen);
      writeVert(verts, idx++,
        {(int16_t)(radius * c), (int16_t)y, (int16_t)(radius * s), n}, rgba);
    }
  }

  void genCylinder(float radius, float halfH, uint32_t rgba,
                   T3DVertPacked **outVerts, uint16_t *outVertCount,
                   rspq_block_t **outBlock)
  {
    constexpr int SEGS = 10;
    // top ring (side) + bottom ring (side) + top center + bottom center +
    // top ring (cap) + bottom ring (cap) = 2*SEGS + 2 + 2*SEGS = 4*SEGS+2
    uint16_t COUNT = 4 * SEGS + 2;
    auto *verts = allocVerts(COUNT);
    *outVerts = verts;
    *outVertCount = COUNT;

    uint32_t idx = 0;
    // Side rings (normals point outward, ny = 0)
    pushRing(verts, idx, SEGS, radius,  halfH, 0.0f, rgba);   // 0 .. SEGS-1
    pushRing(verts, idx, SEGS, radius, -halfH, 0.0f, rgba);   // SEGS .. 2*SEGS-1
    // Top cap centre
    uint16_t nUp = packNormal(0, 1, 0);
    writeVert(verts, idx++, {0, (int16_t)halfH, 0, nUp}, rgba); // 2*SEGS
    // Top cap ring (normal up)
    for (int i = 0; i < SEGS; ++i) {
      float a = 2.0f * (float)M_PI * (float)i / (float)SEGS;
      writeVert(verts, idx++,
        {(int16_t)(radius * cosf(a)), (int16_t)halfH, (int16_t)(radius * sinf(a)), nUp}, rgba);
    }
    // Bottom cap centre
    uint16_t nDown = packNormal(0, -1, 0);
    writeVert(verts, idx++, {0, (int16_t)-halfH, 0, nDown}, rgba); // 3*SEGS+1
    // Bottom cap ring (normal down)
    for (int i = 0; i < SEGS; ++i) {
      float a = 2.0f * (float)M_PI * (float)i / (float)SEGS;
      writeVert(verts, idx++,
        {(int16_t)(radius * cosf(a)), (int16_t)-halfH, (int16_t)(radius * sinf(a)), nDown}, rgba);
    }

    rspq_block_begin();
    t3d_vert_load(verts, 0, COUNT);

    // Side
    for (int i = 0; i < SEGS; ++i) {
      int next = (i + 1) % SEGS;
      // top ring: 0..SEGS-1, bottom ring: SEGS..2*SEGS-1
      t3d_tri_draw(i, SEGS + i, SEGS + next);
      t3d_tri_draw(i, SEGS + next, next);
    }
    // Top cap (fan)
    int topCenter = 2 * SEGS;
    int topRingBase = topCenter + 1;
    for (int i = 0; i < SEGS; ++i) {
      int next = (i + 1) % SEGS;
      t3d_tri_draw(topCenter, topRingBase + next, topRingBase + i);
    }
    // Bottom cap (fan, reversed winding)
    int botCenter = 3 * SEGS + 1;
    int botRingBase = botCenter + 1;
    for (int i = 0; i < SEGS; ++i) {
      int next = (i + 1) % SEGS;
      t3d_tri_draw(botCenter, botRingBase + i, botRingBase + next);
    }
    t3d_tri_sync();
    *outBlock = rspq_block_end();
  }

  void genCone(float radius, float halfH, uint32_t rgba,
               T3DVertPacked **outVerts, uint16_t *outVertCount,
               rspq_block_t **outBlock)
  {
    constexpr int SEGS = 10;
    // tip + side ring + base center + base ring = 1 + SEGS + 1 + SEGS = 2*SEGS+2
    uint16_t COUNT = 2 * SEGS + 2;
    auto *verts = allocVerts(COUNT);
    *outVerts = verts;
    *outVertCount = COUNT;

    uint32_t idx = 0;
    // Tip — slant normal up.
    uint16_t nUp = packNormal(0, 1, 0);
    writeVert(verts, idx++, {0, (int16_t)halfH, 0, nUp}, rgba); // 0
    // Side ring (normals tilt up + outward)
    float slope = halfH / fmaxf(radius, 0.0001f);
    for (int i = 0; i < SEGS; ++i) {
      float a = 2.0f * (float)M_PI * (float)i / (float)SEGS;
      float c = cosf(a), s = sinf(a);
      float nx = c, nz = s, ny = slope;
      float invLen = 1.0f / sqrtf(nx*nx + ny*ny + nz*nz);
      uint16_t n = packNormal(nx*invLen, ny*invLen, nz*invLen);
      writeVert(verts, idx++,
        {(int16_t)(radius * c), (int16_t)-halfH, (int16_t)(radius * s), n}, rgba);
    }
    // Base center + ring (normal down)
    uint16_t nDown = packNormal(0, -1, 0);
    writeVert(verts, idx++, {0, (int16_t)-halfH, 0, nDown}, rgba); // SEGS+1
    for (int i = 0; i < SEGS; ++i) {
      float a = 2.0f * (float)M_PI * (float)i / (float)SEGS;
      writeVert(verts, idx++,
        {(int16_t)(radius * cosf(a)), (int16_t)-halfH, (int16_t)(radius * sinf(a)), nDown}, rgba);
    }

    rspq_block_begin();
    t3d_vert_load(verts, 0, COUNT);
    // Side fans
    for (int i = 0; i < SEGS; ++i) {
      int next = (i + 1) % SEGS;
      t3d_tri_draw(0, 1 + next, 1 + i);
    }
    // Base fan
    int baseCenter = SEGS + 1;
    int baseRingBase = baseCenter + 1;
    for (int i = 0; i < SEGS; ++i) {
      int next = (i + 1) % SEGS;
      t3d_tri_draw(baseCenter, baseRingBase + i, baseRingBase + next);
    }
    t3d_tri_sync();
    *outBlock = rspq_block_end();
  }

  void genSphere(float radius, uint32_t rgba,
                 T3DVertPacked **outVerts, uint16_t *outVertCount,
                 rspq_block_t **outBlock)
  {
    constexpr int LON = 8;   // longitude segments
    constexpr int LAT = 4;   // latitude rings between poles
    // 2 poles + LAT * LON ring verts = 2 + LAT*LON
    uint16_t COUNT = 2 + LAT * LON;
    auto *verts = allocVerts(COUNT);
    *outVerts = verts;
    *outVertCount = COUNT;

    uint32_t idx = 0;
    // North pole
    uint16_t nUp = packNormal(0, 1, 0);
    writeVert(verts, idx++, {0, (int16_t)radius, 0, nUp}, rgba); // 0
    // South pole
    uint16_t nDown = packNormal(0, -1, 0);
    writeVert(verts, idx++, {0, (int16_t)-radius, 0, nDown}, rgba); // 1

    // Latitude rings (excluding poles)
    for (int r = 1; r <= LAT; ++r) {
      float phi = (float)M_PI * (float)r / (float)(LAT + 1); // 0..pi
      float ringR = radius * sinf(phi);
      float y = radius * cosf(phi);
      for (int i = 0; i < LON; ++i) {
        float a = 2.0f * (float)M_PI * (float)i / (float)LON;
        float c = cosf(a), s = sinf(a);
        float nx = sinf(phi) * c;
        float ny = cosf(phi);
        float nz = sinf(phi) * s;
        uint16_t n = packNormal(nx, ny, nz);
        writeVert(verts, idx++,
          {(int16_t)(ringR * c), (int16_t)y, (int16_t)(ringR * s), n}, rgba);
      }
    }

    rspq_block_begin();
    t3d_vert_load(verts, 0, COUNT);
    int firstRing = 2;
    // Top cap (pole 0 to first ring)
    for (int i = 0; i < LON; ++i) {
      int next = (i + 1) % LON;
      t3d_tri_draw(0, firstRing + i, firstRing + next);
    }
    // Middle bands
    for (int r = 0; r < LAT - 1; ++r) {
      int rowA = firstRing + r * LON;
      int rowB = rowA + LON;
      for (int i = 0; i < LON; ++i) {
        int next = (i + 1) % LON;
        t3d_tri_draw(rowA + i, rowB + i, rowB + next);
        t3d_tri_draw(rowA + i, rowB + next, rowA + next);
      }
    }
    // Bottom cap (last ring to pole 1)
    int lastRing = firstRing + (LAT - 1) * LON;
    for (int i = 0; i < LON; ++i) {
      int next = (i + 1) % LON;
      t3d_tri_draw(1, lastRing + next, lastRing + i);
    }
    t3d_tri_sync();
    *outBlock = rspq_block_end();
  }

  void genCapsule(float radius, float innerHalfH, uint32_t rgba,
                  T3DVertPacked **outVerts, uint16_t *outVertCount,
                  rspq_block_t **outBlock)
  {
    // For simplicity: render as a cylinder body + sphere caps approximation.
    // Use a single-pass mesh: cylindrical side + 2 hemisphere caps.
    constexpr int LON = 8;
    constexpr int CAP_LAT = 2; // latitudes per hemisphere (excl. pole)
    // verts:
    //  cyl top ring (LON) + cyl bot ring (LON)              -> side
    //  top cap rings: CAP_LAT * LON + 1 pole
    //  bot cap rings: CAP_LAT * LON + 1 pole
    uint16_t COUNT = 2 * LON + 2 * (CAP_LAT * LON) + 2;
    auto *verts = allocVerts(COUNT);
    *outVerts = verts;
    *outVertCount = COUNT;

    uint32_t idx = 0;
    int16_t Y = (int16_t)innerHalfH;
    // Cyl rings (normal outward, ny=0)
    pushRing(verts, idx, LON, radius,  Y, 0.0f, rgba); // 0..LON-1
    pushRing(verts, idx, LON, radius, -Y, 0.0f, rgba); // LON..2*LON-1

    // Top hemisphere rings (above Y)
    int topRingBase = 2 * LON;
    for (int r = 1; r <= CAP_LAT; ++r) {
      float phi = (float)M_PI * 0.5f * (float)r / (float)(CAP_LAT + 1);
      float ringR = radius * cosf(phi);
      float yOff = radius * sinf(phi);
      for (int i = 0; i < LON; ++i) {
        float a = 2.0f * (float)M_PI * (float)i / (float)LON;
        float c = cosf(a), s = sinf(a);
        uint16_t n = packNormal(cosf(phi)*c, sinf(phi), cosf(phi)*s);
        writeVert(verts, idx++,
          {(int16_t)(ringR * c), (int16_t)(Y + yOff), (int16_t)(ringR * s), n}, rgba);
      }
    }
    int topPole = idx;
    writeVert(verts, idx++, {0, (int16_t)(Y + (int16_t)radius), 0, packNormal(0,1,0)}, rgba);

    // Bottom hemisphere rings (below -Y)
    int botRingBase = idx;
    for (int r = 1; r <= CAP_LAT; ++r) {
      float phi = (float)M_PI * 0.5f * (float)r / (float)(CAP_LAT + 1);
      float ringR = radius * cosf(phi);
      float yOff = radius * sinf(phi);
      for (int i = 0; i < LON; ++i) {
        float a = 2.0f * (float)M_PI * (float)i / (float)LON;
        float c = cosf(a), s = sinf(a);
        uint16_t n = packNormal(cosf(phi)*c, -sinf(phi), cosf(phi)*s);
        writeVert(verts, idx++,
          {(int16_t)(ringR * c), (int16_t)(-Y - yOff), (int16_t)(ringR * s), n}, rgba);
      }
    }
    int botPole = idx;
    writeVert(verts, idx++, {0, (int16_t)(-Y - (int16_t)radius), 0, packNormal(0,-1,0)}, rgba);

    rspq_block_begin();
    t3d_vert_load(verts, 0, COUNT);
    // Cylinder side
    for (int i = 0; i < LON; ++i) {
      int next = (i + 1) % LON;
      t3d_tri_draw(i, LON + i, LON + next);
      t3d_tri_draw(i, LON + next, next);
    }
    // Top hemisphere: cyl-top-ring (0..LON-1) -> first top ring (topRingBase)
    // -> ... -> last top ring -> top pole
    for (int r = 0; r <= CAP_LAT; ++r) {
      int rowA, rowB;
      if (r == 0)            rowA = 0;                          // cyl top
      else                   rowA = topRingBase + (r-1) * LON;
      if (r == CAP_LAT)      rowB = -1;                         // pole next
      else                   rowB = topRingBase + r * LON;

      if (rowB >= 0) {
        for (int i = 0; i < LON; ++i) {
          int next = (i + 1) % LON;
          t3d_tri_draw(rowA + i, rowB + i, rowB + next);
          t3d_tri_draw(rowA + i, rowB + next, rowA + next);
        }
      } else {
        // ring -> pole fan
        for (int i = 0; i < LON; ++i) {
          int next = (i + 1) % LON;
          t3d_tri_draw(rowA + i, topPole, rowA + next);
        }
      }
    }
    // Bottom hemisphere: cyl-bot-ring (LON..2*LON-1) -> bot rings -> bot pole
    for (int r = 0; r <= CAP_LAT; ++r) {
      int rowA, rowB;
      if (r == 0)            rowA = LON;                        // cyl bot
      else                   rowA = botRingBase + (r-1) * LON;
      if (r == CAP_LAT)      rowB = -1;
      else                   rowB = botRingBase + r * LON;

      if (rowB >= 0) {
        for (int i = 0; i < LON; ++i) {
          int next = (i + 1) % LON;
          // Reverse winding for downward-facing strips
          t3d_tri_draw(rowA + i, rowB + next, rowB + i);
          t3d_tri_draw(rowA + i, rowA + next, rowB + next);
        }
      } else {
        for (int i = 0; i < LON; ++i) {
          int next = (i + 1) % LON;
          t3d_tri_draw(rowA + i, rowA + next, botPole);
        }
      }
    }
    t3d_tri_sync();
    *outBlock = rspq_block_end();
  }
}

namespace P64::Comp
{
  void Primitive::initDelete(Object &obj, Primitive* data, void* initData_)
  {
    if (initData_ == nullptr) {
      // destroy
      if (data->drawBlock) {
        rspq_block_free(data->drawBlock);
        data->drawBlock = nullptr;
      }
      if (data->verts) {
        free_uncached(data->verts);
        data->verts = nullptr;
      }
      data->matFP.~RingMat4FP();
      return;
    }

    new (data) Primitive();

    auto *initData = (InitData*)initData_;
    data->shape       = (ShapeType)initData->shape;
    data->layerIdx    = initData->layerIdx;
    data->color[0]    = initData->r;
    data->color[1]    = initData->g;
    data->color[2]    = initData->b;
    data->color[3]    = initData->a;
    data->halfExtend[0] = initData->halfX;
    data->halfExtend[1] = initData->halfY;
    data->halfExtend[2] = initData->halfZ;

    uint32_t rgba = ((uint32_t)data->color[0] << 24)
                  | ((uint32_t)data->color[1] << 16)
                  | ((uint32_t)data->color[2] << 8)
                  |  (uint32_t)data->color[3];

    float hx = data->halfExtend[0];
    float hy = data->halfExtend[1];
    float hz = data->halfExtend[2];

    switch(data->shape) {
      case ShapeType::Box:
        genBox(hx, hy, hz, rgba, &data->verts, &data->vertCount, &data->drawBlock);
        break;
      case ShapeType::Sphere:
        genSphere(hy, rgba, &data->verts, &data->vertCount, &data->drawBlock);
        break;
      case ShapeType::Cylinder:
        genCylinder(hx, hy, rgba, &data->verts, &data->vertCount, &data->drawBlock);
        break;
      case ShapeType::Capsule:
        genCapsule(hx, hy, rgba, &data->verts, &data->vertCount, &data->drawBlock);
        break;
      case ShapeType::Cone:
        genCone(hx, hy, rgba, &data->verts, &data->vertCount, &data->drawBlock);
        break;
      case ShapeType::Pyramid:
        genPyramid(hx, hy, hz, rgba, &data->verts, &data->vertCount, &data->drawBlock);
        break;
      case ShapeType::Plane:
        genPlane(hx, hz, rgba, &data->verts, &data->vertCount, &data->drawBlock);
        break;
    }
    (void)obj;
  }

  void Primitive::draw(Object &obj, Primitive* data, [[maybe_unused]] float deltaTime)
  {
    if (!data->drawBlock) return;

    auto mat = data->matFP.getNext();
    t3d_mat4fp_from_srt(mat, obj.scale, obj.rot, obj.pos);

    if (data->layerIdx) DrawLayer::use3D(data->layerIdx);

    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    rdpq_mode_zbuf(true, true);
    t3d_state_set_drawflags((T3DDrawFlags)(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));

    t3d_matrix_push(mat);
    rspq_block_run(data->drawBlock);
    t3d_matrix_pop(1);

    if (data->layerIdx) DrawLayer::useDefault();
  }
}
