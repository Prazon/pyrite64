/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <chrono>

#include "../renderer/n64Mesh.h"
#include "../renderer/object.h"
#include "../utils/codeParser.h"
#include "../renderer/texture.h"
#include "assets/materialAsset.h"
#include "assets/model3d.h"
#include "assets/particleSystemAsset.h"
#include "assets/saveFileAsset.h"
#include "assets/resourceInstance.h"
#include "assets/resourceType.h"
#include "scene/prefab.h"
#include "tiny3d/tools/gltf_importer/src/structs.h"

namespace Project
{
  class Project;

  enum class ComprTypes : int
  {
    DEFAULT = 0,
    LEVEL_0,
    LEVEL_1,
    LEVEL_2,
    LEVEL_3,
  };

  enum class FileType : int
  {
    UNKNOWN = 0,
    IMAGE,
    AUDIO,
    FONT,
    MODEL_3D,
    CODE_OBJ,
    CODE_GLOBAL,
    PREFAB,
    NODE_GRAPH,
    MUSIC_XM,
    RESOURCE_TYPE,     // .h header in P64::Asset::C… namespace, defines a Data struct
    RESOURCE_INSTANCE, // .p64res json file, holds field values for one RESOURCE_TYPE
    MATERIAL,          // .p64mat material asset (node-graph driven)
    WIDGET_BLUEPRINT,  // .p64widget HUD/menu blueprint (a 2D-only Prefab variant)
    PARTICLE_SYSTEM,   // .p64ptx sprite particle-system asset (CPU-sim emitter)
    SAVE_FILE,         // .p64save typed key/value EEPROM schema (Game::Save codegen)

    _SIZE
  };

  struct AssetConf
  {
    uint64_t uuid{0};
    int format{0};
    int baseScale{0};
    bool gltfBVH{0};

    ComprTypes compression{ComprTypes::DEFAULT};
    bool exclude{false};

    PROP_BOOL(wavForceMono);
    PROP_U32(wavResampleRate);
    PROP_S32(wavCompression);

    PROP_U32(fontId);
    PROP_STRING(fontCharset);

    // extra arbitrary data assets can store
    nlohmann::json data{};

    std::string serialize() const;
  };

  struct AssetManagerEntry
  {
    std::string name{};
    std::string path{};
    std::string outPath{};
    std::string romPath{};
    FileType type{};
    std::shared_ptr<Renderer::Texture> texture{nullptr};
    Assets::Model3D model{};
    std::shared_ptr<Renderer::N64Mesh> mesh3D{};
    std::shared_ptr<Prefab> prefab{nullptr};
    std::shared_ptr<Resource::Instance> resource{nullptr};
    // Set only for editor-authored RESOURCE_TYPE assets (.p64restype). When
    // null on a RESOURCE_TYPE entry, the type is header-authored and `params`
    // holds the parsed C++ struct schema.
    std::shared_ptr<Resource::Type> resourceType{nullptr};
    std::shared_ptr<Assets::MaterialAsset> materialAsset{nullptr};
    std::shared_ptr<Assets::ParticleSystemAsset> particleAsset{nullptr};
    std::shared_ptr<Assets::SaveFileAsset> saveFileAsset{nullptr};
    AssetConf conf{};
    Utils::CPP::Struct params{};

    uint64_t getUUID() const { return conf.uuid; }

    // imgui selectbox:
    uint64_t getId() const { return conf.uuid; }
    const std::string &getName() const { return name; }
  };

  class AssetManager
  {
    private:
      Project *project;
      std::array<std::vector<AssetManagerEntry>, static_cast<size_t>(FileType::_SIZE)> entries{};

      std::unordered_map<std::string, uint64_t> watchFiles{};
      std::chrono::steady_clock::time_point watchLastCheck{};
      bool watchInitialized{false};

      std::unordered_set<uint64_t> dirtyPrefabs{};
      std::unordered_set<uint64_t> dirtyAssetMeta{};
      std::unordered_set<uint64_t> dirtyNodeGraphs{};
      std::unordered_map<uint64_t, std::string> savedPrefabState{};
      std::unordered_map<uint64_t, std::string> savedAssetMetaState{};
      std::unordered_map<uint64_t, std::string> savedNodeGraphState{};
      std::unordered_map<uint64_t, std::string> dirtyNodeGraphState{};

      std::string defaultObjScript{};
      std::string defaultGlobalScript{};
      std::shared_ptr<Renderer::Texture> fallbackTex{};

      // True only while reload() is iterating its bulk MODEL_3D pass.
      // MODEL_3D loader checks this to suppress per-asset error popups
      // (a single summary popup is emitted by reload() instead).
      bool reloadInBulk{false};
      int bulkModelFailures{0};
      int bulkModelStubs{0};

      void reloadEntry(AssetManagerEntry &entry, const std::string &path);
      void resetDirtyTracking();
      void clearDirtyTracking(uint64_t uuid);
    public:
      std::unordered_map<uint64_t, std::pair<int, int>> entriesMap{};
      //std::unordered_map<uint64_t, int> entriesMapScript{};

      explicit AssetManager(Project *pr);
      ~AssetManager();

      void reload();
      void reloadAssetByUUID(uint64_t uuid);
      bool pollWatch();
      bool isDirty() const {
        return !dirtyPrefabs.empty() || !dirtyAssetMeta.empty() || !dirtyNodeGraphs.empty();
      }
      bool isNodeGraphDirty(uint64_t uuid) const {
        return dirtyNodeGraphs.contains(uuid);
      }

      [[nodiscard]] const auto& getEntries() const {
        return entries;
      }
      [[nodiscard]] const std::vector<AssetManagerEntry>& getTypeEntries(FileType type) const {
        return entries[static_cast<int>(type)];
      }

      AssetManagerEntry* getByName(const std::string &name) {
        for (auto &typed : entries) {
          for (auto &entry : typed) {
            if (entry.name == name) {
              return &entry;
            }
          }
        }
        return nullptr;
      }

      AssetManagerEntry* getByPath(const std::string &path);

      AssetManagerEntry* getEntryByUUID(uint64_t uuid) {
        auto it = entriesMap.find(uuid);
        if (it == entriesMap.end()) {
          return nullptr;
        }
        return &entries[it->second.first][it->second.second];
      }

      std::shared_ptr<Prefab> getPrefabByUUID(uint64_t uuid) {
        auto entry = getEntryByUUID(uuid);
        if (!entry || entry->type != FileType::PREFAB) {
          return nullptr;
        }
        return entry->prefab;
      }

      // WidgetBlueprint share the Prefab on-disk shape; FileType discriminates
      // them so widget assets don't show up in 3D prefab pickers and vice
      // versa. The same `entry->prefab` field is reused since the structures
      // are identical.
      std::shared_ptr<Prefab> getWidgetByUUID(uint64_t uuid) {
        auto entry = getEntryByUUID(uuid);
        if (!entry || entry->type != FileType::WIDGET_BLUEPRINT) {
          return nullptr;
        }
        return entry->prefab;
      }

      const std::shared_ptr<Renderer::Texture> &getFallbackTexture();

      // Walks all prefab assets and resolves variants against their parents
      // in dependency order. Called once at the end of reload() so every
      // variant's `obj` reflects parent + patch by the time scenes load.
      void resolvePrefabVariants();

      // Returns prefab uuids that descend from `parentUUID` via uuidParentPrefab
      // chains (transitive). Excludes parentUUID itself. Order is unspecified.
      std::vector<uint64_t> getPrefabDescendants(uint64_t parentUUID);

      void markPrefabDirty(uint64_t uuid);
      void markAssetMetaDirty(uint64_t uuid);
      void markNodeGraphDirty(uint64_t uuid, const std::string &currentState);
      void markNodeGraphSaved(uint64_t uuid, const std::string &savedState);
      void clearNodeGraphDirty(uint64_t uuid);

      void save();

      bool createScript(const std::string &name, bool isGlobal, const std::string &subDir = {});
      uint64_t createNodeGraph(const std::string &name);
      // Creates a fresh .p64mat asset under <project>/assets with a seeded
      // empty graph (one Output node) and a default-constructed compiled
      // Material. Returns the new asset's UUID, or 0 on failure.
      uint64_t createMaterial(const std::string &name);
      // Creates a fresh .p64widget under <project>/assets with a single
      // canvas root Object (isCanvas2D=true). Same on-disk format as Prefab
      // so the existing serializer can be reused. Returns the new asset's
      // UUID, or 0 on failure.
      uint64_t createWidgetBlueprint(const std::string &name);
      uint64_t createResourceInstance(const std::string &name, uint64_t typeUuid, const std::string &subDir = {});
      // Creates a fresh .p64ptx particle-system asset under <project>/assets
      // with all defaults from ParticleSystemAsset's in-struct initializers.
      // Returns the new asset's UUID, or 0 on validation/IO failure.
      uint64_t createParticleSystem(const std::string &name, const std::string &subDir = {});
      // Creates a fresh .p64save asset under <project>/assets/<subDir>/<name>.p64save
      // with no fields. The group name defaults to a sanitized form of `name`.
      // Returns the new asset's UUID, or 0 on validation/IO failure.
      uint64_t createSaveFile(const std::string &name, const std::string &subDir = {});
      // Creates an editor-authored RESOURCE_TYPE schema (.p64restype) under
      // <project>/assets/<subDir>/<name>.p64restype with no fields. Returns
      // the new asset's UUID, or 0 on validation/IO failure.
      uint64_t createResourceType(const std::string &name, const std::string &subDir = {});
  };
}
