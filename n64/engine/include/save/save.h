/**
* P64::Save — minimal typed key/value persistence on libdragon EEPROM.
*
* The save store is a flat int32 array of `slotCount` entries (4 bytes
* each) backed by a single eepfs file. setInt/setFloat/setBool write
* to an in-RAM buffer; commit() flushes the buffer to EEPROM.
*
* Slot indices are user-managed: the game decides which slot stores
* what. (Pixic mirrors PICO-8's dset/dget contract, so slot indices
* map 1:1 to PICO slot numbers.) Type aliasing is the caller's
* responsibility — setInt(0, x) followed by getFloat(0, ...) returns
* the bit-pattern of x reinterpreted as float.
*
* Capacity: 4K EEPROM has 504 useful bytes (126 int32 slots after
* checksum overhead). 16K EEPROM has 2040 bytes (510 slots). The
* engine clamps to a safe 120 on 4K and 500 on 16K; init() returns
* false if the request exceeds the available space.
*/
#pragma once
#include <libdragon.h>

namespace P64::Save
{
  // Initialize the save subsystem with N int32 slots. Reads existing
  // EEPROM data into the in-RAM buffer if present, otherwise zeroes
  // the buffer. Returns true on success.
  bool init(uint32_t slotCount);

  // Number of allocated slots. 0 if init() has not been called or
  // failed.
  uint32_t getSlotCount();

  int32_t getInt(uint32_t slot, int32_t def = 0);
  void    setInt(uint32_t slot, int32_t v);

  float   getFloat(uint32_t slot, float def = 0.0f);
  void    setFloat(uint32_t slot, float v);

  bool    getBool(uint32_t slot, bool def = false);
  void    setBool(uint32_t slot, bool v);

  // Flush in-RAM buffer to EEPROM. Writes are eventually consistent
  // (libdragon eeprom_is_busy / eeprom_wait_idle). Returns true on
  // success.
  bool commit();

  // Discard the in-RAM buffer and reload from EEPROM. Useful if the
  // game wants a "discard changes" reset at a checkpoint.
  bool reload();

  // Zero the buffer in-place. Does not commit — call commit() to
  // persist the wipe.
  void clearAll();

  // Tear down the subsystem (frees the RAM buffer, closes eepfs).
  // Idempotent.
  void shutdown();
}
