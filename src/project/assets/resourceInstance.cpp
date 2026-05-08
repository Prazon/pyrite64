/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "resourceInstance.h"

#include "json.hpp"
#include "../../utils/json.h"
#include "../../utils/jsonBuilder.h"
#include "../../utils/string.h"

std::string Project::Resource::Instance::serialize() const
{
  Utils::JSON::Builder builder{};
  builder.set("uuid", uuid);
  builder.set("typeUuid", typeUuid);

  auto valuesObj = nlohmann::json::object();
  for (const auto &[name, value] : values) {
    valuesObj[name] = value;
  }
  builder.doc["values"] = std::move(valuesObj);

  return builder.toString();
}

void Project::Resource::Instance::deserialize(const std::string &json)
{
  auto doc = nlohmann::json::parse(json, nullptr, false);
  if (!doc.is_object()) return;

  uuid = doc.value<uint64_t>("uuid", 0);
  typeUuid = doc.value<uint64_t>("typeUuid", 0);

  values.clear();
  if (doc.contains("values") && doc["values"].is_object()) {
    for (const auto &[k, v] : doc["values"].items()) {
      if (v.is_string()) values[k] = v.get<std::string>();
      else values[k] = v.dump();
    }
  }
}
