/**
* Path runtime support: shared frame type and flag registry for branching paths.
* Companion to scene/components/path.h. User code reads/writes flags here to
* drive branch selection at fork points along an authored Path component.
*/
#pragma once
#include <libdragon.h>

namespace P64
{
  struct PathFrame
  {
    fm_vec3_t pos;
    fm_vec3_t fwd;
    fm_vec3_t up;
    fm_vec3_t right;
  };

  namespace PathRT
  {
    constexpr uint16_t MAX_FLAGS = 32;

    // Stable hashed id for a flag name. Use at compile time or at registration.
    constexpr uint16_t hashFlag(const char* s)
    {
      uint32_t h = 2166136261u;
      while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
      return (uint16_t)((h ^ (h >> 16)) & 0xFFFFu);
    }

    void  setFlag(uint16_t id, float v);
    float getFlag(uint16_t id);

    inline void  setFlag(const char* name, float v) { setFlag(hashFlag(name), v); }
    inline float getFlag(const char* name)          { return getFlag(hashFlag(name)); }

    // Optional override: pin the active branch group regardless of conditions.
    // 0 = condition-driven (default).
    void    setActiveBranch(uint8_t branchId);
    uint8_t getActiveBranch();
  }
}
