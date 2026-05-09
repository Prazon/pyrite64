/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../scene/varDef.h"

namespace Project::Resource
{
  // Editor-authored RESOURCE_TYPE schema. Lives in a .p64restype JSON file
  // under <project>/assets and is fully editable from the editor's type panel.
  // Mirrors the prefab variable model: every field has a stable uuid that
  // outlives renames, so instance values keyed by uuid stay correct.
  //
  // Header-authored RESOURCE_TYPEs (.h files in P64::Asset::C…) bypass this
  // entirely and continue to flow through Utils::CPP::parseDataStruct.
  class Type
  {
   public:
    uint64_t uuid{0};
    std::string name{};
    std::vector<VarDef> fields{};

    void deserialize(const std::string &json);
    std::string serialize() const;

    const VarDef* findField(uint64_t fieldUuid) const;
    VarDef* findField(uint64_t fieldUuid);
  };
}
