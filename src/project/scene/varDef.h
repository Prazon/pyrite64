/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <string>

#include "../../utils/prop.h"

namespace Project
{
  // Shared variable kind enum used by:
  //   - Prefab class variables (Blueprint-actor style typed properties)
  //   - Editor-authored Resource type fields (.p64restype schemas)
  //
  // Values are persisted to disk in both prefab JSON and resource-type JSON,
  // and are baked into generated runtime headers. Do not renumber existing
  // entries; only append.
  enum class VarKind : uint8_t {
    INT       = 0,  // int32_t
    FLOAT     = 1,
    BOOL      = 2,
    VEC3      = 3,
    QUAT      = 4,
    OBJECT_REF = 5, // any P64::Object*, stored as uint64_t object uuid
    PREFAB_REF = 6, // typed Object*, typeArg = prefab uuid
    ASSET_REF  = 7, // asset uuid reference (typeArg reserved for asset-type tag)
    ARRAY     = 8,  // std::vector<E> where E's VarKind is encoded in typeArg.
                    // Element kind is restricted to scalar kinds in v1
                    // (INT / FLOAT / BOOL); nested arrays / structs are
                    // out of scope until later.
  };

  // Stable definition of a single variable/field. The uuid is the persistent
  // identity that survives renames; instance override maps key on it.
  struct VarDef
  {
    uint64_t uuid{};            // stable id used as key in instance override maps
    std::string name{};
    VarKind kind{VarKind::INT};
    uint64_t typeArg{0};        // PREFAB_REF: target prefab uuid; ASSET_REF: asset-type tag
    GenericValue defaultValue{};
  };
}
