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

  // Editor-authored instances persist uuid-keyed values alongside the legacy
  // string map so a single .p64res can roundtrip through either schema source.
  if (!uuidValues.empty()) {
    auto uuidObj = nlohmann::json::object();
    for (const auto &[k, v] : uuidValues) {
      // JSON object keys are strings; convert uint64 to decimal so it
      // roundtrips losslessly without surprising float precision.
      uuidObj[std::to_string(k)] = v.serialize();
    }
    builder.doc["uuidValues"] = std::move(uuidObj);
  }

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

  uuidValues.clear();
  if (doc.contains("uuidValues") && doc["uuidValues"].is_object()) {
    for (const auto &[k, v] : doc["uuidValues"].items()) {
      uint64_t key = 0;
      try { key = std::stoull(k); } catch (...) { continue; }
      GenericValue gv{};
      gv.deserialize(v.is_string() ? v.get<std::string>() : v.dump());
      uuidValues[key] = std::move(gv);
    }
  }
}
