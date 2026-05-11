/**
* P64::Save implementation.
* Single eepfs file "save.dat" of (slotCount * 4) bytes. The file is
* registered with checksum + backup so a corrupted write is recoverable.
*
* The RAM buffer is malloc'd at init and freed at shutdown. setX writes
* to the buffer; commit() pushes the whole buffer to eepfs in one
* eepfs_write call. That matches PICO-8's dset/dget semantics where
* writes don't hit "media" until cstore-equivalent.
*/
#include "save/save.h"

#include <libdragon.h>
#include <malloc.h>
#include <string.h>

namespace
{
  constexpr const char *SAVE_PATH = "/save.dat";

  int32_t *buffer{nullptr};
  uint32_t slotCount{0};
  bool     initialized{false};

  // 4K EEPROM has 504 bytes free; 16K has 2040. Reserve a generous
  // margin for checksum + backup overhead (~16 bytes per file).
  constexpr uint32_t MAX_SLOTS_4K  = 120;
  constexpr uint32_t MAX_SLOTS_16K = 500;

  uint32_t slotCap()
  {
    eeprom_type_t t = eeprom_present();
    if (t == EEPROM_16K) return MAX_SLOTS_16K;
    if (t == EEPROM_4K)  return MAX_SLOTS_4K;
    return 0;
  }
}

namespace P64::Save
{
  bool init(uint32_t requestedSlots)
  {
    if (initialized) shutdown();
    uint32_t cap = slotCap();
    if (cap == 0) return false;
    if (requestedSlots == 0) return false;
    if (requestedSlots > cap) requestedSlots = cap;

    size_t bytes = (size_t)requestedSlots * sizeof(int32_t);
    buffer = (int32_t*)malloc(bytes);
    if (!buffer) return false;
    memset(buffer, 0, bytes);

    eepfs_entry_t entries[1] = {
      { SAVE_PATH, bytes, /*checksum*/ true, /*backup*/ true }
    };
    int rc = eepfs_init(entries, 1);
    if (rc != EEPFS_ESUCCESS) {
      free(buffer);
      buffer = nullptr;
      return false;
    }

    // Best-effort read: if the file is missing/corrupt, leave the
    // zero-filled buffer in place. The caller can detect "no save yet"
    // by reading any sentinel slot and comparing to default.
    int readRc = eepfs_read(SAVE_PATH, buffer, bytes);
    if (readRc != EEPFS_ESUCCESS) {
      memset(buffer, 0, bytes);
    }

    slotCount   = requestedSlots;
    initialized = true;
    return true;
  }

  uint32_t getSlotCount() { return slotCount; }

  int32_t getInt(uint32_t slot, int32_t def)
  {
    if (!initialized || slot >= slotCount) return def;
    return buffer[slot];
  }

  void setInt(uint32_t slot, int32_t v)
  {
    if (!initialized || slot >= slotCount) return;
    buffer[slot] = v;
  }

  float getFloat(uint32_t slot, float def)
  {
    if (!initialized || slot >= slotCount) return def;
    float f;
    memcpy(&f, &buffer[slot], sizeof(float));
    return f;
  }

  void setFloat(uint32_t slot, float v)
  {
    if (!initialized || slot >= slotCount) return;
    memcpy(&buffer[slot], &v, sizeof(float));
  }

  bool getBool(uint32_t slot, bool def)
  {
    if (!initialized || slot >= slotCount) return def;
    return buffer[slot] != 0;
  }

  void setBool(uint32_t slot, bool v)
  {
    if (!initialized || slot >= slotCount) return;
    buffer[slot] = v ? 1 : 0;
  }

  bool commit()
  {
    if (!initialized) return false;
    size_t bytes = (size_t)slotCount * sizeof(int32_t);
    int rc = eepfs_write(SAVE_PATH, buffer, bytes);
    return rc == EEPFS_ESUCCESS;
  }

  bool reload()
  {
    if (!initialized) return false;
    size_t bytes = (size_t)slotCount * sizeof(int32_t);
    int rc = eepfs_read(SAVE_PATH, buffer, bytes);
    if (rc != EEPFS_ESUCCESS) {
      memset(buffer, 0, bytes);
      return false;
    }
    return true;
  }

  void clearAll()
  {
    if (!initialized) return;
    memset(buffer, 0, (size_t)slotCount * sizeof(int32_t));
  }

  void shutdown()
  {
    if (!initialized) return;
    eepfs_close();
    if (buffer) { free(buffer); buffer = nullptr; }
    slotCount   = 0;
    initialized = false;
  }
}
