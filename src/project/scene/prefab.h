/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <memory>
#include <utility>
#include <vector>

#include "object.h"
#include "../../utils/prop.h"
#include "../component/components.h"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

namespace Project
{
  class Scene;

  // Prefab class variables (Blueprint-actor style). Each variable has a
  // stable uuid (so renames don't break per-instance overrides), a name,
  // a type, and a default value. typeKind values are persisted to disk —
  // do not renumber.
  enum class PrefabVarKind : uint8_t {
    INT       = 0,  // int32_t
    FLOAT     = 1,
    BOOL      = 2,
    VEC3      = 3,
    QUAT      = 4,
    OBJECT_REF = 5, // any P64::Object*, stored as uint64_t object uuid
    PREFAB_REF = 6, // typed Object*, typeArg = prefab uuid
    ASSET_REF  = 7, // reserved for future asset-uuid refs
  };

  struct PrefabVarDef
  {
    uint64_t uuid{};            // stable id used as key in instance override maps
    std::string name{};
    PrefabVarKind kind{PrefabVarKind::INT};
    uint64_t typeArg{0};        // PREFAB_REF: target prefab uuid; ASSET_REF: asset-type tag
    GenericValue defaultValue{};
  };

  class Prefab
  {
    public:
      PROP_U32(uuid);
      Object obj{};

      // Class variables — Blueprint-style typed properties with defaults.
      // Per-instance overrides live on Object::varOverrides keyed by varDef uuid.
      std::vector<PrefabVarDef> variables{};

      // Variant inheritance: when uuidParentPrefab is non-zero, this prefab's
      // effective tree is derived by deserializing the parent prefab's
      // serialized obj, applying `patchOps` (RFC 6902 JSON Patch) on top,
      // then deserializing the result into `obj`. Non-variant prefabs leave
      // both fields default and serialize their full obj tree as before.
      PROP_U64(uuidParentPrefab);
      nlohmann::json patchOps = nlohmann::json::array();

      bool isVariant() const { return uuidParentPrefab.value != 0; }

      std::string serialize(const Object &obj) const;
      std::string serialize() const { return serialize(obj); }

      void deserialize(const std::string &str);

      // Re-resolve this variant's `obj` from a (resolved) parent prefab.
      // No-op for non-variants. Logs and falls back to a copy of parent.obj
      // if the patch fails to apply cleanly.
      void resolveAgainstParent(const Prefab &parent);

      // Recompute this variant's `patchOps` as the JSON Patch diff between
      // parent's serialized obj and this prefab's current obj. Caller is
      // responsible for ensuring `parent` has been resolved already.
      void rebuildPatchFromCurrent(const Prefab &parent);

      void save();
  };
}