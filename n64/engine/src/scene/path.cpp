/**
* Path runtime support: flag registry for branching paths.
* Tiny linear-probe table sized for MAX_FLAGS unique ids; new ids replace the
* oldest occupied slot if full (rare in practice — designers typically use
* well under MAX_FLAGS named conditions per game).
*/
#include "scene/path.h"

namespace
{
  struct Slot { uint16_t id; uint8_t used; uint8_t _pad; float value; };
  Slot g_flags[P64::PathRT::MAX_FLAGS]{};
  uint16_t g_writeCursor = 0;
  uint8_t  g_activeBranch = 0;

  Slot* findSlot(uint16_t id, bool createIfMissing)
  {
    for (uint16_t i = 0; i < P64::PathRT::MAX_FLAGS; ++i) {
      if (g_flags[i].used && g_flags[i].id == id) return &g_flags[i];
    }
    if (!createIfMissing) return nullptr;
    for (uint16_t i = 0; i < P64::PathRT::MAX_FLAGS; ++i) {
      if (!g_flags[i].used) { g_flags[i].used = 1; g_flags[i].id = id; g_flags[i].value = 0.0f; return &g_flags[i]; }
    }
    Slot* victim = &g_flags[g_writeCursor];
    g_writeCursor = (g_writeCursor + 1) % P64::PathRT::MAX_FLAGS;
    victim->id = id;
    victim->value = 0.0f;
    victim->used = 1;
    return victim;
  }
}

namespace P64::PathRT
{
  void setFlag(uint16_t id, float v)
  {
    Slot* s = findSlot(id, true);
    if (s) s->value = v;
  }

  float getFlag(uint16_t id)
  {
    Slot* s = findSlot(id, false);
    return s ? s->value : 0.0f;
  }

  void    setActiveBranch(uint8_t branchId) { g_activeBranch = branchId; }
  uint8_t getActiveBranch()                  { return g_activeBranch; }
}
