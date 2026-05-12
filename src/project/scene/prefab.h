/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "object.h"
#include "varDef.h"
#include "../../utils/prop.h"
#include "../component/components.h"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

namespace Project
{
  class Scene;

  // Prefab class variables (Blueprint-actor style). The kind enum and
  // definition struct are shared with editor-authored Resource type fields.
  // Aliases preserve the historical names used throughout the prefab code.
  using PrefabVarKind = VarKind;
  using PrefabVarDef = VarDef;

  // A single sparse property override on an inherited component (or on an
  // inherited Object's transform when compUuid == 0). Keyed by uuid so it
  // survives parent-side reorders and unrelated edits.
  struct PrefabPropOverride
  {
    uint32_t objUuid{0};   // Object::uuid in the inherited tree
    uint64_t compUuid{0};  // Component::Entry::uuid (0 = property on Object itself, e.g. pos/rot/scale)
    uint64_t propId{0};    // Property<T>::id (crc64 of the field name)
    GenericValue value{};
  };

  // A new component added by the child that the parent does not have. The
  // component's own uuid is unique to the child.
  struct PrefabAddedComponent
  {
    uint32_t objUuid{0};   // Inherited Object::uuid this attaches to (or 0 = root)
    Component::Entry entry{};
  };

  // A new child Object the parent's tree does not contain. parentUuid points
  // at an Object in the inherited tree (0 = under the prefab root).
  struct PrefabAddedObject
  {
    uint32_t parentUuid{0};
    std::shared_ptr<Object> obj{};
  };

  class Prefab
  {
    public:
      PROP_U32(uuid);
      Object obj{};

      // Class variables — Blueprint-style typed properties with defaults.
      // Per-instance overrides live on Object::varOverrides keyed by varDef uuid.
      std::vector<PrefabVarDef> variables{};

      // Default Event Graph for the prefab — serialized as JSON in the
      // ImNodeFlow / Project::Graph::Graph format. The editor instantiates a
      // live ImNodeFlow Graph from this string when the user opens the
      // event graph window, and serializes back here on save. Empty string
      // means "no graph yet" (the editor renders a blank canvas).
      std::string eventGraphJSON{};

      // For child prefabs (uuidParentPrefab != 0) the per-event subgraph map
      // records how the child treats each event entry from the parent's graph:
      //   "inherited" (use parent's), "overridden" (child supplies own), or
      //   "added" (event the parent did not define). Codegen consults this to
      //   emit super-calls vs direct dispatch. Empty for standalone prefabs.
      // Keys are PrefabEvent::Kind ident strings ("Ready", "Tick", "Custom0"…).
      std::unordered_map<std::string, std::string> eventOverrideMode{};

      // Variant inheritance: when uuidParentPrefab is non-zero this prefab's
      // effective tree is derived by deep-copying the parent's resolved tree
      // (uuids preserved), removing anything in the *removed* sets, applying
      // structured property overrides, and finally inserting *added* objects
      // and components. Survives parent-side reorders cleanly.
      PROP_U64(uuidParentPrefab);

      std::vector<PrefabPropOverride>          propOverrides{};
      std::unordered_set<uint32_t>             removedObjects{};
      std::unordered_set<uint64_t>             removedComponents{};
      std::vector<PrefabAddedComponent>        addedComponents{};
      std::vector<PrefabAddedObject>           addedObjects{};
      std::unordered_map<uint64_t, GenericValue> varDefaultOverrides{};
      std::vector<PrefabVarDef>                addedVariables{};
      std::unordered_set<uint64_t>             removedVariables{};

      // Legacy RFC 6902 JSON Patch payload kept around only long enough for
      // migrateFromLegacyPatch() to convert it into the structured overrides
      // above. Always empty on save.
      nlohmann::json legacyPatchOps = nlohmann::json::array();

      bool isVariant() const { return uuidParentPrefab.value != 0; }

      std::string serialize(const Object &obj) const;
      std::string serialize() const { return serialize(obj); }

      void deserialize(const std::string &str);

      // Re-resolve this variant's `obj` (and effective `variables`) from a
      // (resolved) parent prefab. No-op for non-variants.
      void resolveAgainstParent(const Prefab &parent);

      // Convert a legacy patchOps payload into structured override fields.
      // Run after resolveAgainstParent has already produced a resolved `obj`.
      // No-op when legacyPatchOps is empty (already migrated).
      void migrateFromLegacyPatch(const Prefab &parent);

      // Recompute structured override fields by diffing this variant's
      // current `obj` against the parent's resolved tree. Called on save.
      void rebuildOverridesFromCurrent(const Prefab &parent);

      // Walk obj tree and assign a fresh stable uuid to any Object whose
      // uuid is 0 (legacy / never-saved data). Idempotent.
      void ensureStableUuids();

      void save();
  };
}
