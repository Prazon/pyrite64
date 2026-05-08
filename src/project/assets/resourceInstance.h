/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include "../../utils/codeParser.h"

namespace Project::Resource
{
  // A serialized configuration blob bound to a RESOURCE_TYPE header. The
  // `values` map mirrors the type's parsed `Data` fields by name; values are
  // stored as their string forms (matching BinaryFile::writeAs).
  class Instance
  {
   public:
    uint64_t uuid{0};
    uint64_t typeUuid{0};
    std::unordered_map<std::string, std::string> values{};

    void deserialize(const std::string &json);
    std::string serialize() const;
  };
}
