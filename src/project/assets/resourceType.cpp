/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "resourceType.h"

#include "json.hpp"
#include "../../utils/json.h"
#include "../../utils/jsonBuilder.h"

namespace
{
  nlohmann::json serializeFields(const std::vector<Project::VarDef> &fields)
  {
    nlohmann::json out = nlohmann::json::array();
    for (const auto &v : fields) {
      out.push_back({
        {"uuid", v.uuid},
        {"name", v.name},
        {"kind", static_cast<uint8_t>(v.kind)},
        {"typeArg", v.typeArg},
        {"default", v.defaultValue.serialize()},
      });
    }
    return out;
  }

  std::vector<Project::VarDef> deserializeFields(const nlohmann::json &arr)
  {
    std::vector<Project::VarDef> out;
    if (!arr.is_array()) return out;
    out.reserve(arr.size());
    for (const auto &entry : arr) {
      if (!entry.is_object()) continue;
      Project::VarDef v{};
      v.uuid = entry.value("uuid", uint64_t{0});
      v.name = entry.value("name", std::string{});
      v.kind = static_cast<Project::VarKind>(entry.value("kind", uint8_t{0}));
      v.typeArg = entry.value("typeArg", uint64_t{0});
      v.defaultValue.deserialize(entry.value("default", std::string{}));
      if (v.uuid == 0 || v.name.empty()) continue;
      out.push_back(std::move(v));
    }
    return out;
  }
}

std::string Project::Resource::Type::serialize() const
{
  Utils::JSON::Builder builder{};
  builder.set("uuid", uuid);
  builder.set("name", name);
  builder.doc["fields"] = serializeFields(fields);
  return builder.toString();
}

void Project::Resource::Type::deserialize(const std::string &json)
{
  auto doc = nlohmann::json::parse(json, nullptr, false);
  if (!doc.is_object()) return;

  uuid = doc.value<uint64_t>("uuid", 0);
  name = doc.value<std::string>("name", std::string{});
  fields.clear();
  if (doc.contains("fields")) {
    fields = deserializeFields(doc["fields"]);
  }
}

const Project::VarDef* Project::Resource::Type::findField(uint64_t fieldUuid) const
{
  for (const auto &f : fields) if (f.uuid == fieldUuid) return &f;
  return nullptr;
}

Project::VarDef* Project::Resource::Type::findField(uint64_t fieldUuid)
{
  for (auto &f : fields) if (f.uuid == fieldUuid) return &f;
  return nullptr;
}
