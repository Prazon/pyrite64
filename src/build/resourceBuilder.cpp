/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "projectBuilder.h"

#include <filesystem>
#include <format>
#include <unordered_map>

#include "../utils/binaryFile.h"
#include "../utils/codeParser.h"
#include "../utils/fs.h"
#include "../utils/logger.h"
#include "../utils/string.h"
#include "../project/scene/varDef.h"

namespace fs = std::filesystem;

namespace
{
  // Wire-side byte size for an editor-authored field. Stays in sync with the
  // engine accessor in resourceTable generation; both must agree on layout.
  uint32_t kindSize(Project::VarKind k)
  {
    switch (k) {
      case Project::VarKind::INT:        return 4;
      case Project::VarKind::FLOAT:      return 4;
      case Project::VarKind::BOOL:       return 1;
      case Project::VarKind::VEC3:       return 12;
      case Project::VarKind::QUAT:       return 16;
      case Project::VarKind::OBJECT_REF: return 4;
      case Project::VarKind::PREFAB_REF: return 4;
      case Project::VarKind::ASSET_REF:  return 4;
    }
    return 4;
  }

  // N64 MIPS ABI alignment rule shared with the header-authored branch.
  uint32_t kindAlign(Project::VarKind k)
  {
    if (k == Project::VarKind::BOOL) return 1;
    return 4;
  }

  void writeKindValue(Utils::BinaryFile &blob, Project::VarKind k,
                      const GenericValue &v,
                      Build::SceneCtx &sceneCtx)
  {
    switch (k) {
      case Project::VarKind::INT:   blob.write<int32_t>(v.get<int32_t>()); break;
      case Project::VarKind::FLOAT: blob.write<float>(v.get<float>()); break;
      case Project::VarKind::BOOL:  blob.write<uint8_t>(v.get<bool>() ? 1 : 0); break;
      case Project::VarKind::VEC3: {
        glm::vec3 vec = v.get<glm::vec3>();
        blob.write(vec.x); blob.write(vec.y); blob.write(vec.z);
        break;
      }
      case Project::VarKind::QUAT: {
        glm::quat q = v.get<glm::quat>();
        blob.write(q.x); blob.write(q.y); blob.write(q.z); blob.write(q.w);
        break;
      }
      case Project::VarKind::OBJECT_REF:
      case Project::VarKind::PREFAB_REF:
      case Project::VarKind::ASSET_REF: {
        // Resolve uuid -> asset-table index. 0 stays 0 (null), unresolved
        // refs degrade to 0xFFFFFFFF so the engine can detect dangling refs.
        uint64_t uuid = v.get<uint64_t>();
        uint32_t idx = 0;
        if (uuid != 0) {
          auto it = sceneCtx.assetUUIDToIdx.find(uuid);
          idx = (it != sceneCtx.assetUUIDToIdx.end()) ? it->second : 0xFFFFFFFFu;
        }
        blob.write<uint32_t>(idx);
        break;
      }
    }
  }
}

bool Build::buildResourceAssets(Project::Project &project, SceneCtx &sceneCtx)
{
  auto projectPath = fs::path{project.getPath()};
  auto &assets = project.getAssets();
  const auto &instances = assets.getTypeEntries(Project::FileType::RESOURCE_INSTANCE);

  for (auto &asset : instances)
  {
    if (asset.conf.exclude) continue;
    if (!asset.resource) {
      Utils::Logger::log(
        "Resource Instance has no loaded data: " + asset.path,
        Utils::Logger::LEVEL_ERROR
      );
      continue;
    }

    auto outPath = projectPath / asset.outPath;
    fs::create_directories(outPath.parent_path());
    sceneCtx.files.push_back(Utils::FS::toUnixPath(asset.outPath));

    auto *typeEntry = assets.getEntryByUUID(asset.resource->typeUuid);
    if (!typeEntry || typeEntry->type != Project::FileType::RESOURCE_TYPE) {
      // Emit an empty blob so the rom asset table still resolves; warn so the
      // user notices their dangling reference.
      Utils::Logger::log(
        "Resource Instance " + asset.name + " references missing type "
          + Utils::toHex64(asset.resource->typeUuid),
        Utils::Logger::LEVEL_ERROR
      );
      Utils::BinaryFile empty{};
      empty.writeToFile(outPath);
      continue;
    }

    // The runtime side casts the loaded blob straight to the user's Data
    // struct (header-authored) or reads named fields by offset (editor-
    // authored). Either way the bytes here must match the agreed layout
    // (GCC natural alignment for header-authored, the kindSize/kindAlign
    // helpers for editor-authored).
    Utils::BinaryFile blob{};
    uint32_t offset = 0;

    if (typeEntry->resourceType)
    {
      // Editor-authored: walk VarDef[] in declared order using fixed sizes.
      for (const auto &def : typeEntry->resourceType->fields) {
        uint32_t align = kindAlign(def.kind);
        while (offset % align != 0) { blob.write<uint8_t>(0); ++offset; }

        // Effective value: per-instance override else type default.
        auto it = asset.resource->uuidValues.find(def.uuid);
        const GenericValue &val = (it != asset.resource->uuidValues.end())
          ? it->second : def.defaultValue;

        try {
          writeKindValue(blob, def.kind, val, sceneCtx);
        } catch (const std::exception &e) {
          Utils::Logger::log(
            "Resource " + asset.name + ": failed to encode field '"
              + def.name + "': " + e.what(),
            Utils::Logger::LEVEL_ERROR
          );
          return false;
        }
        offset += kindSize(def.kind);
      }
    }
    else
    {
      // Header-authored: existing path, alignment = min(fieldSize, 4).
      auto fieldAlign = [](const Utils::CPP::Field &f) -> uint32_t {
        if (f.type == Utils::DataType::string) return 1;
        uint32_t s = f.dataSize ? f.dataSize : 4;
        return s > 4 ? 4 : s;
      };

      for (const auto &field : typeEntry->params.fields) {
        uint32_t align = fieldAlign(field);
        while (offset % align != 0) {
          blob.write<uint8_t>(0);
          ++offset;
        }

        auto valueIt = asset.resource->values.find(field.name);
        std::string value = (valueIt != asset.resource->values.end())
          ? valueIt->second : field.defaultValue;
        if (value.empty()) value = "0";

        try {
          if (field.type == Utils::DataType::string) {
            uint32_t cap = field.dataSize > 0 ? field.dataSize : 1;
            uint32_t toCopy = std::min<uint32_t>(value.size(), cap > 0 ? cap - 1 : 0);
            for (uint32_t i = 0; i < toCopy; ++i) blob.write<uint8_t>(value[i]);
            for (uint32_t i = toCopy; i < cap; ++i) blob.write<uint8_t>(0);
            offset += cap;
          } else {
            blob.writeAs(value, field.type);
            offset += field.dataSize ? field.dataSize : 4;
          }
        } catch (const std::exception &e) {
          Utils::Logger::log(
            "Resource " + asset.name + ": failed to encode field '"
              + field.name + "' (value '" + value + "'): " + e.what(),
            Utils::Logger::LEVEL_ERROR
          );
          return false;
        }
      }
    }

    // Tail-pad to 4 so consecutive blobs / array-of-Data parsers don't trip.
    while (offset % 4 != 0) {
      blob.write<uint8_t>(0);
      ++offset;
    }

    blob.writeToFile(outPath);
  }

  return true;
}

void Build::buildResourceTable(Project::Project &project, SceneCtx &sceneCtx)
{
  auto &assets = project.getAssets();
  const auto &types = assets.getTypeEntries(Project::FileType::RESOURCE_TYPE);

  // typeUuid → typeIdx in typeTable[]. typeIdx is uint8 on the wire so cap at
  // 254 (0xFF reserved for "not a resource"); larger projects would need a
  // wider type slot in resourceTable.cpp's typeForAssetIdx[].
  std::unordered_map<uint64_t, uint32_t> typeUuidToIdx{};

  std::string srcDecl{};
  std::string srcTypeEntries{};
  // Per-type field tables for editor-authored types. Header-authored types
  // emit an empty (count=0, ptr=nullptr) row so the engine accessor cleanly
  // returns nullptr for any uuid lookup against them.
  std::string srcFieldTables{};
  std::string srcTypeFieldRows{};

  uint32_t typeIdx = 0;
  for (const auto &t : types)
  {
    if (typeIdx >= 0xFF) {
      Utils::Logger::log(
        "Resource type count exceeds 254; remaining types will be ignored.",
        Utils::Logger::LEVEL_ERROR
      );
      break;
    }

    auto uuidStr = std::format("{:016X}", t.getUUID());
    typeUuidToIdx[t.getUUID()] = typeIdx;

    if (t.resourceType)
    {
      // Editor-authored: no C++ struct or onLoad/onUnload to emit. We do
      // emit a per-type field table so the engine accessor can resolve
      // (assetIdx, fieldUuid) -> byte offset at runtime.
      srcTypeEntries += "    { nullptr, nullptr },\n";

      const auto &fields = t.resourceType->fields;
      uint32_t offset = 0;
      std::string rows{};
      for (const auto &def : fields) {
        uint32_t align = kindAlign(def.kind);
        offset = (offset + align - 1) & ~(align - 1);
        rows += "    { 0x" + std::format("{:016X}", def.uuid) + "ull, "
              + std::to_string(static_cast<int>(def.kind)) + ", "
              + std::to_string(offset) + ", "
              + std::to_string(kindSize(def.kind)) + " },\n";
        offset += kindSize(def.kind);
      }
      if (!fields.empty()) {
        srcFieldTables += "  static const ResourceFieldRow fields_" + uuidStr + "[] = {\n";
        srcFieldTables += rows;
        srcFieldTables += "  };\n";
        srcTypeFieldRows += "    { fields_" + uuidStr + ", "
                          + std::to_string(fields.size()) + " },\n";
      } else {
        srcTypeFieldRows += "    { nullptr, 0 },\n";
      }
    }
    else
    {
      auto code = Utils::FS::loadTextFile(t.path);
      bool hasOnLoad   = Utils::CPP::hasFunction(code, "void", "onLoad");
      bool hasOnUnload = Utils::CPP::hasFunction(code, "void", "onUnload");

      srcDecl += "  namespace C" + uuidStr + " {\n";
      srcDecl += "    struct Data;\n";
      if (hasOnLoad)   srcDecl += "    void onLoad(Data*);\n";
      if (hasOnUnload) srcDecl += "    void onUnload(Data*);\n";
      srcDecl += "  }\n";

      srcTypeEntries += "    {";
      srcTypeEntries += hasOnLoad
        ? (" (FuncResource)P64::Asset::C" + uuidStr + "::onLoad,")
        : " nullptr,";
      srcTypeEntries += hasOnUnload
        ? (" (FuncResource)P64::Asset::C" + uuidStr + "::onUnload ")
        : " nullptr ";
      srcTypeEntries += "},\n";

      // Header-authored types don't carry a uuid-keyed field table; the
      // engine accesses their data by direct struct cast.
      srcTypeFieldRows += "    { nullptr, 0 },\n";
    }

    ++typeIdx;
  }

  // Walk the asset table in registration order so typeForAssetIdx[] aligns
  // with the runtime asset_table indices we emit in projectBuilder.cpp.
  std::string srcTypeForIdx{};
  for (size_t i = 0; i < sceneCtx.assetList.size(); ++i)
  {
    auto &entry = sceneCtx.assetList[i];
    uint32_t mapped = 0xFF;
    if (entry.type == (uint32_t)Project::FileType::RESOURCE_INSTANCE) {
      // Look up the instance's RESOURCE_TYPE via assetUUIDToIdx's reverse:
      // we kept the editor entry around, so find it by the rom path match.
      // Cheaper: scan instances and find the one whose romPath matches.
      for (const auto &inst : assets.getTypeEntries(Project::FileType::RESOURCE_INSTANCE)) {
        if (inst.romPath != entry.path) continue;
        if (!inst.resource) break;
        auto it = typeUuidToIdx.find(inst.resource->typeUuid);
        if (it != typeUuidToIdx.end()) mapped = it->second;
        break;
      }
    }
    srcTypeForIdx += "    " + std::to_string(mapped) + ",\n";
  }

  auto src = Utils::FS::loadTextFile("data/scripts/resourceTable.cpp");
  src = Utils::replaceAll(src, "__RESOURCE_DECL__", srcDecl);
  src = Utils::replaceAll(src, "__RESOURCE_TYPE_ENTRIES__", srcTypeEntries);
  src = Utils::replaceAll(src, "__RESOURCE_TYPE_FOR_IDX__", srcTypeForIdx);
  src = Utils::replaceAll(src, "__RESOURCE_FIELD_TABLES__", srcFieldTables);
  src = Utils::replaceAll(src, "__RESOURCE_TYPE_FIELD_ROWS__", srcTypeFieldRows);

  auto outPath = project.getPath() + "/src/p64/resourceTable.cpp";
  Utils::FS::saveTextFile(outPath, src);
}
