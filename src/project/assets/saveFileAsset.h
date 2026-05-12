/**
* @copyright 2026 - Prazon
* @license MIT
*
* On-disk schema for a .p64save save-file asset. A save-file asset declares a
* named "group" of typed fields. The project builder collects every save asset
* and flat-allocates each field a slot index inside the single shared
* P64::Save EEPROM buffer, then emits typed accessors into
* <project>/src/p64/saveTable.{h,cpp}.
*
* Slot indices are NOT stored in the asset. They are assigned at build time
* (in deterministic order: groups sorted by uuid, fields in declared order)
* so a field can be renamed without invalidating others. Adding a field at
* the end is safe; reordering or removing earlier fields shifts later slots
* and silently changes what an existing EEPROM image means.
*/
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "json.hpp"

namespace Project::Assets
{
  struct SaveFileAsset
  {
    enum FieldType : int32_t {
      FT_INT    = 0,
      FT_FLOAT  = 1,
      FT_BOOL   = 2,
      FT_STRING = 3,
      FT_VEC2   = 4,
      FT_VEC3   = 5,
    };

    struct Field
    {
      std::string name{};
      FieldType   type{FT_INT};

      // Defaults. Only the slot matching `type` is meaningful; the others
      // are left at their zero value but kept on-disk so toggling type
      // doesn't immediately destroy what the user typed.
      int32_t  defInt{0};
      float    defFloat{0.0f};
      bool     defBool{false};
      std::string defString{};   // truncated to stringLen chars
      float    defVec[3]{0,0,0};

      // For FT_STRING only. Stored chars (excluding NUL); packs into
      // ceil(stringLen / 4) slots, NUL-padded in the last slot.
      // Clamped to [1..32] in the inspector.
      uint32_t stringLen{8};
    };

    uint64_t uuid{0};
    int32_t  version{1};

    // Display name for the generated namespace. Defaults to a sanitized
    // form of the asset's filename when the asset is created.
    std::string groupName{};

    std::vector<Field> fields{};

    // Slot count this asset would consume. Used by the build for capacity
    // checks and by the inspector for the "uses N/M slots" label.
    uint32_t slotsUsed() const;
    static uint32_t fieldSlotCount(const Field &f);

    [[nodiscard]] std::string serialize() const;
    void deserialize(const std::string &doc);
  };
}
