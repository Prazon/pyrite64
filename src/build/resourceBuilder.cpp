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

namespace fs = std::filesystem;

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
    // struct, so the bytes here must match GCC's natural-alignment layout
    // for that struct. Pad each field up to its alignment before writing.
    // N64 MIPS ABI: alignment = min(fieldSize, 4). Strings (char arrays)
    // are byte-aligned; their dataSize already includes the array length.
    Utils::BinaryFile blob{};
    uint32_t offset = 0;

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
          // Pad/truncate to the declared char[N] capacity, NUL-terminated.
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

    auto code = Utils::FS::loadTextFile(t.path);
    bool hasOnLoad   = Utils::CPP::hasFunction(code, "void", "onLoad");
    bool hasOnUnload = Utils::CPP::hasFunction(code, "void", "onUnload");

    auto uuidStr = std::format("{:016X}", t.getUUID());
    typeUuidToIdx[t.getUUID()] = typeIdx;

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

  auto outPath = project.getPath() + "/src/p64/resourceTable.cpp";
  Utils::FS::saveTextFile(outPath, src);
}
