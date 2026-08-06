/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "prefab.h"

#include <algorithm>
#include <functional>
#include <unordered_map>

#include "../../utils/json.h"
#include "../../utils/jsonBuilder.h"
#include "../../utils/logger.h"
#include "../../utils/hash.h"
#include "../../context.h"

using Builder = Utils::JSON::Builder;

namespace
{
  nlohmann::json serializeVariables(const std::vector<Project::PrefabVarDef> &vars)
  {
    nlohmann::json out = nlohmann::json::array();
    for (const auto &v : vars) {
      nlohmann::json entry = {
        {"uuid", v.uuid},
        {"name", v.name},
        {"kind", static_cast<uint8_t>(v.kind)},
        {"typeArg", v.typeArg},
        {"default", v.defaultValue.serialize()},
      };
      out.push_back(std::move(entry));
    }
    return out;
  }

  std::vector<Project::PrefabVarDef> deserializeVariables(const nlohmann::json &arr)
  {
    std::vector<Project::PrefabVarDef> out;
    if (!arr.is_array()) return out;
    out.reserve(arr.size());
    for (const auto &entry : arr) {
      if (!entry.is_object()) continue;
      Project::PrefabVarDef v{};
      v.uuid = entry.value("uuid", uint64_t{0});
      v.name = entry.value("name", std::string{});
      v.kind = static_cast<Project::PrefabVarKind>(entry.value("kind", uint8_t{0}));
      v.typeArg = entry.value("typeArg", uint64_t{0});
      v.defaultValue.deserialize(entry.value("default", std::string{}));
      // Skip nameless / uuid-less entries (corrupted file or version skew).
      if (v.uuid == 0 || v.name.empty()) continue;
      out.push_back(std::move(v));
    }
    return out;
  }

  // Recursive walk: index every Object in `root` (and its subtree) by uuid.
  void indexByUuid(Project::Object &root,
                   std::unordered_map<uint32_t, Project::Object*> &out)
  {
    if (root.uuid != 0) out[root.uuid] = &root;
    for (auto &c : root.children) indexByUuid(*c, out);
  }

  // Recursive deep-copy of an Object subtree. Preserves uuids — child variants
  // key their overrides off those uuids.
  std::shared_ptr<Project::Object> cloneSubtree(const Project::Object &src,
                                                Project::Object *parent)
  {
    auto out = parent
      ? std::make_shared<Project::Object>(*parent)
      : std::make_shared<Project::Object>();
    out->name              = src.name;
    out->uuid              = src.uuid;
    out->runtimeId         = src.runtimeId;
    out->uuidPrefab.value  = src.uuidPrefab.value;
    out->pos.value         = src.pos.value;
    out->rot.value         = src.rot.value;
    out->scale.value       = src.scale.value;
    out->proportionalScale = src.proportionalScale;
    out->enabled           = src.enabled;
    out->selectable        = src.selectable;
    out->isCanvas2D        = src.isCanvas2D;
    out->anchor2D          = src.anchor2D;
    out->layerIndex2D      = src.layerIndex2D;
    out->fromPrefab        = src.fromPrefab;
    out->propOverrides     = src.propOverrides;
    out->varOverrides      = src.varOverrides;
    // Components carry shared_ptr<void> data; round-trip through JSON so
    // each instance owns its own component data rather than aliasing the
    // parent's. The per-component serializer is the canonical clone path.
    for (const auto &c : src.components) {
      auto &def = Project::Component::TABLE[c.id];
      Project::Component::Entry e{};
      e.id   = c.id;
      e.uuid = c.uuid;
      e.name = c.name;
      auto j = def.funcSerialize(c);
      e.data = def.funcDeserialize(j);
      out->components.push_back(std::move(e));
    }
    for (const auto &child : src.children) {
      out->children.push_back(cloneSubtree(*child, out.get()));
    }
    return out;
  }

  void cloneRootInto(const Project::Object &src, Project::Object &dst)
  {
    dst.name              = src.name;
    dst.uuid              = src.uuid;
    dst.runtimeId         = src.runtimeId;
    dst.uuidPrefab.value  = src.uuidPrefab.value;
    dst.pos.value         = src.pos.value;
    dst.rot.value         = src.rot.value;
    dst.scale.value       = src.scale.value;
    dst.proportionalScale = src.proportionalScale;
    dst.enabled           = src.enabled;
    dst.selectable        = src.selectable;
    dst.isCanvas2D        = src.isCanvas2D;
    dst.anchor2D          = src.anchor2D;
    dst.layerIndex2D      = src.layerIndex2D;
    dst.fromPrefab        = src.fromPrefab;
    dst.propOverrides     = src.propOverrides;
    dst.varOverrides      = src.varOverrides;
    dst.children.clear();
    dst.components.clear();
    for (const auto &c : src.components) {
      auto &def = Project::Component::TABLE[c.id];
      Project::Component::Entry e{};
      e.id   = c.id;
      e.uuid = c.uuid;
      e.name = c.name;
      auto j = def.funcSerialize(c);
      e.data = def.funcDeserialize(j);
      dst.components.push_back(std::move(e));
    }
    for (const auto &child : src.children) {
      dst.children.push_back(cloneSubtree(*child, &dst));
    }
  }

  // Apply a single property override to an Object's transform or a component.
  // Returns true on success — false silently when target lookup fails (e.g.
  // a property override targeting a component that was also removed).
  bool applyPropOverride(Project::Object &target,
                         const Project::PrefabPropOverride &ovr)
  {
    if (ovr.compUuid == 0) {
      // Override on the Object itself — store in its propOverrides map.
      target.propOverrides[ovr.propId] = ovr.value;
      return true;
    }
    for (auto &c : target.components) {
      if (c.uuid != ovr.compUuid) continue;
      // Component-level property overrides: round-trip through the
      // component's own JSON to mutate the specific field. This keeps us
      // honest about the on-disk format and avoids hand-coding a setter
      // for every per-component property.
      //
      // Strategy: serialize → JSON object → write the field whose CRC64
      // matches propId → deserialize. The CRC64 mapping is reconstructed
      // by walking the serialized object's keys.
      auto &def = Project::Component::TABLE[c.id];
      auto j = def.funcSerialize(c);
      if (!j.is_object()) return false;
      bool matched = false;
      for (auto it = j.begin(); it != j.end(); ++it) {
        if (Utils::Hash::crc64(it.key()) != ovr.propId) continue;
        // Write the value back as a string (GenericValue's own ser format
        // is a stable string that the per-component deserialize knows how
        // to parse for primitive fields). For richer struct fields the
        // caller is expected to pass a JSON-shaped value via ovr.value's
        // valString.
        if (!ovr.value.valString.empty()) {
          auto parsed = nlohmann::json::parse(ovr.value.valString, nullptr, false);
          if (!parsed.is_discarded()) {
            it.value() = parsed;
          } else {
            it.value() = ovr.value.valString;
          }
        } else {
          // Fall back to GenericValue's serialized scalar.
          it.value() = ovr.value.serialize();
        }
        matched = true;
        break;
      }
      if (!matched) return false;
      c.data = def.funcDeserialize(j);
      return true;
    }
    return false;
  }

  // Compute per-property delta between a child's resolved Object and the
  // parent's same-uuid Object. Emits one PrefabPropOverride per differing
  // field (transform fields and component data).
  void collectDiff(const Project::Object &child,
                   const Project::Object &parent,
                   std::vector<Project::PrefabPropOverride> &out)
  {
    auto pushIfChanged = [&](uint64_t pid, const auto &cv, const auto &pv) {
      if (cv == pv) return;
      Project::PrefabPropOverride o{};
      o.objUuid = child.uuid;
      o.compUuid = 0;
      o.propId = pid;
      using T = std::decay_t<decltype(cv)>;
      o.value.set<T>(cv);
      out.push_back(std::move(o));
    };
    pushIfChanged(child.pos.id,   child.pos.value,   parent.pos.value);
    pushIfChanged(child.rot.id,   child.rot.value,   parent.rot.value);
    pushIfChanged(child.scale.id, child.scale.value, parent.scale.value);

    // Component property delta: walk child components and find each by uuid
    // in the parent. Anything new is already represented as addedComponents
    // (caller handles), anything removed shows up via removedComponents.
    for (const auto &cc : child.components) {
      const Project::Component::Entry *pc = nullptr;
      for (const auto &p : parent.components) {
        if (p.uuid == cc.uuid) { pc = &p; break; }
      }
      if (!pc) continue;
      auto &def = Project::Component::TABLE[cc.id];
      auto cj = def.funcSerialize(cc);
      auto pj = def.funcSerialize(*pc);
      if (!cj.is_object() || !pj.is_object()) continue;
      for (auto it = cj.begin(); it != cj.end(); ++it) {
        if (!pj.contains(it.key())) continue;
        if (pj[it.key()] == it.value()) continue;
        Project::PrefabPropOverride o{};
        o.objUuid = child.uuid;
        o.compUuid = cc.uuid;
        o.propId = Utils::Hash::crc64(it.key());
        // Stash the raw JSON for applyPropOverride to round-trip later.
        o.value.valString = it.value().dump();
        o.value.type = 9; // string-typed sentinel
        out.push_back(std::move(o));
      }
    }
  }

  // Recursively walk parent and child trees keyed by Object::uuid to build:
  //  - propOverrides : per-field diffs on shared (same-uuid) objects
  //  - removedObjects: uuids present in parent but missing in child
  //  - removedComponents: comp uuids present in parent-tree but missing in
  //                       the child's same-uuid object
  //  - addedComponents : new comp uuids on shared objects
  //  - addedObjects   : new uuids in child not present in parent
  void diffTrees(const Project::Object &parent,
                 const Project::Object &child,
                 Project::Prefab &out,
                 const std::unordered_map<uint32_t, const Project::Object*> &parentIdx,
                 const std::unordered_map<uint32_t, const Project::Object*> &childIdx)
  {
    // Same-uuid root: emit property/component diffs.
    if (parent.uuid == child.uuid) {
      collectDiff(child, parent, out.propOverrides);
      // Component additions / removals.
      for (const auto &pc : parent.components) {
        bool stillThere = false;
        for (const auto &cc : child.components) {
          if (cc.uuid == pc.uuid) { stillThere = true; break; }
        }
        if (!stillThere) out.removedComponents.insert(pc.uuid);
      }
      for (const auto &cc : child.components) {
        bool inParent = false;
        for (const auto &pc : parent.components) {
          if (pc.uuid == cc.uuid) { inParent = true; break; }
        }
        if (!inParent) {
          Project::PrefabAddedComponent a{};
          a.objUuid = child.uuid;
          // Clone the entry so the prefab owns its own copy.
          auto &def = Project::Component::TABLE[cc.id];
          a.entry.id = cc.id;
          a.entry.uuid = cc.uuid;
          a.entry.name = cc.name;
          auto j = def.funcSerialize(cc);
          a.entry.data = def.funcDeserialize(j);
          out.addedComponents.push_back(std::move(a));
        }
      }
    }

    // Walk children, recursing into same-uuid pairs.
    for (const auto &pc : parent.children) {
      auto childIt = childIdx.find(pc->uuid);
      if (childIt == childIdx.end()) {
        out.removedObjects.insert(pc->uuid);
        continue;
      }
      diffTrees(*pc, *childIt->second, out, parentIdx, childIdx);
    }

    // Discover added children: in child's tree but not in parent's.
    for (const auto &cc : child.children) {
      auto parentIt = parentIdx.find(cc->uuid);
      if (parentIt != parentIdx.end()) continue;
      Project::PrefabAddedObject a{};
      // Find this added subtree's anchor: parent in the child tree must be
      // a uuid that exists in the parent prefab (or 0 = root).
      a.parentUuid = (cc->parent && parentIdx.contains(cc->parent->uuid))
        ? cc->parent->uuid
        : 0;
      a.obj = cloneSubtree(*cc, nullptr);
      out.addedObjects.push_back(std::move(a));
    }
  }
}

std::string Project::Prefab::serialize(const Object &obj) const
{
  Builder builder{};
  builder.set(uuid);
  if (!variables.empty()) {
    builder.doc["variables"] = serializeVariables(variables);
  }
  if (!eventGraphJSON.empty()) {
    auto parsed = nlohmann::json::parse(eventGraphJSON, nullptr, false);
    if (!parsed.is_discarded()) {
      builder.doc["eventGraph"] = parsed;
    }
  }
  if (!eventOverrideMode.empty()) {
    nlohmann::json m = nlohmann::json::object();
    for (auto &[k, v] : eventOverrideMode) m[k] = v;
    builder.doc["eventOverrideMode"] = m;
  }
  if (uuidParentPrefab.value != 0) {
    // Variant: persist only the parent link + structured deltas. `obj` is
    // recomputed at load time by resolveAgainstParent.
    builder.set(uuidParentPrefab);

    if (!propOverrides.empty()) {
      auto arr = nlohmann::json::array();
      for (auto &o : propOverrides) {
        nlohmann::json j;
        j["obj"]  = o.objUuid;
        j["comp"] = o.compUuid;
        j["pid"]  = o.propId;
        j["val"]  = o.value.serialize();
        j["vt"]   = o.value.type;
        arr.push_back(j);
      }
      builder.doc["propOverrides"] = arr;
    }
    if (!removedObjects.empty()) {
      auto arr = nlohmann::json::array();
      for (auto u : removedObjects) arr.push_back(u);
      builder.doc["removedObjects"] = arr;
    }
    if (!removedComponents.empty()) {
      auto arr = nlohmann::json::array();
      for (auto u : removedComponents) arr.push_back(u);
      builder.doc["removedComponents"] = arr;
    }
    if (!addedComponents.empty()) {
      auto arr = nlohmann::json::array();
      for (auto &a : addedComponents) {
        auto &def = Component::TABLE[a.entry.id];
        nlohmann::json e;
        e["obj"]  = a.objUuid;
        e["id"]   = a.entry.id;
        e["uuid"] = a.entry.uuid;
        e["name"] = a.entry.name;
        e["data"] = def.funcSerialize(a.entry);
        arr.push_back(e);
      }
      builder.doc["addedComponents"] = arr;
    }
    if (!addedObjects.empty()) {
      auto arr = nlohmann::json::array();
      for (auto &a : addedObjects) {
        nlohmann::json j;
        j["parent"] = a.parentUuid;
        j["obj"]    = a.obj ? a.obj->serialize() : nlohmann::json::object();
        arr.push_back(j);
      }
      builder.doc["addedObjects"] = arr;
    }
    if (!varDefaultOverrides.empty()) {
      auto m = nlohmann::json::object();
      for (auto &[k, v] : varDefaultOverrides) {
        m[std::to_string(k)] = v.serialize();
      }
      builder.doc["varDefaultOverrides"] = m;
    }
    if (!addedVariables.empty()) {
      builder.doc["addedVariables"] = serializeVariables(addedVariables);
    }
    if (!removedVariables.empty()) {
      auto arr = nlohmann::json::array();
      for (auto u : removedVariables) arr.push_back(u);
      builder.doc["removedVariables"] = arr;
    }
  } else {
    // Standalone prefab: full tree on disk.
    builder.doc["obj"] = obj.serialize();
  }
  return builder.toString();
}

void Project::Prefab::deserialize(const std::string &str)
{
  auto doc = nlohmann::json::parse(str, nullptr, false);
  if(!doc.is_object())return;
  Utils::JSON::readProp(doc, uuid);
  Utils::JSON::readProp(doc, uuidParentPrefab);

  variables.clear();
  if (doc.contains("variables")) {
    variables = deserializeVariables(doc["variables"]);
  }

  eventGraphJSON.clear();
  if (doc.contains("eventGraph") && doc["eventGraph"].is_object()) {
    eventGraphJSON = doc["eventGraph"].dump();
  }

  eventOverrideMode.clear();
  if (doc.contains("eventOverrideMode") && doc["eventOverrideMode"].is_object()) {
    for (auto it = doc["eventOverrideMode"].begin();
         it != doc["eventOverrideMode"].end(); ++it) {
      if (it.value().is_string()) eventOverrideMode[it.key()] = it.value();
    }
  }

  propOverrides.clear();
  removedObjects.clear();
  removedComponents.clear();
  addedComponents.clear();
  addedObjects.clear();
  varDefaultOverrides.clear();
  addedVariables.clear();
  removedVariables.clear();
  legacyPatchOps = nlohmann::json::array();

  if (uuidParentPrefab.value != 0) {
    // Variant: read structured deltas. AssetManager calls
    // resolveAgainstParent once the parent prefab is itself resolved.
    if (doc.contains("propOverrides") && doc["propOverrides"].is_array()) {
      for (auto &j : doc["propOverrides"]) {
        PrefabPropOverride o{};
        o.objUuid  = j.value("obj",  uint32_t{0});
        o.compUuid = j.value("comp", uint64_t{0});
        o.propId   = j.value("pid",  uint64_t{0});
        o.value.deserialize(j.value("val", std::string{}));
        if (j.contains("vt")) o.value.type = j["vt"];
        propOverrides.push_back(std::move(o));
      }
    }
    if (doc.contains("removedObjects") && doc["removedObjects"].is_array()) {
      for (auto &j : doc["removedObjects"]) removedObjects.insert(j.get<uint32_t>());
    }
    if (doc.contains("removedComponents") && doc["removedComponents"].is_array()) {
      for (auto &j : doc["removedComponents"]) removedComponents.insert(j.get<uint64_t>());
    }
    if (doc.contains("addedComponents") && doc["addedComponents"].is_array()) {
      for (auto &j : doc["addedComponents"]) {
        PrefabAddedComponent a{};
        a.objUuid = j.value("obj", uint32_t{0});
        int cid = j.value("id", -1);
        if (cid < 0 || cid >= (int)Component::TABLE.size()) continue;
        auto &def = Component::TABLE[cid];
        a.entry.id   = cid;
        a.entry.uuid = j.value("uuid", uint64_t{0});
        a.entry.name = j.value("name", std::string{});
        auto data = j.contains("data") ? j["data"] : nlohmann::json::object();
        a.entry.data = def.funcDeserialize(data);
        addedComponents.push_back(std::move(a));
      }
    }
    if (doc.contains("addedObjects") && doc["addedObjects"].is_array()) {
      for (auto &j : doc["addedObjects"]) {
        PrefabAddedObject a{};
        a.parentUuid = j.value("parent", uint32_t{0});
        auto child = std::make_shared<Object>();
        if (j.contains("obj")) {
          auto subdoc = j["obj"];
          child->deserialize(nullptr, subdoc);
        }
        a.obj = child;
        addedObjects.push_back(std::move(a));
      }
    }
    if (doc.contains("varDefaultOverrides") && doc["varDefaultOverrides"].is_object()) {
      for (auto it = doc["varDefaultOverrides"].begin();
           it != doc["varDefaultOverrides"].end(); ++it) {
        uint64_t k = std::stoull(it.key());
        GenericValue v{};
        v.deserialize(it.value().get<std::string>());
        varDefaultOverrides[k] = v;
      }
    }
    if (doc.contains("addedVariables")) {
      addedVariables = deserializeVariables(doc["addedVariables"]);
    }
    if (doc.contains("removedVariables") && doc["removedVariables"].is_array()) {
      for (auto &j : doc["removedVariables"]) removedVariables.insert(j.get<uint64_t>());
    }

    // Legacy: if a "patch" array is present and no structured fields were
    // seen, stash it for migrateFromLegacyPatch to consume on first resolve.
    if (doc.contains("patch") && doc["patch"].is_array()
        && propOverrides.empty()
        && removedObjects.empty()
        && removedComponents.empty()
        && addedComponents.empty()
        && addedObjects.empty()
        && varDefaultOverrides.empty()
        && addedVariables.empty()
        && removedVariables.empty()) {
      legacyPatchOps = doc["patch"];
    }

    obj = Object{};
  } else {
    obj.deserialize(nullptr, doc["obj"]);
  }
}

void Project::Prefab::resolveAgainstParent(const Prefab &parent)
{
  if (uuidParentPrefab.value == 0) return;

  // 1. Deep-copy parent's resolved tree (preserving uuids).
  cloneRootInto(parent.obj, obj);

  // 2. Index by uuid for O(1) lookups during override application.
  std::unordered_map<uint32_t, Object*> idx;
  indexByUuid(obj, idx);

  // 3. Drop removed objects (recursive: a removed Object takes its
  //    descendants with it). Walk children with iterators to allow erasure.
  std::function<void(Object&)> dropRemoved = [&](Object &o) {
    o.children.erase(
      std::remove_if(o.children.begin(), o.children.end(),
        [&](const std::shared_ptr<Object> &c) {
          return removedObjects.contains(c->uuid);
        }),
      o.children.end()
    );
    for (auto &c : o.children) dropRemoved(*c);
  };
  dropRemoved(obj);
  // Rebuild index (drops invalidated pointers).
  idx.clear();
  indexByUuid(obj, idx);

  // 4. Drop removed components anywhere in the tree.
  std::function<void(Object&)> dropComps = [&](Object &o) {
    o.components.erase(
      std::remove_if(o.components.begin(), o.components.end(),
        [&](const Component::Entry &e) {
          return removedComponents.contains(e.uuid);
        }),
      o.components.end()
    );
    for (auto &c : o.children) dropComps(*c);
  };
  dropComps(obj);

  // 5. Apply property overrides.
  for (auto &o : propOverrides) {
    auto it = idx.find(o.objUuid);
    if (it == idx.end()) continue;
    applyPropOverride(*it->second, o);
  }

  // 6. Insert added components.
  for (auto &a : addedComponents) {
    Object *target = nullptr;
    if (a.objUuid == 0) {
      target = &obj;
    } else {
      auto it = idx.find(a.objUuid);
      if (it != idx.end()) target = it->second;
    }
    if (!target) continue;
    auto &def = Component::TABLE[a.entry.id];
    Component::Entry copy{};
    copy.id   = a.entry.id;
    copy.uuid = a.entry.uuid;
    copy.name = a.entry.name;
    auto j = def.funcSerialize(a.entry);
    copy.data = def.funcDeserialize(j);
    target->components.push_back(std::move(copy));
  }

  // 7. Insert added objects.
  for (auto &a : addedObjects) {
    Object *parentObj = nullptr;
    if (a.parentUuid == 0) {
      parentObj = &obj;
    } else {
      auto it = idx.find(a.parentUuid);
      if (it != idx.end()) parentObj = it->second;
    }
    if (!parentObj || !a.obj) continue;
    auto clone = cloneSubtree(*a.obj, parentObj);
    parentObj->children.push_back(clone);
    indexByUuid(*clone, idx);
  }

  // 8. Merge variables: parent's, minus removed, with overridden defaults,
  //    plus the child's additions.
  variables.clear();
  variables.reserve(parent.variables.size());
  for (const auto &v : parent.variables) {
    if (removedVariables.contains(v.uuid)) continue;
    PrefabVarDef merged = v;
    auto it = varDefaultOverrides.find(v.uuid);
    if (it != varDefaultOverrides.end()) merged.defaultValue = it->second;
    variables.push_back(std::move(merged));
  }
  for (const auto &v : addedVariables) variables.push_back(v);

  // 9. Convert any legacy patch payload to the structured model now that
  //    we have a resolved tree to diff against.
  if (legacyPatchOps.is_array() && !legacyPatchOps.empty()) {
    migrateFromLegacyPatch(parent);
  }
}

void Project::Prefab::migrateFromLegacyPatch(const Prefab &parent)
{
  if (!legacyPatchOps.is_array() || legacyPatchOps.empty()) return;

  // Apply the legacy RFC 6902 patch to a transient Object tree, then diff
  // it semantically against the parent to populate the structured fields.
  // We start from the parent's already-resolved tree (uuids preserved).
  auto parentJson = parent.obj.serialize();
  nlohmann::json effective;
  try {
    effective = parentJson.patch(legacyPatchOps);
  } catch (const std::exception &e) {
    Utils::Logger::log(
      "Prefab variant " + std::to_string(uuid.value)
        + " legacy patch failed against parent "
        + std::to_string(uuidParentPrefab.value) + ": " + e.what()
        + " — dropping migration; child will mirror parent.",
      Utils::Logger::LEVEL_ERROR
    );
    legacyPatchOps = nlohmann::json::array();
    return;
  }
  Object resolved{};
  resolved.deserialize(nullptr, effective);

  // The current `obj` is the parent's clone; we want to diff `resolved`
  // (= effective post-patch) against `parent.obj` for the deltas.
  std::unordered_map<uint32_t, const Object*> parentIdx, childIdx;
  std::function<void(const Object&, std::unordered_map<uint32_t, const Object*>&)> idx =
    [&](const Object &o, auto &m) {
      if (o.uuid != 0) m[o.uuid] = &o;
      for (auto &c : o.children) idx(*c, m);
    };
  idx(parent.obj, parentIdx);
  idx(resolved,   childIdx);

  diffTrees(parent.obj, resolved, *this, parentIdx, childIdx);
  legacyPatchOps = nlohmann::json::array();

  // The structured overrides we just computed weren't applied to `obj`
  // yet — they were derived from `resolved`, which is what `obj` should
  // become. Adopt resolved as the new obj.
  cloneRootInto(resolved, obj);

  Utils::Logger::log(
    "Migrated prefab variant " + std::to_string(uuid.value)
      + " from legacy RFC 6902 patch to structured overrides ("
      + std::to_string(propOverrides.size()) + " prop, "
      + std::to_string(removedObjects.size()) + " removedObj, "
      + std::to_string(removedComponents.size()) + " removedComp, "
      + std::to_string(addedComponents.size()) + " addedComp, "
      + std::to_string(addedObjects.size()) + " addedObj).",
    Utils::Logger::LEVEL_INFO
  );
}

void Project::Prefab::rebuildOverridesFromCurrent(const Prefab &parent)
{
  if (uuidParentPrefab.value == 0) return;

  // Wipe structured overrides; we recompute them from current obj vs parent.obj.
  propOverrides.clear();
  removedObjects.clear();
  removedComponents.clear();
  addedComponents.clear();
  addedObjects.clear();

  std::unordered_map<uint32_t, const Object*> parentIdx, childIdx;
  std::function<void(const Object&, std::unordered_map<uint32_t, const Object*>&)> idx =
    [&](const Object &o, auto &m) {
      if (o.uuid != 0) m[o.uuid] = &o;
      for (auto &c : o.children) idx(*c, m);
    };
  idx(parent.obj, parentIdx);
  idx(obj,        childIdx);

  diffTrees(parent.obj, obj, *this, parentIdx, childIdx);

  // Variables: recompute additions / removals / default overrides.
  varDefaultOverrides.clear();
  addedVariables.clear();
  removedVariables.clear();
  std::unordered_set<uint64_t> parentVarUuids;
  for (auto &v : parent.variables) parentVarUuids.insert(v.uuid);
  for (auto &v : variables) {
    if (parentVarUuids.contains(v.uuid)) {
      // Inherited — record an override only if the default differs.
      const PrefabVarDef *pv = nullptr;
      for (auto &p : parent.variables) if (p.uuid == v.uuid) { pv = &p; break; }
      if (pv && pv->defaultValue.serialize() != v.defaultValue.serialize()) {
        varDefaultOverrides[v.uuid] = v.defaultValue;
      }
    } else {
      addedVariables.push_back(v);
    }
  }
  for (auto &p : parent.variables) {
    bool stillThere = false;
    for (auto &v : variables) if (v.uuid == p.uuid) { stillThere = true; break; }
    if (!stillThere) removedVariables.insert(p.uuid);
  }
}

void Project::Prefab::ensureStableUuids()
{
  std::function<void(Object&)> walk = [&](Object &o) {
    if (o.uuid == 0) o.uuid = static_cast<uint32_t>(Utils::Hash::randomU64());
    for (auto &c : o.children) walk(*c);
  };
  walk(obj);
}

void Project::Prefab::save(const std::string &path)
{
  Utils::FS::saveTextFile(path, serialize());
}
