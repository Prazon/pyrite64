/**
* Path component implementation.
* At init, buckets control points by branchId, builds a Catmull-Rom spline
* + arc-length LUT per group, and runs parallel transport across LUT samples
* so up/right axes stay continuous through climbs and rolls. At sample time
* resolves the active branch group via PathRT and reads from that group's LUT.
*/
#include "scene/components/path.h"
#include "scene/path.h"
#include <stdlib.h>
#include <string.h>

namespace
{
  // Binary blob written by the editor side (compPath.cpp::build).
  // Header followed by pointCount CtrlPointInit entries, then branchCount
  // BranchInit entries. Keep in sync with the editor.
  struct __attribute__((packed)) InitHeader
  {
    uint16_t pointCount;
    uint16_t branchCount;
    uint16_t lutPerSegment;   // sample density per spline segment, typical 8-16
    uint16_t _pad;
  };
  static_assert(sizeof(InitHeader) == 8);

  struct __attribute__((packed)) CtrlPointInit
  {
    float    pos[3];
    float    tension;
    uint8_t  branchId;
    uint8_t  flags;
    uint8_t  _pad[2];
  };
  static_assert(sizeof(CtrlPointInit) == 20);

  struct __attribute__((packed)) BranchInit
  {
    uint16_t fromIdx;
    uint8_t  branchId;
    uint8_t  op;
    uint16_t flagId;
    uint16_t _pad;
    float    value;
  };
  static_assert(sizeof(BranchInit) == 12);

  // Catmull-Rom interpolation (uniform). Tension t controls the basis matrix;
  // 0.5 is the standard centripetal-ish form most authoring tools use.
  fm_vec3_t catmullRom(const fm_vec3_t& p0, const fm_vec3_t& p1,
                       const fm_vec3_t& p2, const fm_vec3_t& p3,
                       float t, float tension)
  {
    float t2 = t * t;
    float t3 = t2 * t;
    fm_vec3_t r;
    for (int i = 0; i < 3; ++i) {
      float a = -tension * p0.v[i] + (2.0f - tension) * p1.v[i]
              + (tension - 2.0f) * p2.v[i] + tension * p3.v[i];
      float b = 2.0f * tension * p0.v[i] + (tension - 3.0f) * p1.v[i]
              + (3.0f - 2.0f * tension) * p2.v[i] - tension * p3.v[i];
      float c = -tension * p0.v[i] + tension * p2.v[i];
      float d = p1.v[i];
      r.v[i] = a * t3 + b * t2 + c * t + d;
    }
    return r;
  }

  fm_vec3_t seedUp(const fm_vec3_t& fwd)
  {
    fm_vec3_t up;
    float ax = fwd.v[0] < 0 ? -fwd.v[0] : fwd.v[0];
    float ay = fwd.v[1] < 0 ? -fwd.v[1] : fwd.v[1];
    float az = fwd.v[2] < 0 ? -fwd.v[2] : fwd.v[2];
    if (ay <= ax && ay <= az) { up = {{0, 1, 0}}; }
    else if (ax <= az)        { up = {{1, 0, 0}}; }
    else                      { up = {{0, 0, 1}}; }
    return up;
  }

  void normalizeOrFallback(fm_vec3_t* v, const fm_vec3_t& fallback)
  {
    if (fm_vec3_len2(v) < 1e-8f) { *v = fallback; return; }
    fm_vec3_t n; fm_vec3_norm(&n, v); *v = n;
  }

  bool evalCondition(uint8_t op, float a, float b)
  {
    // 0=eq 1=ne 2=lt 3=le 4=gt 5=ge
    if (op == 0) return a == b;
    if (op == 1) return a != b;
    if (op == 2) return a <  b;
    if (op == 3) return a <= b;
    if (op == 4) return a >  b;
    if (op == 5) return a >= b;
    return false;
  }

  // Build one group's LUT from the supplied indices into the points array.
  // Returns true on success. The caller owns `out` and must free its lut/arc.
  bool buildGroup(P64::Comp::Path& path, uint8_t branchId,
                  const uint16_t* idx, uint16_t idxCount,
                  uint16_t lutPerSeg, P64::Comp::Path::Group* out)
  {
    using LUT = P64::Comp::Path::LUTSample;
    out->lut = nullptr;
    out->arc = nullptr;
    out->lutCount = 0;
    out->branchId = branchId;
    out->totalLength = 0.0f;
    if (idxCount < 2) return false;

    auto getCtrl = [&](int i) -> fm_vec3_t {
      if (i < 0)              return path.points[idx[0]].pos;
      if (i >= (int)idxCount) return path.points[idx[idxCount - 1]].pos;
      return path.points[idx[i]].pos;
    };

    uint16_t segCount = idxCount - 1;
    uint16_t lutCount = segCount * lutPerSeg + 1;
    LUT*   lut = (LUT*)malloc(sizeof(LUT) * lutCount);
    float* arc = (float*)malloc(sizeof(float) * lutCount);
    if (!lut || !arc) {
      if (lut) free(lut);
      if (arc) free(arc);
      return false;
    }

    int writeIdx = 0;
    for (uint16_t s = 0; s < segCount; ++s) {
      fm_vec3_t p0 = getCtrl((int)s - 1);
      fm_vec3_t p1 = getCtrl((int)s);
      fm_vec3_t p2 = getCtrl((int)s + 1);
      fm_vec3_t p3 = getCtrl((int)s + 2);
      float tension = path.points[idx[s]].tension;
      if (tension <= 0.0f) tension = 0.5f;

      uint16_t kEnd = (s == segCount - 1) ? lutPerSeg : (lutPerSeg - 1);
      for (uint16_t k = 0; k <= kEnd; ++k) {
        float t = (float)k / (float)lutPerSeg;
        lut[writeIdx].pos = catmullRom(p0, p1, p2, p3, t, tension);
        ++writeIdx;
      }
    }

    for (uint16_t i = 0; i < lutCount; ++i) {
      fm_vec3_t fwd;
      if (i + 1 < lutCount) {
        fm_vec3_sub(&fwd, &lut[i + 1].pos, &lut[i].pos);
      } else {
        fm_vec3_sub(&fwd, &lut[i].pos, &lut[i - 1].pos);
      }
      fm_vec3_t fb = {{0, 0, 1}};
      normalizeOrFallback(&fwd, fb);
      lut[i].fwd = fwd;
    }

    {
      fm_vec3_t up0 = seedUp(lut[0].fwd);
      fm_vec3_t r;
      fm_vec3_cross(&r, &up0, &lut[0].fwd);
      fm_vec3_t fb = {{1, 0, 0}};
      normalizeOrFallback(&r, fb);
      fm_vec3_t up;
      fm_vec3_cross(&up, &lut[0].fwd, &r);
      fm_vec3_t fbu = {{0, 1, 0}};
      normalizeOrFallback(&up, fbu);
      lut[0].up    = up;
      lut[0].right = r;
    }

    for (uint16_t i = 1; i < lutCount; ++i) {
      fm_vec3_t prevUp = lut[i - 1].up;
      fm_vec3_t fwd    = lut[i].fwd;

      float dot = prevUp.v[0]*fwd.v[0] + prevUp.v[1]*fwd.v[1] + prevUp.v[2]*fwd.v[2];
      fm_vec3_t up;
      up.v[0] = prevUp.v[0] - dot * fwd.v[0];
      up.v[1] = prevUp.v[1] - dot * fwd.v[1];
      up.v[2] = prevUp.v[2] - dot * fwd.v[2];
      fm_vec3_t fbu = seedUp(fwd);
      normalizeOrFallback(&up, fbu);

      fm_vec3_t r;
      fm_vec3_cross(&r, &up, &fwd);
      fm_vec3_t fbr = {{1, 0, 0}};
      normalizeOrFallback(&r, fbr);

      fm_vec3_cross(&up, &fwd, &r);
      normalizeOrFallback(&up, fbu);

      lut[i].up    = up;
      lut[i].right = r;
    }

    arc[0] = 0.0f;
    for (uint16_t i = 1; i < lutCount; ++i) {
      fm_vec3_t d;
      fm_vec3_sub(&d, &lut[i].pos, &lut[i - 1].pos);
      arc[i] = arc[i - 1] + fm_vec3_len(&d);
    }

    out->lut         = lut;
    out->arc         = arc;
    out->lutCount    = lutCount;
    out->totalLength = arc[lutCount - 1];
    return true;
  }
}

namespace P64::Comp
{
  void Path::initDelete(Object& obj, Path* data, void* initData_)
  {
    (void)obj;
    if (initData_ == nullptr) {
      if (data->groups) {
        for (uint16_t g = 0; g < data->groupCount; ++g) {
          if (data->groups[g].lut) free(data->groups[g].lut);
          if (data->groups[g].arc) free(data->groups[g].arc);
        }
        free(data->groups);
        data->groups = nullptr;
      }
      if (data->points)   { free(data->points);   data->points   = nullptr; }
      if (data->branches) { free(data->branches); data->branches = nullptr; }
      data->pointCount = data->branchCount = data->groupCount = 0;
      return;
    }

    auto header = (InitHeader*)initData_;
    auto pointSrc  = (CtrlPointInit*)((uint8_t*)initData_ + sizeof(InitHeader));
    auto branchSrc = (BranchInit*)(pointSrc + header->pointCount);

    data->pointCount  = header->pointCount;
    data->branchCount = header->branchCount;
    data->groupCount  = 0;
    data->points      = nullptr;
    data->branches    = nullptr;
    data->groups      = nullptr;

    if (data->pointCount < 2) return;

    data->points = (CtrlPoint*)malloc(sizeof(CtrlPoint) * data->pointCount);
    for (uint16_t i = 0; i < data->pointCount; ++i) {
      auto& src = pointSrc[i];
      auto& dst = data->points[i];
      dst.pos.v[0]  = src.pos[0];
      dst.pos.v[1]  = src.pos[1];
      dst.pos.v[2]  = src.pos[2];
      dst.tension   = (src.tension > 0.0f) ? src.tension : 0.5f;
      dst.branchId  = src.branchId;
      dst.flags     = src.flags;
    }

    if (data->branchCount > 0) {
      data->branches = (Branch*)malloc(sizeof(Branch) * data->branchCount);
      for (uint16_t i = 0; i < data->branchCount; ++i) {
        auto& src = branchSrc[i];
        auto& dst = data->branches[i];
        dst.fromIdx  = src.fromIdx;
        dst.branchId = src.branchId;
        dst.op       = src.op;
        dst.flagId   = src.flagId;
        dst.value    = src.value;
      }
    }

    uint16_t lutPerSeg = header->lutPerSegment ? header->lutPerSegment : 12;

    // Bucket point indices by branchId. Flat O(N*MAX_GROUPS) — fine since
    // MAX_GROUPS is small and we only do this once at scene load.
    uint16_t bucketCounts[MAX_GROUPS]{};
    for (uint16_t i = 0; i < data->pointCount; ++i) {
      uint8_t b = data->points[i].branchId;
      if (b < MAX_GROUPS) bucketCounts[b]++;
    }
    uint16_t usedGroups = 0;
    for (uint8_t b = 0; b < MAX_GROUPS; ++b) {
      if (bucketCounts[b] >= 2) usedGroups++;
    }
    if (usedGroups == 0) return;

    data->groups = (Group*)malloc(sizeof(Group) * usedGroups);

    uint16_t bufStorage[MAX_GROUPS][64];
    uint16_t bufLen[MAX_GROUPS]{};
    bool overflow = false;
    for (uint16_t i = 0; i < data->pointCount; ++i) {
      uint8_t b = data->points[i].branchId;
      if (b >= MAX_GROUPS) continue;
      if (bufLen[b] >= 64) { overflow = true; continue; }
      bufStorage[b][bufLen[b]++] = i;
    }
    (void)overflow; // silently truncates — stay within 64 points per group

    uint16_t writeG = 0;
    for (uint8_t b = 0; b < MAX_GROUPS && writeG < usedGroups; ++b) {
      if (bucketCounts[b] < 2) continue;
      uint16_t use = bufLen[b];
      if (use > 64) use = 64;
      Group g{};
      if (buildGroup(*data, b, bufStorage[b], use, lutPerSeg, &g)) {
        data->groups[writeG++] = g;
      }
    }
    data->groupCount = writeG;
  }

  uint8_t Path::resolveActiveGroup(const Path& path)
  {
    uint8_t override_ = PathRT::getActiveBranch();
    if (override_ != 0) {
      for (uint16_t g = 0; g < path.groupCount; ++g) {
        if (path.groups[g].branchId == override_) return override_;
      }
    }
    for (uint16_t b = 0; b < path.branchCount; ++b) {
      const Branch& br = path.branches[b];
      float lhs = PathRT::getFlag(br.flagId);
      if (evalCondition(br.op, lhs, br.value)) {
        for (uint16_t g = 0; g < path.groupCount; ++g) {
          if (path.groups[g].branchId == br.branchId) return br.branchId;
        }
      }
    }
    return 0;
  }

  float Path::length(const Path& path)
  {
    if (path.groupCount == 0) return 0.0f;
    uint8_t active = resolveActiveGroup(path);
    for (uint16_t g = 0; g < path.groupCount; ++g) {
      if (path.groups[g].branchId == active) return path.groups[g].totalLength;
    }
    return path.groups[0].totalLength;
  }

  PathFrame Path::sample(const Path& path, float s)
  {
    PathFrame out{};
    if (path.groupCount == 0) {
      out.pos = {{0, 0, 0}};
      out.fwd = {{0, 0, 1}};
      out.up  = {{0, 1, 0}};
      out.right = {{1, 0, 0}};
      return out;
    }

    uint8_t active = resolveActiveGroup(path);
    const Group* g = &path.groups[0];
    for (uint16_t i = 0; i < path.groupCount; ++i) {
      if (path.groups[i].branchId == active) { g = &path.groups[i]; break; }
    }

    if (s < 0.0f) s = 0.0f;
    if (s > g->totalLength) s = g->totalLength;

    uint16_t hi = g->lutCount - 1;
    uint16_t i = 0;
    for (; i < hi; ++i) {
      if (g->arc[i + 1] >= s) break;
    }

    float segStart = g->arc[i];
    float segEnd   = g->arc[i + 1];
    float local;
    if (segEnd - segStart > 1e-6f) {
      local = (s - segStart) / (segEnd - segStart);
    } else {
      local = 0.0f;
    }

    const LUTSample& a = g->lut[i];
    const LUTSample& b = g->lut[i + 1];

    out.pos.v[0] = a.pos.v[0] + (b.pos.v[0] - a.pos.v[0]) * local;
    out.pos.v[1] = a.pos.v[1] + (b.pos.v[1] - a.pos.v[1]) * local;
    out.pos.v[2] = a.pos.v[2] + (b.pos.v[2] - a.pos.v[2]) * local;

    out.fwd   = a.fwd;
    out.up    = a.up;
    out.right = a.right;
    return out;
  }
}
