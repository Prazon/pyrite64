/**
* Path component.
* Authors a Catmull-Rom spline through a list of control points in the
* owning Object's local space, with optional alternate-route branches.
* Designed for on-rails camera/ship paths (Star Fox-style), patrol routes,
* and other "follow this curve" use cases.
*
* Branching model: each control point belongs to a branch group (0 =
* trunk; 1..N = alternates). The engine builds a separate LUT per group;
* `sample()` resolves the active group at call time from PathRT
* (manual override → first matching condition → trunk fallback) and
* samples that group's LUT.
*
* Frames are precomputed at init via parallel transport so loops and dives
* don't NaN. Sample by arc length s; the returned PathFrame is in Object
* local space (compose with obj.pos/rot for world space).
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"
#include "scene/path.h"

namespace P64::Comp
{
  struct Path
  {
    static constexpr uint32_t ID = 18;
    static constexpr uint32_t MAX_GROUPS = 8;

    struct CtrlPoint
    {
      fm_vec3_t pos;
      float     tension;
      uint8_t   branchId;    // 0 = trunk; nonzero = alternate route
      uint8_t   flags;
      uint8_t   _pad[2];
    };

    // Branch condition. If predicate (flagId op value) is true, this branch's
    // branchId is preferred when sample() resolves the active group.
    struct Branch
    {
      uint16_t fromIdx;       // advisory only (for inspector visualization)
      uint8_t  branchId;
      uint8_t  op;            // 0=eq 1=ne 2=lt 3=le 4=gt 5=ge
      uint16_t flagId;
      uint16_t _pad;
      float    value;
    };

    struct LUTSample
    {
      fm_vec3_t pos;
      fm_vec3_t fwd;
      fm_vec3_t up;
      fm_vec3_t right;
    };

    // Per-group precomputed spline (one per used branchId).
    struct Group
    {
      LUTSample* lut;
      float*     arc;
      uint16_t   lutCount;
      uint8_t    branchId;
      uint8_t    _pad;
      float      totalLength;
    };

    CtrlPoint*  points{nullptr};
    Branch*     branches{nullptr};
    Group*      groups{nullptr};
    uint16_t    pointCount{0};
    uint16_t    branchCount{0};
    uint16_t    groupCount{0};
    uint16_t    _pad{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Path);
    }

    static void initDelete(Object& obj, Path* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] Path* data,
                       [[maybe_unused]] float deltaTime) {}

    // Returns total length of the currently-active branch group's spline.
    static float length(const Path& path);

    // Sample the active branch group's spline at arc length s. The active
    // group is resolved from PathRT::getActiveBranch() (nonzero override),
    // else first branch whose condition holds, else 0 (trunk).
    static PathFrame sample(const Path& path, float s);

    // Resolve which group is active right now, given current PathRT state.
    static uint8_t resolveActiveGroup(const Path& path);
  };
}
