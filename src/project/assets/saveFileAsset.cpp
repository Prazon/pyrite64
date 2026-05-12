/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "saveFileAsset.h"

namespace Project::Assets
{
  uint32_t SaveFileAsset::fieldSlotCount(const Field &f)
  {
    switch (f.type) {
      case FT_INT:    return 1;
      case FT_FLOAT:  return 1;
      case FT_BOOL:   return 1;
      case FT_STRING: {
        uint32_t n = f.stringLen;
        if (n == 0) n = 1;
        return (n + 3) / 4;
      }
      case FT_VEC2:   return 2;
      case FT_VEC3:   return 3;
    }
    return 1;
  }

  uint32_t SaveFileAsset::slotsUsed() const
  {
    uint32_t total = 0;
    for (const auto &f : fields) total += fieldSlotCount(f);
    return total;
  }
}

std::string Project::Assets::SaveFileAsset::serialize() const
{
  nlohmann::json doc{};
  doc["uuid"]      = uuid;
  doc["version"]   = version;
  doc["groupName"] = groupName;

  nlohmann::json arr = nlohmann::json::array();
  for (const auto &f : fields) {
    nlohmann::json fj{};
    fj["name"]      = f.name;
    fj["type"]      = (int32_t)f.type;
    fj["defInt"]    = f.defInt;
    fj["defFloat"]  = f.defFloat;
    fj["defBool"]   = f.defBool;
    fj["defString"] = f.defString;
    fj["defVec"]    = nlohmann::json::array({f.defVec[0], f.defVec[1], f.defVec[2]});
    fj["stringLen"] = f.stringLen;
    arr.push_back(fj);
  }
  doc["fields"] = arr;
  return doc.dump(2);
}

void Project::Assets::SaveFileAsset::deserialize(const std::string &raw)
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

  uuid      = doc.value<uint64_t>("uuid", 0);
  version   = doc.value<int32_t>("version", 1);
  groupName = doc.value<std::string>("groupName", "");
  fields.clear();

  if (doc.contains("fields") && doc["fields"].is_array()) {
    for (const auto &fj : doc["fields"]) {
      if (!fj.is_object()) continue;
      Field f{};
      f.name      = fj.value<std::string>("name", "");
      f.type      = (FieldType)fj.value<int32_t>("type", FT_INT);
      f.defInt    = fj.value<int32_t>("defInt", 0);
      f.defFloat  = fj.value<float>("defFloat", 0.0f);
      f.defBool   = fj.value<bool>("defBool", false);
      f.defString = fj.value<std::string>("defString", "");
      f.stringLen = fj.value<uint32_t>("stringLen", 8);
      if (fj.contains("defVec") && fj["defVec"].is_array() && fj["defVec"].size() >= 3) {
        f.defVec[0] = fj["defVec"][0].get<float>();
        f.defVec[1] = fj["defVec"][1].get<float>();
        f.defVec[2] = fj["defVec"][2].get<float>();
      }
      fields.push_back(std::move(f));
    }
  }
}
