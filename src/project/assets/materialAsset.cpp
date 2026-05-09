/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "materialAsset.h"

#include "../../utils/json.h"
#include "../../utils/jsonBuilder.h"

std::string Project::Assets::MaterialAsset::serialize() const
{
  nlohmann::json doc{};
  doc["uuid"] = uuid;
  doc["graph"] = graphJSON.empty()
    ? nlohmann::json::object({{"nodes", nlohmann::json::array()},
                              {"links", nlohmann::json::array()}})
    : nlohmann::json::parse(graphJSON);
  doc["compiled"] = compiled.serialize();
  return doc.dump(2);
}

void Project::Assets::MaterialAsset::deserialize(const std::string &raw)
{
  if (raw.empty()) {
    *this = {};
    return;
  }
  auto doc = nlohmann::json::parse(raw, nullptr, false);
  if (!doc.is_object()) {
    *this = {};
    return;
  }

  uuid = doc.value<uint64_t>("uuid", 0);

  if (doc.contains("graph")) {
    graphJSON = doc["graph"].dump();
  } else {
    graphJSON = R"({"nodes":[],"links":[]})";
  }

  if (doc.contains("compiled") && doc["compiled"].is_object()) {
    compiled.deserialize(doc["compiled"]);
  } else {
    compiled = {};
  }
}
