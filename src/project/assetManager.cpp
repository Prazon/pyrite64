/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "assetManager.h"
#include "../context.h"
#include "../editor/thumbnailCache.h"
#include <filesystem>
#include <format>
#include <chrono>
#include <unordered_set>

#include "SHA256.h"
#include "../utils/codeParser.h"
#include "../utils/fs.h"
#include "../utils/hash.h"
#include "../utils/json.h"
#include "../utils/jsonBuilder.h"
#include "../utils/logger.h"
#include "../utils/meshGen.h"
#include "../utils/string.h"
#include "../utils/textureFormats.h"
#include "tiny3d/tools/gltf_importer/src/parser.h"
#include "../build/collisionMeshStub.h"
#include "../editor/imgui/notification.h"

namespace fs = std::filesystem;

namespace
{
  template<typename Loader>
  void updateDirtyState(
    uint64_t uuid,
    const std::string &currentState,
    std::unordered_set<uint64_t> &dirtySet,
    std::unordered_map<uint64_t, std::string> &savedState,
    Loader &&loadSavedState)
  {
    auto itSaved = savedState.find(uuid);
    if (itSaved == savedState.end()) {
      itSaved = savedState.emplace(uuid, loadSavedState()).first;
    }

    if (currentState == itSaved->second) {
      dirtySet.erase(uuid);
    } else {
      dirtySet.insert(uuid);
    }
  }

  fs::path getCodePath(Project::Project *project) {
    auto res = fs::path{project->getPath()} / "src" / "user";
    if (!fs::exists(res)) {
      fs::create_directory(res);
    }
    return res;
  }

  fs::path getAssetPath(Project::Project *project) {
    auto res = fs::path{project->getPath()} / "assets";
    if (!fs::exists(res)) {
      fs::create_directory(res);
    }
    return res;
  }

  std::string getAssetROMPath(const std::string &path, const std::string &basePath)
  {
    auto pathAbs = Utils::FS::toUnixPath(fs::absolute(path));
    pathAbs = pathAbs.substr(basePath.length());
    pathAbs = Utils::replaceFirst(pathAbs, "/assets/", "filesystem/");
    return pathAbs;
  }

  std::string changeExt(const std::string &path, const std::string &newExt)
  {
    auto p = fs::path(path);
    p.replace_extension(newExt);
    return p.string();
  }


  void deserialize(Project::AssetConf &conf, const fs::path &pathMeta)
  {
    auto doc = Utils::JSON::loadFile(pathMeta);
    if (doc.is_object()) {
      conf.uuid = doc.value<uint64_t>("uuid", 0);
      conf.format = doc["format"];
      conf.baseScale = doc["baseScale"];
      conf.compression = (Project::ComprTypes)doc.value<int>("compression", 0);
      conf.gltfBVH = doc["gltfBVH"];
      Utils::JSON::readProp(doc, conf.wavForceMono);
      Utils::JSON::readProp(doc, conf.wavResampleRate);
      Utils::JSON::readProp(doc, conf.wavCompression);
      Utils::JSON::readProp(doc, conf.fontId);
      Utils::JSON::readProp(doc, conf.fontCharset);

      conf.data = doc.contains("data") ? doc["data"] : nlohmann::json::object();
      conf.exclude = doc["exclude"];
    }
  }

  bool buildAssetEntry(Project::Project *project, const fs::path &path, Project::AssetManagerEntry &entry)
  {
    auto projectBase = fs::absolute(project->getPath()).string();
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

    std::string outPath = getAssetROMPath(path.string(), projectBase);

    Project::FileType type = Project::FileType::UNKNOWN;
    if (ext == ".png") {
      type = Project::FileType::IMAGE;
      if (path.string().ends_with(".bci.png")) {
        outPath = changeExt(outPath, ".bci");
      } else {
        outPath = changeExt(outPath, ".sprite");
      }
    } else if (ext == ".wav" || ext == ".mp3") {
      type = Project::FileType::AUDIO;
      outPath = changeExt(outPath, ".wav64");
    } else if (ext == ".xm") {
      type = Project::FileType::MUSIC_XM;
      outPath = changeExt(outPath, ".xm64");
    } else if (ext == ".glb" || ext == ".gltf") {
      type = Project::FileType::MODEL_3D;
      outPath = changeExt(outPath, ".t3dm");
    } else if (ext == ".ttf") {
      type = Project::FileType::FONT;
      outPath = changeExt(outPath, ".font64");
    } else if (ext == ".prefab") {
      type = Project::FileType::PREFAB;
      outPath = changeExt(outPath, ".pf");
    } else if (ext == ".p64widget") {
      // Widget blueprints share the prefab on-disk shape and are inlined into
      // scenes the same way prefab instances are; no per-asset ROM artifact.
      type = Project::FileType::WIDGET_BLUEPRINT;
      outPath = changeExt(outPath, ".pf");
    } else if (ext == ".p64graph") {
      type = Project::FileType::NODE_GRAPH;
      outPath = changeExt(outPath, ".pg");
    } else if (ext == ".p64res") {
      type = Project::FileType::RESOURCE_INSTANCE;
      outPath = changeExt(outPath, ".res");
    } else if (ext == ".p64restype") {
      // Editor-authored resource type schema. No ROM artifact of its own;
      // RESOURCE_INSTANCE assets that reference it produce the binary blob.
      type = Project::FileType::RESOURCE_TYPE;
    } else if (ext == ".p64mat") {
      // Material assets resolve at editor build time — they don't emit a
      // ROM blob themselves (the model that references them stamps the
      // resolved Material into its own data). outPath is left unset so
      // the projectBuilder skip list catches it.
      type = Project::FileType::MATERIAL;
    } else if (ext == ".p64ptx") {
      // Particle-system assets resolve at editor build time. Components
      // that reference them stamp the conf into their own InitData, so
      // no rom artifact is needed (same pattern as MATERIAL).
      type = Project::FileType::PARTICLE_SYSTEM;
    } else if (ext == ".p64save") {
      // Save-file assets are pure schema. The build collects every save
      // asset and emits typed accessors into <project>/src/p64/saveTable.*.
      // No rom artifact.
      type = Project::FileType::SAVE_FILE;
    }

    if (type == Project::FileType::UNKNOWN) {
      return false;
    }

    auto romPath = outPath;
    romPath.replace(0, std::string{"filesystem/"}.length(), "rom:/");

    entry = Project::AssetManagerEntry{
      .name = path.filename().string(),
      .path = path.string(),
      .outPath = outPath,
      .romPath = romPath,
      .type = type,
    };

    entry.conf.baseScale = 16;
    auto pathMeta = path;
    pathMeta += ".conf";
    if (fs::exists(pathMeta)) {
      deserialize(entry.conf, pathMeta);
    }

    bool forceSave = false;
    if (entry.conf.uuid == 0) {
      entry.conf.uuid = Utils::Hash::randomU64();
      forceSave = true;
    }

    if (type == Project::FileType::IMAGE) {
      if (entry.path.ends_with(".bci.png")) {
        entry.conf.format = (int)Utils::TexFormat::BCI_256;
      }
    }

    if (type == Project::FileType::FONT && entry.conf.fontCharset.value.empty()) {
      entry.conf.fontCharset.value =
        " !\"#$%&\'()*+,-./"                 "\n"
        "0123456789:;<=>?@"                  "\n"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"  "\n"
        "abcdefghijklmnopqrstuvwxyz{|}~";
    }

    // if this is the first time the asset is seen, we must save the config
    // otherwise any UUID relations may be messed up
    if(forceSave) {
      Utils::FS::saveTextFile(pathMeta, entry.conf.serialize());
    }
    return true;
  }

  bool buildCodeEntry(const fs::path &path, Project::AssetManagerEntry &entry)
  {
    auto code = Utils::FS::loadTextFile(path);

    // Dispatch by namespace marker. Order matters: ::Asset:: must be checked
    // before ::Script:: would fail through, since headers live in P64::Asset
    // and source files in P64::Script / P64::GlobalScript.
    Project::FileType type = Project::FileType::UNKNOWN;
    size_t uuidPos = std::string::npos;

    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

    if (ext == ".h") {
      uuidPos = code.find("::Asset::");
      if (uuidPos == std::string::npos) return false;
      type = Project::FileType::RESOURCE_TYPE;
      uuidPos += 9;
    } else {
      uuidPos = code.find("::Script::");
      if (uuidPos == std::string::npos) {
        type = Project::FileType::CODE_GLOBAL;
        uuidPos = code.find("::GlobalScript::");
        if (uuidPos == std::string::npos) {
          return false;
        }
        uuidPos += 16;
      } else {
        type = Project::FileType::CODE_OBJ;
        uuidPos += 10;
      }
    }

    if (uuidPos + 16 > code.size()) {
      return false;
    }

    auto uuidStr = code.substr(uuidPos, 16);
    uint64_t uuid = 0;
    try {
      uuid = std::stoull(uuidStr, nullptr, 16);
    } catch (...) {
      return false;
    }

    entry = Project::AssetManagerEntry{
      .name = path.filename().string(),
      .path = path.string(),
      .type = type,
      .params = Utils::CPP::parseDataStruct(code, "Data")
    };
    entry.conf.uuid = uuid;

    return true;
  }
}

std::string Project::AssetConf::serialize() const {
  return Utils::JSON::Builder{}
    .set("uuid", uuid)
    .set("format", format)
    .set("baseScale", baseScale)
    .set("compression", static_cast<int>(compression))
    .set("gltfBVH", gltfBVH)
    .set(wavForceMono)
    .set(wavResampleRate)
    .set(wavCompression)
    .set(fontId)
    .set(fontCharset)
    .set("exclude", exclude)
    .set("data", data)
    .toString();
}

Project::AssetManager::AssetManager(Project* pr)
  : project{pr}
{
  defaultObjScript = Utils::FS::loadTextFile("data/scripts/defaultObject.cpp");
  defaultGlobalScript = Utils::FS::loadTextFile("data/scripts/defaultGlobal.cpp");
}

Project::AssetManager::~AssetManager() {

}

void Project::AssetManager::resetDirtyTracking()
{
  dirtyPrefabs.clear();
  dirtyAssetMeta.clear();
  dirtyNodeGraphs.clear();
  savedPrefabState.clear();
  savedAssetMetaState.clear();
  savedNodeGraphState.clear();
  dirtyNodeGraphState.clear();
}

void Project::AssetManager::clearDirtyTracking(uint64_t uuid)
{
  dirtyPrefabs.erase(uuid);
  dirtyAssetMeta.erase(uuid);
  dirtyNodeGraphs.erase(uuid);
  savedPrefabState.erase(uuid);
  savedAssetMetaState.erase(uuid);
  savedNodeGraphState.erase(uuid);
  dirtyNodeGraphState.erase(uuid);
}

void Project::AssetManager::reloadEntry(AssetManagerEntry &entry, const std::string &path)
{
  switch(entry.type)
  {
    case FileType::IMAGE:
    {
      bool isMono = Utils::isTexFormatMono(static_cast<Utils::TexFormat>(entry.conf.format));
      entry.texture = std::make_shared<Renderer::Texture>(ctx.gpu, path, isMono);
    } break;

    case FileType::PREFAB:
    case FileType::WIDGET_BLUEPRINT:
    {
      entry.prefab = std::make_shared<Prefab>();
      entry.prefab->deserialize(Utils::FS::loadTextFile(path));
    } break;

    case FileType::RESOURCE_INSTANCE:
    {
      entry.resource = std::make_shared<Resource::Instance>();
      entry.resource->deserialize(Utils::FS::loadTextFile(path));
    } break;

    case FileType::RESOURCE_TYPE:
    {
      // Only editor-authored types (.p64restype) get reloaded here;
      // header-authored types live under the code path and are seeded once
      // in buildCodeEntry, never re-parsed via reloadEntry.
      auto ext = fs::path{path}.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
      if (ext == ".p64restype") {
        entry.resourceType = std::make_shared<Resource::Type>();
        entry.resourceType->deserialize(Utils::FS::loadTextFile(path));
      }
    } break;

    case FileType::MATERIAL:
    {
      entry.materialAsset = std::make_shared<Assets::MaterialAsset>();
      entry.materialAsset->deserialize(Utils::FS::loadTextFile(path));
      // Self-contained UUID (matches prefab/resource-instance convention).
      // Without this, materialAsset->uuid stays 0 on disk and the asset
      // entry uses the .conf-minted uuid only.
      if (entry.materialAsset->uuid == 0) {
        entry.materialAsset->uuid = entry.conf.uuid;
        Utils::FS::saveTextFile(entry.path, entry.materialAsset->serialize());
      } else {
        entry.conf.uuid = entry.materialAsset->uuid;
      }
    } break;

    case FileType::PARTICLE_SYSTEM:
    {
      entry.particleAsset = std::make_shared<Assets::ParticleSystemAsset>();
      entry.particleAsset->deserialize(Utils::FS::loadTextFile(path));
      if (entry.particleAsset->uuid == 0) {
        entry.particleAsset->uuid = entry.conf.uuid;
        Utils::FS::saveTextFile(entry.path, entry.particleAsset->serialize());
      } else {
        entry.conf.uuid = entry.particleAsset->uuid;
      }
    } break;

    case FileType::SAVE_FILE:
    {
      entry.saveFileAsset = std::make_shared<Assets::SaveFileAsset>();
      entry.saveFileAsset->deserialize(Utils::FS::loadTextFile(path));
      if (entry.saveFileAsset->uuid == 0) {
        entry.saveFileAsset->uuid = entry.conf.uuid;
        Utils::FS::saveTextFile(entry.path, entry.saveFileAsset->serialize());
      } else {
        entry.conf.uuid = entry.saveFileAsset->uuid;
      }
    } break;

    case FileType::MODEL_3D:
    {
      try{
        if(!entry.conf.data.contains("materials")) {
          entry.conf.data["materials"] = nlohmann::json::object();
        }
        auto &savedMats = entry.conf.data["materials"];

        entry.model = {
          .t3dm = T3DM::parseGLTF(path.c_str(), {
            .globalScale = (float)entry.conf.baseScale,
            .animSampleRate = 60,
            .createBVH = entry.conf.gltfBVH,
            .verbose = false,
            .assetPath = "assets/",
            .assetPathFull = fs::absolute(project->getPath() + "/assets").string(),
            .projectPath = fs::path{project->getPath()},
            .getMaterialInfo = [&](const std::string &matName, T3DM::Config::MatInfo &matInfo) -> bool
            {
              if(!savedMats.contains(matName))return false;
              auto &matData = savedMats[matName];
              matInfo.texSizeX = matData["tex0"]["texSize"][0];
              matInfo.texSizeY = matData["tex0"]["texSize"][1];
              matInfo.pointFilter = matData["filter"] != 0;
              return true;
            },
          }), .materials = {},
        };

        for(const auto &t3dMat : entry.model.t3dm.materials) {
          auto &mat = entry.model.materials[t3dMat.first];
          if(savedMats.contains(t3dMat.first)) {
            mat.deserialize(savedMats[t3dMat.first]);
          } else {
            mat.fromT3D(*this, t3dMat.second);
          }
        }

        // Material-asset overlay: each entry under conf.data["materialAssetRefs"]
        // pins a model slot to a referenced .p64mat. The asset's compiled
        // Material wins over inline overrides and over the T3D defaults,
        // so editing the .p64mat propagates to every model on next reload.
        if (entry.conf.data.contains("materialAssetRefs")) {
          auto &refs = entry.conf.data["materialAssetRefs"];
          for (auto it = refs.begin(); it != refs.end(); ++it) {
            const std::string &slotName = it.key();
            uint64_t matAssetUUID = it.value().get<uint64_t>();
            auto matEntry = getEntryByUUID(matAssetUUID);
            if (!matEntry || matEntry->type != FileType::MATERIAL || !matEntry->materialAsset) continue;
            auto slotIt = entry.model.materials.find(slotName);
            if (slotIt == entry.model.materials.end()) continue;
            slotIt->second = matEntry->materialAsset->compiled;
            slotIt->second.isCustom.value = true;
          }
        }

        if (!entry.model.t3dm.models.empty()) {
          if (!entry.mesh3D) {
            entry.mesh3D = std::make_shared<Renderer::N64Mesh>();
          }
          entry.mesh3D->fromT3DM(entry.model, *this);

          // Faithful-but-flagged: the model renders with the real N64 S10.5
          // wrap artifacts intact; we just tell the user why and where so
          // they can rescale UVs or tile the texture.
          const auto &uvDiag = entry.mesh3D->getUvDiag();
          if (uvDiag.outOfRange) {
            std::string msg = entry.name + ": material '" + uvDiag.materialName
              + "' UVs reach ~" + std::to_string(uvDiag.worstPixel)
              + "px, past the N64 S10.5 texel limit (~"
              + std::to_string(Renderer::N64Mesh::S10_5_MAX_PIXEL)
              + "px). Texture will wrap and color-alias on hardware; "
                "rescale UVs or tile the texture.";
            Utils::Logger::log(msg, Utils::Logger::LEVEL_WARN);
            if (!reloadInBulk && ctx.window) {
              Editor::Noti::add(Editor::Noti::Type::WARN, msg);
            }
          }
        }
      } catch (std::exception &e) {
        Utils::Logger::log("Failed to load 3D model asset: " + entry.path + " - " + e.what(), Utils::Logger::LEVEL_ERROR);
        if (reloadInBulk) {
          ++bulkModelFailures;
        } else if (ctx.window) {
          Editor::Noti::add(Editor::Noti::Type::ERROR,
            "Failed to load 3D model: " + entry.name + "\n" + e.what());
        }
      }

      // Collision-only fallback: if parseGLTF produced no usable models
      // (because every primitive is missing a material or the material
      // lacks fast64 extras data), enumerate mesh-node names directly
      // from the glTF so the editor can still expose this asset to the
      // CollisionMesh component. mesh3D is left null on purpose: the
      // collision build path reads vertices straight from the source
      // glTF and does not need T3D mesh data.
      if (entry.model.t3dm.models.empty()) {
        auto stubNames = Build::enumerateGltfMeshNodes(entry.path);
        if (!stubNames.empty()) {
          for (const auto &name : stubNames) {
            entry.model.t3dm.models.push_back({.name = name});
          }
          if (reloadInBulk) {
            ++bulkModelStubs;
          } else if (ctx.window) {
            Editor::Noti::add(Editor::Noti::Type::INFO,
              "Imported as collision-only: " + entry.name
                + "\n(Add fast64 materials to enable rendering.)");
          }
        }
      }
    }
    break;

    default: break;
  }
}

void Project::AssetManager::reload() {
  for (auto &e : entries)e.clear();
  entriesMap.clear();
  resetDirtyTracking();
  watchFiles.clear();
  watchInitialized = false;

  auto assetPath = fs::path{project->getPath()} / "assets";
  if (!fs::exists(assetPath)) {
    fs::create_directory(assetPath);
  }

  // scan all files
  for (const auto &entry : fs::recursive_directory_iterator{assetPath}) {
    if (entry.is_regular_file()) {
      auto path = entry.path();
      watchFiles[path.string()] = Utils::FS::getFileAge(path);
      AssetManagerEntry assetEntry{};
      if (!buildAssetEntry(project, path, assetEntry)) {
        continue;
      }

      if (assetEntry.type == FileType::IMAGE) {
        if (ctx.window) {
          reloadEntry(assetEntry, path.string());
        }
      }

      if (assetEntry.type == FileType::PREFAB
          || assetEntry.type == FileType::WIDGET_BLUEPRINT) {
        reloadEntry(assetEntry, path.string());
        if (assetEntry.prefab) {
          assetEntry.conf.uuid = assetEntry.prefab->uuid.value;
        }
      }

      if (assetEntry.type == FileType::MATERIAL) {
        // Materials must be resolved before MODEL_3D so the second-pass
        // model loader can stamp asset-driven materials onto model slots.
        reloadEntry(assetEntry, path.string());
      }

      if (assetEntry.type == FileType::PARTICLE_SYSTEM) {
        reloadEntry(assetEntry, path.string());
      }

      if (assetEntry.type == FileType::SAVE_FILE) {
        reloadEntry(assetEntry, path.string());
      }

      if (assetEntry.type == FileType::RESOURCE_INSTANCE) {
        reloadEntry(assetEntry, path.string());
        if (assetEntry.resource && assetEntry.resource->uuid != 0) {
          // Self-contained UUID inside the .p64res — match prefab convention,
          // ignore any sidecar .conf uuid that buildAssetEntry may have minted.
          assetEntry.conf.uuid = assetEntry.resource->uuid;
        } else if (assetEntry.resource) {
          // Fresh instance with no uuid yet (just got created): seed it from
          // the conf uuid that buildAssetEntry generated, and persist.
          assetEntry.resource->uuid = assetEntry.conf.uuid;
          Utils::FS::saveTextFile(assetEntry.path, assetEntry.resource->serialize());
        }
      }

      if (assetEntry.type == FileType::RESOURCE_TYPE) {
        // Editor-authored .p64restype: load schema and align uuids.
        // Header-authored .h types come in via buildCodeEntry below and
        // skip this branch (resourceType stays null).
        reloadEntry(assetEntry, path.string());
        if (assetEntry.resourceType && assetEntry.resourceType->uuid != 0) {
          assetEntry.conf.uuid = assetEntry.resourceType->uuid;
        } else if (assetEntry.resourceType) {
          assetEntry.resourceType->uuid = assetEntry.conf.uuid;
          Utils::FS::saveTextFile(assetEntry.path, assetEntry.resourceType->serialize());
        }
      }

      entries[(int)assetEntry.type].push_back(assetEntry);
    }
  }

  auto codePath = getCodePath(project);
  for (const auto &entry : fs::recursive_directory_iterator{codePath}) {
    if (entry.is_regular_file()) {
      auto path = entry.path();
      auto ext = path.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
      if (ext != ".cpp" && ext != ".h") continue;

      watchFiles[path.string()] = Utils::FS::getFileAge(path);
      AssetManagerEntry codeEntry{};
      if (!buildCodeEntry(path, codeEntry)) {
        continue;
      }

      entries[(int)codeEntry.type].push_back(codeEntry);
    }
  }

  // sort by name
  for (auto &typed : entries) {
    std::sort(typed.begin(), typed.end(), [](const AssetManagerEntry &a, const AssetManagerEntry &b) {
      return a.name < b.name;
    });
  }

  for (auto &typed : entries)
  {
    int idx = 0;
    for (auto &entry : typed)
    {
      entriesMap[entry.getUUID()] = {(int)entry.type, idx};
      ++idx;
    }
  }

  // now load models (after all textures are there now)
  reloadInBulk = true;
  bulkModelFailures = 0;
  bulkModelStubs = 0;
  for (auto &typed : entries) {
    for (auto &entry : typed) {
      if (entry.type == FileType::MODEL_3D) {
        reloadEntry(entry, entry.path);
      }
    }
  }
  reloadInBulk = false;

  if (ctx.window && bulkModelFailures > 0) {
    Editor::Noti::add(Editor::Noti::Type::ERROR,
      std::to_string(bulkModelFailures) + " 3D model(s) failed to load. See log for details.");
  }
  if (ctx.window && bulkModelStubs > 0) {
    Editor::Noti::add(Editor::Noti::Type::INFO,
      std::to_string(bulkModelStubs) + " model(s) loaded as collision-only (no fast64 materials).");
  }

  // Resolve prefab variants once all prefab assets are present in the map.
  resolvePrefabVariants();
}

void Project::AssetManager::resolvePrefabVariants()
{
  // Kahn's algorithm: process prefabs whose parents are already resolved.
  // Standalone prefabs (no parent) are resolved-by-default and seed the queue
  // implicitly. Cycles (a → b → a) leave participants unresolved; we log and
  // skip rather than infinite-loop.
  auto &prefabEntries = entries[(int)FileType::PREFAB];
  std::unordered_set<uint64_t> resolved;
  for (auto &e : prefabEntries) {
    if (e.prefab && !e.prefab->isVariant()) resolved.insert(e.prefab->uuid.value);
  }

  bool progress = true;
  while (progress) {
    progress = false;
    for (auto &e : prefabEntries) {
      if (!e.prefab) continue;
      if (!e.prefab->isVariant()) continue;
      if (resolved.contains(e.prefab->uuid.value)) continue;

      uint64_t parentUUID = e.prefab->uuidParentPrefab.value;
      if (!resolved.contains(parentUUID)) continue;

      auto parent = getPrefabByUUID(parentUUID);
      if (!parent) {
        Utils::Logger::log(
          "Prefab variant " + std::to_string(e.prefab->uuid.value)
            + " references missing parent " + std::to_string(parentUUID)
            + " — leaving empty.",
          Utils::Logger::LEVEL_ERROR
        );
        resolved.insert(e.prefab->uuid.value);
        progress = true;
        continue;
      }
      e.prefab->resolveAgainstParent(*parent);
      resolved.insert(e.prefab->uuid.value);
      progress = true;
    }
  }

  for (auto &e : prefabEntries) {
    if (e.prefab && e.prefab->isVariant() && !resolved.contains(e.prefab->uuid.value)) {
      Utils::Logger::log(
        "Prefab variant " + std::to_string(e.prefab->uuid.value)
          + " unresolved (cycle or missing parent chain).",
        Utils::Logger::LEVEL_ERROR
      );
    }
  }
}

std::vector<uint64_t> Project::AssetManager::getPrefabDescendants(uint64_t parentUUID)
{
  std::vector<uint64_t> out;
  auto &prefabEntries = entries[(int)FileType::PREFAB];

  // BFS from parentUUID over the parent->children edge induced by
  // uuidParentPrefab. Cheap; O(prefabs * depth).
  std::vector<uint64_t> frontier{parentUUID};
  while (!frontier.empty()) {
    auto cur = frontier.back();
    frontier.pop_back();
    for (auto &e : prefabEntries) {
      if (!e.prefab) continue;
      if (e.prefab->uuidParentPrefab.value != cur) continue;
      out.push_back(e.prefab->uuid.value);
      frontier.push_back(e.prefab->uuid.value);
    }
  }
  return out;
}

bool Project::AssetManager::pollWatch()
{
  using Clock = std::chrono::steady_clock;
  // Check for changes every 2 seconds
  constexpr auto kMinInterval = std::chrono::milliseconds(2000);

  auto now = Clock::now();
  if (watchInitialized && (now - watchLastCheck) < kMinInterval) {
    return false;
  }
  watchInitialized = true;
  watchLastCheck = now;

  // Snapshot current files so we can diff against watchFiles
  std::unordered_map<std::string, uint64_t> currentFiles{};
  std::vector<std::string> addedAssets{};
  std::vector<std::string> modifiedAssets{};
  std::vector<std::string> addedCode{};
  std::vector<std::string> modifiedCode{};
  std::vector<std::string> removedPaths{};

  // Detect added/modified asset files
  auto assetPath = fs::path{project->getPath()} / "assets";
  if (fs::exists(assetPath)) {
    for (const auto &entry : fs::recursive_directory_iterator{assetPath}) {
      if (!entry.is_regular_file()) continue;
      auto path = entry.path();
      auto pathStr = path.string();
      uint64_t age = Utils::FS::getFileAge(path);

      currentFiles[pathStr] = age;
      auto it = watchFiles.find(pathStr);
      if (it == watchFiles.end()) {
        addedAssets.push_back(pathStr);
      } else if (it->second != age) {
        modifiedAssets.push_back(pathStr);
      }
    }
  }

  // Detect added/modified script files.
  auto codePath = getCodePath(project);
  if (fs::exists(codePath)) {
    for (const auto &entry : fs::recursive_directory_iterator{codePath}) {
      if (!entry.is_regular_file()) continue;
      auto path = entry.path();
      auto ext = path.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
      if (ext != ".cpp" && ext != ".h") continue;

      auto pathStr = path.string();
      uint64_t age = Utils::FS::getFileAge(path);

      currentFiles[pathStr] = age;
      auto it = watchFiles.find(pathStr);
      if (it == watchFiles.end()) {
        addedCode.push_back(pathStr);
      } else if (it->second != age) {
        modifiedCode.push_back(pathStr);
      }
    }
  }

  // Anything missing from the snapshot is treated as removed
  for (const auto &pair : watchFiles) {
    if (currentFiles.find(pair.first) == currentFiles.end()) {
      removedPaths.push_back(pair.first);
    }
  }

  // Bail out if nothing changed
  bool changed = !addedAssets.empty() || !modifiedAssets.empty() ||
                 !addedCode.empty() || !modifiedCode.empty() ||
                 !removedPaths.empty();
  if (!changed) {
    return false;
  }

  // Track which entry lists we need to re-sort
  std::unordered_set<int> touchedTypes{};
  std::vector<std::string> modelReloadPaths{};

  // Remove an entry by absolute path across all lists
  auto removeEntryByPath = [&](const std::string &pathStr) {
    fs::path pathIn{pathStr};
    for (size_t typeIdx = 0; typeIdx < entries.size(); ++typeIdx) {
      auto &typed = entries[typeIdx];
      for (size_t i = 0; i < typed.size(); ++i) {
        if (fs::path{typed[i].path} == pathIn) {
          auto uuid = typed[i].getUUID();
          clearDirtyTracking(uuid);
          typed.erase(typed.begin() + i);
          touchedTypes.insert(static_cast<int>(typeIdx));
          return true;
        }
      }
    }
    return false;
  };

  for (const auto &pathStr : removedPaths) {
    removeEntryByPath(pathStr);
  }

  // Rebuild a single asset entry and reload if needed
  auto addOrUpdateAsset = [&](const std::string &pathStr) {
    AssetManagerEntry newEntry{};
    if (!buildAssetEntry(project, fs::path{pathStr}, newEntry)) {
      return;
    }

    removeEntryByPath(pathStr);
    entries[static_cast<int>(newEntry.type)].push_back(std::move(newEntry));
    touchedTypes.insert(static_cast<int>(newEntry.type));

    auto entry = getByPath(pathStr);
    if (!entry) {
      return;
    }

    if (entry->type == FileType::MODEL_3D) {
      modelReloadPaths.push_back(pathStr);
      return;
    }

    if (entry->type == FileType::IMAGE
      || entry->type == FileType::PREFAB
      || entry->type == FileType::WIDGET_BLUEPRINT
      || entry->type == FileType::RESOURCE_INSTANCE
      || entry->type == FileType::MATERIAL
      || entry->type == FileType::PARTICLE_SYSTEM
      || entry->type == FileType::SAVE_FILE)
    {
      reloadEntry(*entry, entry->path);
      if ((entry->type == FileType::PREFAB
           || entry->type == FileType::WIDGET_BLUEPRINT)
          && entry->prefab) {
        entry->conf.uuid = entry->prefab->uuid.value;
      }
      if (entry->type == FileType::RESOURCE_INSTANCE && entry->resource && entry->resource->uuid != 0) {
        entry->conf.uuid = entry->resource->uuid;
      }
    }

    auto uuid = entry->getUUID();
    clearDirtyTracking(uuid);
  };

  // Rebuild a single script entry
  auto addOrUpdateCode = [&](const std::string &pathStr) {
    AssetManagerEntry newEntry{};
    if (!buildCodeEntry(fs::path{pathStr}, newEntry)) {
      return;
    }

    removeEntryByPath(pathStr);
    entries[static_cast<int>(newEntry.type)].push_back(std::move(newEntry));
    touchedTypes.insert(static_cast<int>(newEntry.type));
  };

  // Add or update all the assets and scripts that were found
  for (const auto &pathStr : addedAssets) {
    addOrUpdateAsset(pathStr);
  }
  for (const auto &pathStr : modifiedAssets) {
    addOrUpdateAsset(pathStr);
  }
  for (const auto &pathStr : addedCode) {
    addOrUpdateCode(pathStr);
  }
  for (const auto &pathStr : modifiedCode) {
    addOrUpdateCode(pathStr);
  }

  // Reload models after texture updates are applied
  for (const auto &pathStr : modelReloadPaths) {
    auto entry = getByPath(pathStr);
    if (entry) {
      reloadEntry(*entry, entry->path);
    }
  }

  //sort by name
  for (size_t typeIdx = 0; typeIdx < entries.size(); ++typeIdx) {
    if (touchedTypes.find(static_cast<int>(typeIdx)) == touchedTypes.end()) {
      continue;
    }
    auto &typed = entries[typeIdx];
    std::sort(typed.begin(), typed.end(), [](const AssetManagerEntry &a, const AssetManagerEntry &b) {
      return a.name < b.name;
    });
  }

  // Rebuild UUID lookup after edits
  entriesMap.clear();
  for (auto &typed : entries) {
    int idx = 0;
    for (auto &entry : typed) {
      entriesMap[entry.getUUID()] = {(int)entry.type, idx};
      ++idx;
    }
  }

  // Update watcher snapshot for next poll
  watchFiles = std::move(currentFiles);
  return true;
}

void Project::AssetManager::reloadAssetByUUID(uint64_t uuid) {
  auto asset = getEntryByUUID(uuid);
  if (!asset)return;
  reloadEntry(*asset, asset->path);

  // The asset's visuals changed, so any cached thumbnail is now stale.
  if (ctx.thumbnails)ctx.thumbnails->invalidate(uuid);
}

const std::shared_ptr<Renderer::Texture> & Project::AssetManager::getFallbackTexture()
{
  if(!fallbackTex) {
    fallbackTex = std::make_shared<Renderer::Texture>(ctx.gpu, "data/img/fallback.png");
  }
  return fallbackTex;
}

void Project::AssetManager::save()
{
  std::vector<uint64_t> prefabsToSave{dirtyPrefabs.begin(), dirtyPrefabs.end()};
  for (auto uuid : prefabsToSave) {
    auto entry = getEntryByUUID(uuid);
    bool isPrefabLike = entry
      && (entry->type == FileType::PREFAB
          || entry->type == FileType::WIDGET_BLUEPRINT);
    if (!isPrefabLike || !entry->prefab) {
      dirtyPrefabs.erase(uuid);
      savedPrefabState.erase(uuid);
      continue;
    }

    entry->prefab->save(entry->path);
    dirtyPrefabs.erase(uuid);
    savedPrefabState.erase(uuid);
  }

  std::vector<uint64_t> assetsToSave{dirtyAssetMeta.begin(), dirtyAssetMeta.end()};
  for (auto uuid : assetsToSave) {
    auto entry = getEntryByUUID(uuid);
    if (!entry
      || entry->type == FileType::UNKNOWN
      || entry->type == FileType::CODE_OBJ
      || entry->type == FileType::CODE_GLOBAL
      || entry->type == FileType::PREFAB
      || entry->type == FileType::WIDGET_BLUEPRINT
      || entry->type == FileType::MATERIAL
      || entry->type == FileType::PARTICLE_SYSTEM
      || entry->type == FileType::SAVE_FILE)
    {
      dirtyAssetMeta.erase(uuid);
      savedAssetMetaState.erase(uuid);
      continue;
    }

    auto pathMeta = entry->path + ".conf";
    auto json = entry->conf.serialize();

    Utils::Logger::log("Asset meta-data changed, forcing recompile: " + entry->outPath, Utils::Logger::LEVEL_INFO);
    fs::remove(project->getPath() + "/" + entry->outPath);
    Utils::FS::saveTextFile(pathMeta, json);

    dirtyAssetMeta.erase(uuid);
    savedAssetMetaState.erase(uuid);
  }

  std::vector<uint64_t> graphsToSave{dirtyNodeGraphs.begin(), dirtyNodeGraphs.end()};
  for (auto uuid : graphsToSave) {
    auto entry = getEntryByUUID(uuid);
    auto itState = dirtyNodeGraphState.find(uuid);

    if (!entry || entry->type != FileType::NODE_GRAPH || itState == dirtyNodeGraphState.end()) {
      clearNodeGraphDirty(uuid);
      continue;
    }

    Utils::FS::saveTextFile(entry->path, itState->second);
    markNodeGraphSaved(uuid, itState->second);
  }
}

void Project::AssetManager::markPrefabDirty(uint64_t uuid)
{
  auto entry = getEntryByUUID(uuid);
  bool isPrefabLike = entry
    && (entry->type == FileType::PREFAB
        || entry->type == FileType::WIDGET_BLUEPRINT);
  if (!isPrefabLike || !entry->prefab) {
    return;
  }

  auto currentState = entry->prefab->serialize();
  updateDirtyState(uuid, currentState, dirtyPrefabs, savedPrefabState, [&]() {
    return Utils::FS::loadTextFile(entry->path);
  });
}

void Project::AssetManager::markAssetMetaDirty(uint64_t uuid)
{
  auto entry = getEntryByUUID(uuid);
  if (!entry
    || entry->type == FileType::UNKNOWN
    || entry->type == FileType::CODE_OBJ
    || entry->type == FileType::CODE_GLOBAL
    || entry->type == FileType::PREFAB
    || entry->type == FileType::WIDGET_BLUEPRINT
    || entry->type == FileType::MATERIAL)
  {
    return;
  }

  auto currentState = entry->conf.serialize();
  updateDirtyState(uuid, currentState, dirtyAssetMeta, savedAssetMetaState, [&]() {
    return Utils::FS::loadTextFile(entry->path + ".conf");
  });
}

void Project::AssetManager::markNodeGraphDirty(uint64_t uuid, const std::string &currentState)
{
  auto entry = getEntryByUUID(uuid);
  if (!entry || entry->type != FileType::NODE_GRAPH) {
    return;
  }

  updateDirtyState(uuid, currentState, dirtyNodeGraphs, savedNodeGraphState, [&]() {
    return Utils::FS::loadTextFile(entry->path);
  });

  if (dirtyNodeGraphs.contains(uuid)) {
    dirtyNodeGraphState[uuid] = currentState;
  } else {
    dirtyNodeGraphState.erase(uuid);
  }
}

void Project::AssetManager::markNodeGraphSaved(uint64_t uuid, const std::string &savedState)
{
  dirtyNodeGraphs.erase(uuid);
  dirtyNodeGraphState.erase(uuid);
  savedNodeGraphState[uuid] = savedState;
}

void Project::AssetManager::clearNodeGraphDirty(uint64_t uuid)
{
  dirtyNodeGraphs.erase(uuid);
  dirtyNodeGraphState.erase(uuid);
  savedNodeGraphState.erase(uuid);
}

bool Project::AssetManager::createScript(const std::string &name, bool isGlobal, const std::string &subDir) {
  // Catch forbidden characters
  if (name.find_first_of("/\\:*?\"<>|") != std::string::npos) {
    return false;
  }

  auto codePath = getCodePath(project);
  fs::path dirPath = codePath;

  if (!subDir.empty()) {
    fs::path relPath{subDir};
    if (!relPath.is_absolute()) {
      relPath = relPath.lexically_normal();
      bool hasParent = false;
      for (const auto &part : relPath) {
        if (part == "..") {
          hasParent = true;
          break;
        }
      }
      if (!hasParent) {
        dirPath /= relPath;
      }
    }
  }

  fs::create_directories(dirPath);

  auto filePath = dirPath / (name + ".cpp");

  uint64_t uuid = Utils::Hash::randomU64();
  auto uuidStr = std::format("{:016X}", uuid);
  uuidStr[0] = 'C'; // avoid leading numbers since it's used as a namespace name

  if (fs::exists(filePath)) {
    return false;
  }

  auto code = isGlobal ? defaultGlobalScript : defaultObjScript;
  code = Utils::replaceAll(code, "__UUID__", uuidStr);

  Utils::FS::saveTextFile(filePath, code);
  if (!fs::exists(filePath)) {
    return false;
  }

  reload();
  return true;
}

uint64_t Project::AssetManager::createNodeGraph(const std::string &name)
{
  auto assetPath = getAssetPath(project);
  auto filePath = assetPath / (name + ".p64graph");

  if (fs::exists(filePath))return 0;

  Utils::FS::saveTextFile(filePath, "{\"nodes\": [], \"links\": []}");
  reload();

  auto entry = getByName(name + ".p64graph");
  return entry ? entry->getUUID() : 0;
}

uint64_t Project::AssetManager::createWidgetBlueprint(const std::string &name)
{
  if (name.empty()) return 0;
  if (name.find_first_of("/\\:*?\"<>|") != std::string::npos) return 0;

  auto assetPath = getAssetPath(project);
  auto filePath = assetPath / (name + ".p64widget");
  if (fs::exists(filePath)) return 0;

  ::Project::Prefab widget{};
  widget.uuid.value = (uint32_t)Utils::Hash::randomU64();
  widget.obj.name = name;
  widget.obj.isCanvas2D = true;

  Utils::FS::saveTextFile(filePath, widget.serialize());
  reload();
  auto entry = getByPath(filePath.string());
  return entry ? entry->getUUID() : 0;
}

uint64_t Project::AssetManager::createMaterial(const std::string &name)
{
  if (name.empty()) return 0;
  if (name.find_first_of("/\\:*?\"<>|") != std::string::npos) return 0;

  auto assetPath = getAssetPath(project);
  auto filePath = assetPath / (name + ".p64mat");
  if (fs::exists(filePath)) return 0;

  ::Project::Assets::MaterialAsset asset{};
  asset.uuid = Utils::Hash::randomU64();
  asset.graphJSON = R"({"nodes":[],"links":[]})";
  // Compile defaults match Graph::compile()'s no-sink baseline.
  asset.compiled.persp.value = true;
  asset.compiled.zmode.value = 0b11;
  asset.compiled.dither.value = 15;
  asset.compiled.primColor.value = {0.0f, 0.0f, 0.0f, 1.0f};
  asset.compiled.envColor.value  = {0.5f, 0.5f, 0.5f, 1.0f};
  asset.compiled.isCustom.value = true;

  Utils::FS::saveTextFile(filePath, asset.serialize());
  reload();
  auto entry = getByPath(filePath.string());
  return entry ? entry->getUUID() : 0;
}

uint64_t Project::AssetManager::createResourceType(
  const std::string &name, const std::string &subDir)
{
  if (name.empty()) return 0;
  if (name.find_first_of("/\\:*?\"<>|") != std::string::npos) return 0;

  auto assetPath = getAssetPath(project);
  fs::path dirPath = assetPath;
  if (!subDir.empty()) {
    fs::path relPath{subDir};
    if (!relPath.is_absolute()) {
      relPath = relPath.lexically_normal();
      bool hasParent = false;
      for (const auto &part : relPath) {
        if (part == "..") { hasParent = true; break; }
      }
      if (!hasParent) dirPath /= relPath;
    }
  }
  fs::create_directories(dirPath);

  auto filePath = dirPath / (name + ".p64restype");
  if (fs::exists(filePath)) return 0;

  Resource::Type type{};
  type.uuid = Utils::Hash::randomU64();
  type.name = name;
  Utils::FS::saveTextFile(filePath, type.serialize());

  reload();
  auto entry = getByPath(filePath.string());
  return entry ? entry->getUUID() : 0;
}

uint64_t Project::AssetManager::createResourceInstance(
  const std::string &name, uint64_t typeUuid, const std::string &subDir)
{
  if (name.empty() || typeUuid == 0) return 0;
  if (name.find_first_of("/\\:*?\"<>|") != std::string::npos) return 0;

  auto assetPath = getAssetPath(project);
  fs::path dirPath = assetPath;

  if (!subDir.empty()) {
    fs::path relPath{subDir};
    if (!relPath.is_absolute()) {
      relPath = relPath.lexically_normal();
      bool hasParent = false;
      for (const auto &part : relPath) {
        if (part == "..") { hasParent = true; break; }
      }
      if (!hasParent) dirPath /= relPath;
    }
  }
  fs::create_directories(dirPath);

  auto filePath = dirPath / (name + ".p64res");
  if (fs::exists(filePath)) return 0;

  Resource::Instance inst{};
  inst.uuid = Utils::Hash::randomU64();
  inst.typeUuid = typeUuid;
  Utils::FS::saveTextFile(filePath, inst.serialize());

  reload();
  auto entry = getByPath(filePath.string());
  return entry ? entry->getUUID() : 0;
}

uint64_t Project::AssetManager::createParticleSystem(
  const std::string &name, const std::string &subDir)
{
  if (name.empty()) return 0;
  if (name.find_first_of("/\\:*?\"<>|") != std::string::npos) return 0;

  auto assetPath = getAssetPath(project);
  fs::path dirPath = assetPath;
  if (!subDir.empty()) {
    fs::path relPath{subDir};
    if (!relPath.is_absolute()) {
      relPath = relPath.lexically_normal();
      bool hasParent = false;
      for (const auto &part : relPath) {
        if (part == "..") { hasParent = true; break; }
      }
      if (!hasParent) dirPath /= relPath;
    }
  }
  fs::create_directories(dirPath);

  auto filePath = dirPath / (name + ".p64ptx");
  if (fs::exists(filePath)) return 0;

  ::Project::Assets::ParticleSystemAsset asset{};
  asset.uuid = Utils::Hash::randomU64();
  Utils::FS::saveTextFile(filePath, asset.serialize());

  reload();
  auto entry = getByPath(filePath.string());
  return entry ? entry->getUUID() : 0;
}

uint64_t Project::AssetManager::createSaveFile(
  const std::string &name, const std::string &subDir)
{
  if (name.empty()) return 0;
  if (name.find_first_of("/\\:*?\"<>|") != std::string::npos) return 0;

  auto assetPath = getAssetPath(project);
  fs::path dirPath = assetPath;
  if (!subDir.empty()) {
    fs::path relPath{subDir};
    if (!relPath.is_absolute()) {
      relPath = relPath.lexically_normal();
      bool hasParent = false;
      for (const auto &part : relPath) {
        if (part == "..") { hasParent = true; break; }
      }
      if (!hasParent) dirPath /= relPath;
    }
  }
  fs::create_directories(dirPath);

  auto filePath = dirPath / (name + ".p64save");
  if (fs::exists(filePath)) return 0;

  ::Project::Assets::SaveFileAsset asset{};
  asset.uuid = Utils::Hash::randomU64();
  // Sanitize the name into a C++ identifier for the generated namespace.
  // Keep alnum + underscore; lead with underscore if first char is a digit.
  std::string g;
  g.reserve(name.size());
  for (char c : name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_') {
      g.push_back(c);
    } else {
      g.push_back('_');
    }
  }
  if (!g.empty() && g[0] >= '0' && g[0] <= '9') g.insert(g.begin(), '_');
  asset.groupName = g;
  Utils::FS::saveTextFile(filePath, asset.serialize());

  reload();
  auto entry = getByPath(filePath.string());
  return entry ? entry->getUUID() : 0;
}

Project::AssetManagerEntry *Project::AssetManager::getByPath(const std::string &path)
{
  fs::path pathIn{path};
  for (auto &typed : entries) {
    for (auto &entry : typed) {
      if (fs::path{entry.path} == pathIn) {
        return &entry;
      }
    }
  }
  return nullptr;
}