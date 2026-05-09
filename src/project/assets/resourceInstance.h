/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include "../../utils/codeParser.h"
#include "../../utils/prop.h"

namespace Project::Resource
{
  // A serialized configuration blob bound to a RESOURCE_TYPE.
  //
  // Two storage modes share one Instance struct, picked by the type's author:
  //   - Header-authored types (.h): `values` map keyed by C++ field name,
  //     values stored as their string forms (matching BinaryFile::writeAs).
  //   - Editor-authored types (.p64restype): `uuidValues` map keyed by the
  //     field's stable VarDef uuid so renames in the type editor don't break
  //     existing instance values.
  //
  // Both maps may carry data on disk if a type is migrated; the load path
  // for the active mode is authoritative and the other map is ignored.
  class Instance
  {
   public:
    uint64_t uuid{0};
    uint64_t typeUuid{0};
    std::unordered_map<std::string, std::string> values{};
    std::unordered_map<uint64_t, GenericValue> uuidValues{};

    void deserialize(const std::string &json);
    std::string serialize() const;
  };
}
