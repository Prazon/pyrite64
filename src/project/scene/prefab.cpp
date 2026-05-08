/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "prefab.h"

#include "../../utils/json.h"
#include "../../utils/jsonBuilder.h"
#include "../../utils/logger.h"
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
}

std::string Project::Prefab::serialize(const Object &obj) const
{
  Builder builder{};
  builder.set(uuid);
  if (!variables.empty()) {
    builder.doc["variables"] = serializeVariables(variables);
  }
  if (uuidParentPrefab.value != 0) {
    // Variant: persist only the parent link + the diff against parent's tree.
    // `obj` is recomputed at load time by resolveAgainstParent.
    builder.set(uuidParentPrefab);
    builder.doc["patch"] = patchOps;
  } else {
    // Standalone prefab: full tree on disk (legacy format).
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

  if (uuidParentPrefab.value != 0) {
    // Variant: stash the patch; AssetManager calls resolveAgainstParent
    // once the parent prefab is itself resolved (topological order).
    patchOps = doc.contains("patch") && doc["patch"].is_array()
      ? doc["patch"]
      : nlohmann::json::array();
    obj = Object{};
  } else {
    obj.deserialize(nullptr, doc["obj"]);
  }
}

void Project::Prefab::resolveAgainstParent(const Prefab &parent)
{
  if (uuidParentPrefab.value == 0) return;

  auto parentJson = parent.obj.serialize();
  nlohmann::json effective = parentJson;
  if (patchOps.is_array() && !patchOps.empty()) {
    try {
      effective = parentJson.patch(patchOps);
    } catch (const std::exception &e) {
      Utils::Logger::log(
        "Prefab variant " + std::to_string(uuid.value)
          + " patch failed against parent "
          + std::to_string(uuidParentPrefab.value) + ": " + e.what()
          + " — falling back to parent tree.",
        Utils::Logger::LEVEL_ERROR
      );
      effective = parentJson;
    }
  }
  obj = Object{};
  obj.deserialize(nullptr, effective);
}

void Project::Prefab::rebuildPatchFromCurrent(const Prefab &parent)
{
  if (uuidParentPrefab.value == 0) return;
  auto parentJson = parent.obj.serialize();
  auto myJson = obj.serialize();
  patchOps = nlohmann::json::diff(parentJson, myJson);
}

void Project::Prefab::save()
{
  auto prefabJson = serialize();
  Utils::FS::saveTextFile(
    ctx.project->getPath() + "/assets/" + obj.name + ".prefab",
    prefabJson
  );
}
