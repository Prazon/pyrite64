#include "cliCommands.h"

#include "argparse/argparse.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../context.h"
#include "../project/project.h"
#include "../project/assetManager.h"
#include "../project/component/components.h"
#include "../project/scene/object.h"
#include "../project/scene/prefab.h"
#include "../project/scene/scene.h"
#include "../project/scene/sceneManager.h"
#include "../project/assets/resourceInstance.h"
#include "../project/assets/resourceType.h"
#include "../project/prefabFunctions.h"
#include "../project/prefabScaffolder.h"
#include "../project/graph/graph.h"
#include "../project/materialGraph/graph.h"
#include "../project/assets/materialAsset.h"
#include "../project/compile/compileErrors.h"
#include "../utils/fs.h"
#include "../utils/hash.h"
#include "../utils/json.h"
#include "../utils/jsonBuilder.h"
#include "../utils/logger.h"

namespace fs = std::filesystem;

namespace
{
  void emitJSON(const nlohmann::json &j) { fputs(j.dump(2).c_str(), stdout); fputc('\n', stdout); }
  void emitErr(const std::string &msg) { fputs(("error: " + msg + "\n").c_str(), stderr); }

  std::optional<uint64_t> tryParseUUID(const std::string &s)
  {
    if (s.empty()) return std::nullopt;
    try {
      if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return std::stoull(s.substr(2), nullptr, 16);
      }
      bool allDigits = std::all_of(s.begin(), s.end(),
                                   [](unsigned char c){ return std::isdigit(c) != 0; });
      if (allDigits) return std::stoull(s, nullptr, 10);
      bool allHex = s.size() == 16 && std::all_of(s.begin(), s.end(),
                    [](unsigned char c){ return std::isxdigit(c) != 0; });
      if (allHex) return std::stoull(s, nullptr, 16);
    } catch (...) {}
    return std::nullopt;
  }

  // Asset resolver: try uuid first, then exact name (with extension), then
  // stem match, then path tail. Optional type filter narrows the search.
  Project::AssetManagerEntry *resolveAsset(
    Project::Project &proj,
    const std::string &key,
    Project::FileType filterType = Project::FileType::UNKNOWN
  ) {
    auto &am = proj.getAssets();
    if (auto u = tryParseUUID(key)) {
      auto *e = am.getEntryByUUID(*u);
      if (e && (filterType == Project::FileType::UNKNOWN || e->type == filterType)) return e;
    }
    if (auto *e = am.getByName(key)) {
      if (filterType == Project::FileType::UNKNOWN || e->type == filterType) return e;
    }
    // Stem / path-tail fallback: scan via const view, then re-fetch the
    // matching entry via getEntryByUUID for a non-const handle.
    uint64_t matchedUUID = 0;
    const auto &entries = am.getEntries();
    for (int i = 0; i < (int)Project::FileType::_SIZE && matchedUUID == 0; ++i) {
      auto t = (Project::FileType)i;
      if (filterType != Project::FileType::UNKNOWN && t != filterType) continue;
      for (const auto &entry : entries[i]) {
        if (fs::path(entry.name).stem().string() == key ||
            (entry.path.size() >= key.size() &&
             entry.path.compare(entry.path.size() - key.size(), key.size(), key) == 0)) {
          matchedUUID = entry.conf.uuid;
          break;
        }
      }
    }
    if (matchedUUID) return am.getEntryByUUID(matchedUUID);
    return nullptr;
  }

  // Prefab-or-Widget resolver: widgets share the prefab on-disk shape, so
  // every prefab-* CLI command should also accept .p64widget assets. Use
  // this in place of resolveAsset(..., FileType::PREFAB) for the prefab
  // editing commands; CLI users invoking widget-* aliases get the same
  // behavior, and direct prefab-* invocations still keep working on
  // .prefab files because the resolver also accepts those.
  Project::AssetManagerEntry *resolvePrefabOrWidget(
    Project::Project &proj,
    const std::string &key
  ) {
    if (auto *e = resolveAsset(proj, key, Project::FileType::PREFAB)) return e;
    if (auto *e = resolveAsset(proj, key, Project::FileType::WIDGET_BLUEPRINT)) return e;
    return nullptr;
  }

  const char *fileTypeName(Project::FileType t)
  {
    switch (t) {
      case Project::FileType::UNKNOWN:           return "unknown";
      case Project::FileType::IMAGE:             return "image";
      case Project::FileType::AUDIO:             return "audio";
      case Project::FileType::FONT:              return "font";
      case Project::FileType::MODEL_3D:          return "model3d";
      case Project::FileType::CODE_OBJ:          return "code";
      case Project::FileType::CODE_GLOBAL:       return "codeGlobal";
      case Project::FileType::PREFAB:            return "prefab";
      case Project::FileType::NODE_GRAPH:        return "nodeGraph";
      case Project::FileType::MUSIC_XM:          return "musicXM";
      case Project::FileType::RESOURCE_TYPE:     return "resourceType";
      case Project::FileType::RESOURCE_INSTANCE: return "resourceInstance";
      case Project::FileType::MATERIAL:          return "material";
      case Project::FileType::WIDGET_BLUEPRINT:  return "widgetBlueprint";
      case Project::FileType::PARTICLE_SYSTEM:   return "particleSystem";
      case Project::FileType::SAVE_FILE:         return "saveFile";
      default: return "?";
    }
  }

  Project::FileType parseFileTypeName(const std::string &s)
  {
    for (int i = 0; i < (int)Project::FileType::_SIZE; ++i) {
      if (s == fileTypeName((Project::FileType)i)) return (Project::FileType)i;
    }
    return Project::FileType::UNKNOWN;
  }

  int findCompId(const std::string &key)
  {
    if (auto u = tryParseUUID(key)) {
      int v = (int)*u;
      if (v >= 0 && v < (int)Project::Component::TABLE.size()) return v;
    }
    for (const auto &info : Project::Component::TABLE) {
      if (key == info.name) return info.id;
    }
    // case-insensitive / strip-spaces fallback for "Sprite Billboard" vs "spritebillboard".
    auto norm = [](std::string s) {
      s.erase(std::remove_if(s.begin(), s.end(),
        [](unsigned char c){ return std::isspace(c) || c == '_' || c == '-'; }), s.end());
      std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return (char)std::tolower(c); });
      return s;
    };
    auto k = norm(key);
    for (const auto &info : Project::Component::TABLE) {
      if (norm(info.name) == k) return info.id;
    }
    return -1;
  }

  // Recursive uuid lookup. Object uuids are uint32_t but we accept the full
  // uint64_t range so callers can pass a uuid that came back from a JSON
  // describe response without having to truncate it.
  Project::Object *findObjectByUUIDRecursive(Project::Object *node, uint64_t uuid)
  {
    if (!node) return nullptr;
    if ((uint64_t)node->uuid == uuid) return node;
    for (auto &c : node->children) {
      if (auto *hit = findObjectByUUIDRecursive(c.get(), uuid)) return hit;
    }
    return nullptr;
  }

  Project::Object *findObjectByUUIDRecursiveWithParent(
    Project::Object *node, uint64_t uuid,
    Project::Object **outParent, size_t *outIdx)
  {
    if (!node) return nullptr;
    if ((uint64_t)node->uuid == uuid) {
      *outParent = nullptr; *outIdx = 0; return node;
    }
    for (size_t i = 0; i < node->children.size(); ++i) {
      auto *child = node->children[i].get();
      if ((uint64_t)child->uuid == uuid) {
        *outParent = node; *outIdx = i; return child;
      }
      Project::Object *grandParent = nullptr; size_t grandIdx = 0;
      auto *hit = findObjectByUUIDRecursiveWithParent(child, uuid, &grandParent, &grandIdx);
      if (hit) {
        *outParent = grandParent ? grandParent : child;
        if (!grandParent) {
          // Direct child of `child` was the hit; restate parent/idx.
          *outParent = child; *outIdx = grandIdx;
        } else {
          *outParent = grandParent; *outIdx = grandIdx;
        }
        return hit;
      }
    }
    return nullptr;
  }

  // Detect the obj://<uuid> addressing form. Hex or decimal accepted; the
  // hash form lets headless callers stash the uuid that came back from a
  // describe call and reuse it across rename/move operations without the
  // path-string fragility.
  bool tryParseObjUUID(const std::string &path, uint64_t &out)
  {
    static const std::string kPrefix = "obj://";
    if (path.size() <= kPrefix.size()) return false;
    if (path.compare(0, kPrefix.size(), kPrefix) != 0) return false;
    auto u = tryParseUUID(path.substr(kPrefix.size()));
    if (!u) return false;
    out = *u;
    return true;
  }

  // Slash-separated name path. Empty / "/" / the root's own name resolves to
  // the root. Returns the root itself if not found via a deeper match. Also
  // accepts `obj://<uuid>` for stable addressing across renames.
  Project::Object *findObjectByPath(Project::Object *root, const std::string &path)
  {
    if (!root) return nullptr;
    uint64_t uuid = 0;
    if (tryParseObjUUID(path, uuid)) {
      return findObjectByUUIDRecursive(root, uuid);
    }
    std::string p = path;
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    while (!p.empty() && p.back()  == '/') p.pop_back();
    if (p.empty() || p == root->name) return root;
    // Tolerate either "Foo/Bar" or "Root/Foo/Bar" (consume root's name if leading).
    if (p.size() > root->name.size() + 1 &&
        p.compare(0, root->name.size(), root->name) == 0 &&
        p[root->name.size()] == '/') {
      p.erase(0, root->name.size() + 1);
    }
    Project::Object *cur = root;
    while (!p.empty()) {
      auto slash = p.find('/');
      std::string seg = (slash == std::string::npos) ? p : p.substr(0, slash);
      p = (slash == std::string::npos) ? "" : p.substr(slash + 1);
      Project::Object *next = nullptr;
      for (auto &c : cur->children) {
        if (c->name == seg) { next = c.get(); break; }
      }
      if (!next) return nullptr;
      cur = next;
    }
    return cur;
  }

  // Same walk but also returns the parent and the child index within parent->children.
  // For the root itself, parent is nullptr.
  Project::Object *findObjectByPathWithParent(
    Project::Object *root,
    const std::string &path,
    Project::Object **outParent,
    size_t *outIndex
  ) {
    *outParent = nullptr;
    *outIndex = 0;
    if (!root) return nullptr;
    uint64_t uuid = 0;
    if (tryParseObjUUID(path, uuid)) {
      return findObjectByUUIDRecursiveWithParent(root, uuid, outParent, outIndex);
    }
    std::string p = path;
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    while (!p.empty() && p.back()  == '/') p.pop_back();
    if (p.empty() || p == root->name) return root;
    if (p.size() > root->name.size() + 1 &&
        p.compare(0, root->name.size(), root->name) == 0 &&
        p[root->name.size()] == '/') {
      p.erase(0, root->name.size() + 1);
    }
    Project::Object *cur = root;
    while (!p.empty()) {
      auto slash = p.find('/');
      std::string seg = (slash == std::string::npos) ? p : p.substr(0, slash);
      p = (slash == std::string::npos) ? "" : p.substr(slash + 1);
      size_t found = (size_t)-1;
      for (size_t i = 0; i < cur->children.size(); ++i) {
        if (cur->children[i]->name == seg) { found = i; break; }
      }
      if (found == (size_t)-1) return nullptr;
      *outParent = cur;
      *outIndex = found;
      cur = cur->children[found].get();
    }
    return cur;
  }

  Project::Component::Entry *findComponent(Project::Object &obj, const std::string &key)
  {
    if (auto u = tryParseUUID(key)) {
      // try comp uuid first
      for (auto &e : obj.components) if (e.uuid == *u) return &e;
      // fall through to id-based
      int v = (int)*u;
      if (v >= 0 && v < (int)Project::Component::TABLE.size()) {
        for (auto &e : obj.components) if (e.id == v) return &e;
      }
    }
    int id = findCompId(key);
    if (id < 0) return nullptr;
    for (auto &e : obj.components) if (e.id == id) return &e;
    return nullptr;
  }

  // Parse a --value string as JSON, falling back to a raw string literal so
  // `--value foo` still works for string fields.
  nlohmann::json parseValueJSON(const std::string &raw)
  {
    auto j = nlohmann::json::parse(raw, nullptr, false);
    if (j.is_discarded()) return nlohmann::json(raw);
    return j;
  }

  nlohmann::json serializeAssetEntry(const Project::AssetManagerEntry &e, bool detailed)
  {
    nlohmann::json j;
    j["uuid"] = e.conf.uuid;
    j["name"] = e.name;
    j["type"] = fileTypeName(e.type);
    j["path"] = e.path;
    if (!e.outPath.empty()) j["outPath"] = e.outPath;
    if (!e.romPath.empty()) j["romPath"] = e.romPath;
    if (detailed) {
      auto confStr = e.conf.serialize();
      j["conf"] = nlohmann::json::parse(confStr, nullptr, false);
      switch (e.type) {
        case Project::FileType::PREFAB:
          if (e.prefab) {
            j["prefab"] = nlohmann::json::parse(e.prefab->serialize(), nullptr, false);
          }
          break;
        case Project::FileType::RESOURCE_INSTANCE:
          if (e.resource) {
            j["resource"] = nlohmann::json::parse(e.resource->serialize(), nullptr, false);
          }
          break;
        case Project::FileType::RESOURCE_TYPE:
          j["fields"] = nlohmann::json::array();
          for (const auto &f : e.params.fields) {
            nlohmann::json field;
            field["name"] = f.name;
            field["dataType"] = (int)f.type;
            field["dataSize"] = f.dataSize;
            if (!f.defaultValue.empty()) field["default"] = f.defaultValue;
            j["fields"].push_back(field);
          }
          break;
        default: break;
      }
    }
    return j;
  }

  // Persist a prefab back to its on-disk location (NOT Prefab::save, which
  // hardcodes <project>/assets/<name>.prefab and would silently move files
  // out of subdirectories). For variants, recomputes structured overrides
  // from the resolved obj tree before serializing.
  void savePrefabAt(const std::string &absPath, Project::Prefab &prefab)
  {
    if (prefab.isVariant()) {
      auto parent = ctx.project
        ? ctx.project->getAssets().getPrefabByUUID(prefab.uuidParentPrefab.value)
        : std::shared_ptr<Project::Prefab>{};
      if (parent) prefab.rebuildOverridesFromCurrent(*parent);
    }
    Utils::FS::saveTextFile(absPath, prefab.serialize());
  }

  // ── Scene helpers ──────────────────────────────────────────────────────

  // Resolve a scene by id (decimal string or json int) or by exact name.
  // Returns -1 on failure. Accepts the same spelling tolerance as the asset
  // resolver so CLI consumers can use either form.
  int resolveSceneId(Project::Project &project, const std::string &key)
  {
    if (key.empty()) return -1;
    if (auto u = tryParseUUID(key)) {
      int candidate = (int)*u;
      for (const auto &e : project.getScenes().getEntries()) {
        if (e.id == candidate) return candidate;
      }
    }
    for (const auto &e : project.getScenes().getEntries()) {
      if (e.name == key) return e.id;
    }
    return -1;
  }

  // Build, mutate, save lifecycle. We route through SceneManager::loadScene
  // (rather than just constructing a free Scene) because some component
  // init paths reach into ctx.project->getScenes().getLoadedScene() — Camera
  // pulls fbWidth/fbHeight from it, AnimModel and Model read renderPipeline
  // and layers3D, etc. In CLI mode there is no GUI loader to populate that,
  // so we set it explicitly. UndoRedo and mainSelection clears triggered by
  // loadScene are pure data clears and harmless when no GUI is running.
  Project::Scene *openScene(Project::Project &project, int id)
  {
    project.getScenes().loadScene(id);
    return project.getScenes().getLoadedScene();
  }

  void saveScene(Project::Project &project, Project::Scene & /*unused*/)
  {
    project.getScenes().save();
    project.getScenes().reload();
  }

  nlohmann::json sceneEntryJSON(const Project::SceneEntry &e)
  {
    nlohmann::json j;
    j["id"] = e.id;
    j["name"] = e.name;
    if (!e.relPath.empty()) j["relPath"] = e.relPath;
    return j;
  }

  // ──────────────────────────────────────────────────────────────────────
  //  Command implementations
  // ──────────────────────────────────────────────────────────────────────

  int cmdAssetList(const CLI::Commands::Args &a, Project::Project &project)
  {
    Project::FileType filter = Project::FileType::UNKNOWN;
    if (!a.type.empty()) {
      filter = parseFileTypeName(a.type);
      if (filter == Project::FileType::UNKNOWN && a.type != "unknown") {
        emitErr("unknown asset type '" + a.type + "'"); return 1;
      }
    }
    nlohmann::json out = nlohmann::json::array();
    auto &am = project.getAssets();
    for (int i = 0; i < (int)Project::FileType::_SIZE; ++i) {
      auto t = (Project::FileType)i;
      if (filter != Project::FileType::UNKNOWN && t != filter) continue;
      for (const auto &e : am.getTypeEntries(t)) {
        out.push_back(serializeAssetEntry(e, false));
      }
    }
    emitJSON(out);
    return 0;
  }

  int cmdAssetDescribe(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    emitJSON(serializeAssetEntry(*e, true));
    return 0;
  }

  int cmdAssetDelete(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    std::string assetPath = e->path;
    std::error_code ec;
    fs::remove(assetPath, ec);
    fs::remove(assetPath + ".conf", ec);
    project.getAssets().reload();
    nlohmann::json out;
    out["deleted"] = assetPath;
    emitJSON(out);
    return 0;
  }

  int cmdAssetRename(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty()) {
      emitErr("--asset and --name are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    fs::path oldP{e->path};
    fs::path newP = oldP.parent_path() / (a.name + oldP.extension().string());
    if (fs::exists(newP)) { emitErr("destination exists: " + newP.string()); return 1; }
    std::error_code ec;
    fs::rename(oldP, newP, ec);
    if (ec) { emitErr("rename failed: " + ec.message()); return 1; }
    if (fs::exists(oldP.string() + ".conf")) {
      fs::rename(oldP.string() + ".conf", newP.string() + ".conf", ec);
    }
    project.getAssets().reload();
    auto *fresh = project.getAssets().getByPath(newP.string());
    nlohmann::json out;
    out["renamed"] = newP.string();
    if (fresh) out["asset"] = serializeAssetEntry(*fresh, false);
    emitJSON(out);
    return 0;
  }

  int cmdAssetImport(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.file.empty()) { emitErr("--file is required"); return 1; }
    fs::path src{a.file};
    if (!fs::exists(src)) { emitErr("source file not found: " + src.string()); return 1; }
    fs::path destDir = fs::path(project.getPath()) / "assets";
    if (!a.dest.empty()) {
      fs::path d{a.dest};
      destDir = d.is_absolute() ? d : (fs::path(project.getPath()) / d);
    }
    fs::create_directories(destDir);
    fs::path dest = destDir / src.filename();
    if (fs::exists(dest)) { emitErr("destination exists: " + dest.string()); return 1; }
    std::error_code ec;
    fs::copy_file(src, dest, ec);
    if (ec) { emitErr("copy failed: " + ec.message()); return 1; }
    project.getAssets().reload();
    auto *e = project.getAssets().getByPath(dest.string());
    nlohmann::json out;
    out["imported"] = dest.string();
    if (e) out["asset"] = serializeAssetEntry(*e, false);
    emitJSON(out);
    return 0;
  }

  int cmdAssetSetConf(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field, --value are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    // Round-trip the conf JSON: serialize → patch field → write to .conf.
    auto confStr = e->conf.serialize();
    auto j = nlohmann::json::parse(confStr, nullptr, false);
    if (!j.is_object()) { emitErr("conf is not a JSON object (corrupt asset?)"); return 1; }
    j[a.field] = parseValueJSON(a.value);
    Utils::FS::saveTextFile(e->path + ".conf", j.dump(2));
    project.getAssets().reload();
    auto *fresh = resolveAsset(project, std::to_string(e->conf.uuid));
    nlohmann::json out;
    out["updated"] = a.field;
    if (fresh) out["asset"] = serializeAssetEntry(*fresh, true);
    emitJSON(out);
    return 0;
  }

  // ── Component introspection ─────────────────────────────────────────

  int cmdComponentList(const CLI::Commands::Args &/*a*/, Project::Project &/*project*/)
  {
    nlohmann::json out = nlohmann::json::array();
    for (const auto &info : Project::Component::TABLE) {
      nlohmann::json j;
      j["id"] = info.id;
      j["name"] = info.name;
      if (info.icon) j["icon"] = info.icon;
      out.push_back(j);
    }
    emitJSON(out);
    return 0;
  }

  int cmdComponentDescribe(const CLI::Commands::Args &a, Project::Project &/*project*/)
  {
    if (a.comp.empty()) { emitErr("--comp is required"); return 1; }
    int id = findCompId(a.comp);
    if (id < 0) { emitErr("unknown component: " + a.comp); return 1; }
    const auto &info = Project::Component::TABLE[id];
    nlohmann::json out;
    out["id"] = info.id;
    out["name"] = info.name;
    // Schema by round-trip: init a fresh component on a throwaway Object,
    // then serialize. Keys + value-types in the resulting JSON describe the
    // component's persistable surface uniformly across all comp types.
    Project::Object dummy{};
    Project::Component::Entry entry{
      .id = info.id, .uuid = 0, .name = info.name, .data = info.funcInit(dummy)
    };
    out["schema"] = info.funcSerialize(entry);
    emitJSON(out);
    return 0;
  }

  // ── Prefab lifecycle ────────────────────────────────────────────────

  int cmdPrefabCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty()) { emitErr("--name is required"); return 1; }
    fs::path destDir = fs::path(project.getPath()) / "assets";
    if (!a.dir.empty()) {
      fs::path d{a.dir};
      destDir = d.is_absolute() ? d : (fs::path(project.getPath()) / d);
    }
    fs::create_directories(destDir);
    fs::path outPath = destDir / (a.name + ".prefab");
    if (fs::exists(outPath)) { emitErr("destination exists: " + outPath.string()); return 1; }

    Project::Prefab prefab{};
    prefab.uuid.value = Utils::Hash::randomU64();
    prefab.obj.name = a.name;
    prefab.obj.uuid = (uint32_t)Utils::Hash::randomU64();
    prefab.obj.scale.value = {1.0f, 1.0f, 1.0f};
    prefab.obj.rot.value = {0, 0, 0, 1};
    // Scaffold default lifecycle: PrefabEvent + matching PrefabFunc nodes
    // pre-wired, plus the OnReady/OnEnable/OnDisable P64_NODE stubs in
    // src/user/<Prefab>.{h,cpp}. --no-scaffold opts out for callers that
    // want a bare prefab.
    if (!a.noScaffold) {
      Project::PrefabScaffolder::seedDefaultsForNewPrefab(
        project.getPath(), prefab, outPath.filename().string());
    } else {
      Project::ensurePrefabUserSource(project.getPath(), outPath.filename().string());
    }
    Utils::FS::saveTextFile(outPath.string(), prefab.serialize());
    project.getAssets().reload();
    auto *e = project.getAssets().getByPath(outPath.string());
    nlohmann::json out;
    out["created"] = outPath.string();
    out["scaffolded"] = !a.noScaffold;
    if (e) out["asset"] = serializeAssetEntry(*e, true);
    emitJSON(out);
    return 0;
  }

  int cmdPrefabDuplicate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty()) {
      emitErr("--asset and --name are required"); return 1;
    }
    auto *src = resolvePrefabOrWidget(project, a.asset);
    if (!src || !src->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    fs::path srcP{src->path};
    fs::path destDir = a.dir.empty() ? srcP.parent_path()
                                     : (fs::path(a.dir).is_absolute() ? fs::path(a.dir)
                                        : (fs::path(project.getPath()) / a.dir));
    fs::create_directories(destDir);
    fs::path outPath = destDir / (a.name + ".prefab");
    if (fs::exists(outPath)) { emitErr("destination exists: " + outPath.string()); return 1; }

    Project::Prefab dup = *src->prefab;
    dup.uuid.value = Utils::Hash::randomU64();
    dup.obj.name = a.name;
    dup.obj.uuid = (uint32_t)Utils::Hash::randomU64();
    Utils::FS::saveTextFile(outPath.string(), dup.serialize());
    Project::ensurePrefabUserSource(project.getPath(), outPath.filename().string());
    project.getAssets().reload();
    auto *e = project.getAssets().getByPath(outPath.string());
    nlohmann::json out;
    out["created"] = outPath.string();
    if (e) out["asset"] = serializeAssetEntry(*e, true);
    emitJSON(out);
    return 0;
  }

  int cmdPrefabVariant(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.parent.empty() || a.name.empty()) {
      emitErr("--parent and --name are required"); return 1;
    }
    auto *parent = resolvePrefabOrWidget(project, a.parent);
    if (!parent || !parent->prefab) { emitErr("parent prefab not found: " + a.parent); return 1; }

    fs::path destDir = a.dir.empty() ? fs::path(parent->path).parent_path()
                                     : (fs::path(a.dir).is_absolute() ? fs::path(a.dir)
                                        : (fs::path(project.getPath()) / a.dir));
    fs::create_directories(destDir);
    fs::path outPath = destDir / (a.name + ".prefab");
    if (fs::exists(outPath)) { emitErr("destination exists: " + outPath.string()); return 1; }

    Project::Prefab variant{};
    variant.uuid.value = Utils::Hash::randomU64();
    variant.uuidParentPrefab.value = parent->prefab->uuid.value;
    variant.obj = Project::Object{};
    variant.obj.name = a.name;
    Utils::FS::saveTextFile(outPath.string(), variant.serialize());
    project.getAssets().reload();
    auto *e = project.getAssets().getByPath(outPath.string());
    nlohmann::json out;
    out["created"] = outPath.string();
    if (e) out["asset"] = serializeAssetEntry(*e, true);
    emitJSON(out);
    return 0;
  }

  int cmdPrefabDescribe(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    nlohmann::json out;
    out["uuid"] = e->prefab->uuid.value;
    out["name"] = e->name;
    out["path"] = e->path;
    out["isVariant"] = e->prefab->isVariant();
    if (e->prefab->isVariant()) out["uuidParentPrefab"] = e->prefab->uuidParentPrefab.value;
    out["obj"] = e->prefab->obj.serialize();
    if (!e->prefab->variables.empty()) {
      out["variables"] = nlohmann::json::array();
      for (const auto &v : e->prefab->variables) {
        nlohmann::json vj;
        vj["uuid"] = v.uuid; vj["name"] = v.name;
        vj["kind"] = (int)v.kind; vj["typeArg"] = v.typeArg;
        out["variables"].push_back(vj);
      }
    }
    emitJSON(out);
    return 0;
  }

  // ── Prefab tree editing ─────────────────────────────────────────────

  int cmdPrefabAddObject(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty()) {
      emitErr("--asset and --name are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    auto *parent = findObjectByPath(&e->prefab->obj, a.parent);
    if (!parent) { emitErr("parent path not found: " + a.parent); return 1; }
    auto child = std::make_shared<Project::Object>(*parent);
    child->name = a.name;
    child->uuid = (uint32_t)Utils::Hash::randomU64();
    child->scale.value = {1.0f, 1.0f, 1.0f};
    child->rot.value = {0, 0, 0, 1};
    parent->children.push_back(child);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    auto *fresh = resolvePrefabOrWidget(project, a.asset);
    nlohmann::json out;
    out["addedObject"] = a.name;
    out["uuid"] = child->uuid;
    if (fresh && fresh->prefab) out["obj"] = fresh->prefab->obj.serialize();
    emitJSON(out);
    return 0;
  }

  int cmdPrefabRemoveObject(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.path.empty()) {
      emitErr("--asset and --path are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *parent = nullptr;
    size_t idx = 0;
    auto *target = findObjectByPathWithParent(&e->prefab->obj, a.path, &parent, &idx);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    if (!parent) { emitErr("cannot remove root object"); return 1; }
    parent->children.erase(parent->children.begin() + idx);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["removedPath"] = a.path;
    auto *fresh = resolvePrefabOrWidget(project, a.asset);
    if (fresh && fresh->prefab) out["obj"] = fresh->prefab->obj.serialize();
    emitJSON(out);
    return 0;
  }

  int cmdPrefabSetTransform(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field, --value are required (--field one of pos|rot|scale|name|enabled)"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    auto *target = findObjectByPath(&e->prefab->obj, a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }

    auto v = parseValueJSON(a.value);
    bool ok = true;
    if (a.field == "pos" || a.field == "scale") {
      if (!v.is_array() || v.size() != 3) { ok = false; }
      else {
        glm::vec3 vec{ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
        if (a.field == "pos") target->pos.value = vec; else target->scale.value = vec;
      }
    } else if (a.field == "rot") {
      if (!v.is_array() || v.size() != 4) ok = false;
      else target->rot.value = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>()};
    } else if (a.field == "name") {
      if (!v.is_string()) ok = false; else target->name = v.get<std::string>();
    } else if (a.field == "enabled") {
      if (!v.is_boolean()) ok = false; else target->enabled = v.get<bool>();
    } else if (a.field == "selectable") {
      if (!v.is_boolean()) ok = false; else target->selectable = v.get<bool>();
    } else if (a.field == "isCanvas2D") {
      if (!v.is_boolean()) ok = false; else target->isCanvas2D = v.get<bool>();
    } else if (a.field == "anchor2D") {
      if (!v.is_number_integer()) ok = false; else target->anchor2D = (uint8_t)v.get<int>();
    } else if (a.field == "layerIndex2D") {
      if (!v.is_number_integer()) ok = false; else target->layerIndex2D = (uint8_t)v.get<int>();
    } else {
      emitErr("unknown field '" + a.field + "' (allowed: pos, rot, scale, name, enabled, selectable, isCanvas2D, anchor2D, layerIndex2D)");
      return 1;
    }
    if (!ok) { emitErr("--value did not match expected type for field '" + a.field + "'"); return 1; }

    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["updated"] = a.field;
    auto *fresh = resolvePrefabOrWidget(project, a.asset);
    if (fresh && fresh->prefab) {
      auto *t = findObjectByPath(&fresh->prefab->obj, a.path);
      if (t) out["obj"] = t->serialize();
    }
    emitJSON(out);
    return 0;
  }

  int cmdPrefabAddComponent(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty()) {
      emitErr("--asset and --comp are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    auto *target = findObjectByPath(&e->prefab->obj, a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    int compId = findCompId(a.comp);
    if (compId < 0) { emitErr("unknown component: " + a.comp); return 1; }
    target->addComponent(compId);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["added"] = Project::Component::TABLE[compId].name;
    auto *fresh = resolvePrefabOrWidget(project, a.asset);
    if (fresh && fresh->prefab) {
      auto *t = findObjectByPath(&fresh->prefab->obj, a.path);
      if (t) out["obj"] = t->serialize();
    }
    emitJSON(out);
    return 0;
  }

  int cmdPrefabRemoveComponent(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty()) {
      emitErr("--asset and --comp are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    auto *target = findObjectByPath(&e->prefab->obj, a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    auto *entry = findComponent(*target, a.comp);
    if (!entry) { emitErr("component not present: " + a.comp); return 1; }
    target->removeComponent(entry->uuid);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["removed"] = a.comp;
    auto *fresh = resolvePrefabOrWidget(project, a.asset);
    if (fresh && fresh->prefab) {
      auto *t = findObjectByPath(&fresh->prefab->obj, a.path);
      if (t) out["obj"] = t->serialize();
    }
    emitJSON(out);
    return 0;
  }

  int cmdPrefabSetProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --comp, --field, --value are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    auto *target = findObjectByPath(&e->prefab->obj, a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    auto *entry = findComponent(*target, a.comp);
    if (!entry) { emitErr("component not present: " + a.comp); return 1; }
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    if (!data.is_object()) { emitErr("component data is not a JSON object"); return 1; }
    if (!data.contains(a.field)) {
      emitErr("component '" + std::string(info.name) + "' has no field '" + a.field + "'");
      return 1;
    }
    data[a.field] = parseValueJSON(a.value);
    entry->data = info.funcDeserialize(data);

    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["updated"] = a.field;
    auto *fresh = resolvePrefabOrWidget(project, a.asset);
    if (fresh && fresh->prefab) {
      auto *t = findObjectByPath(&fresh->prefab->obj, a.path);
      if (t) out["obj"] = t->serialize();
    }
    emitJSON(out);
    return 0;
  }

  // ── Code (per-prefab + global script files) ─────────────────────────

  int cmdCodeAddFunction(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.func.empty()) {
      emitErr("--asset and --func are required"); return 1;
    }
    // For prefabs we use the prefab's filename (with .prefab) as the source
    // pair stem, mirroring how the editor's prefab Code panel addresses files.
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    std::string stemArg;
    if (e->type == Project::FileType::PREFAB) {
      stemArg = e->name; // e.g. "Foo.prefab"
    } else if (e->type == Project::FileType::CODE_OBJ ||
               e->type == Project::FileType::CODE_GLOBAL) {
      stemArg = fs::path(e->name).stem().string();
    } else {
      emitErr("--asset must be a prefab or code asset"); return 1;
    }
    bool ok = Project::addPrefabFunction(project.getPath(), stemArg, a.func);
    if (!ok) { emitErr("addPrefabFunction failed"); return 1; }
    nlohmann::json out;
    out["added"] = a.func;
    out["asset"] = e->name;
    emitJSON(out);
    return 0;
  }

  int cmdCodeRenameFunction(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from, --to are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    std::string stemArg = (e->type == Project::FileType::PREFAB)
      ? e->name : fs::path(e->name).stem().string();
    bool ok = Project::renamePrefabFunction(project.getPath(), stemArg, a.from, a.to);
    if (!ok) { emitErr("renamePrefabFunction failed"); return 1; }
    nlohmann::json out;
    out["renamed"] = {{"from", a.from}, {"to", a.to}};
    emitJSON(out);
    return 0;
  }

  int cmdCodeRemoveFunction(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.func.empty()) {
      emitErr("--asset and --func are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    std::string stemArg = (e->type == Project::FileType::PREFAB)
      ? e->name : fs::path(e->name).stem().string();
    bool ok = Project::removePrefabFunction(project.getPath(), stemArg, a.func);
    if (!ok) { emitErr("removePrefabFunction failed"); return 1; }
    nlohmann::json out;
    out["removed"] = a.func;
    emitJSON(out);
    return 0;
  }

  int cmdCodeListFunctions(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    std::string stemArg = (e->type == Project::FileType::PREFAB)
      ? e->name : fs::path(e->name).stem().string();
    auto fns = Project::scanPrefabFunctions(project.getPath(), stemArg);
    nlohmann::json out = nlohmann::json::array();
    for (const auto &f : fns) {
      nlohmann::json j;
      j["name"] = f.name;
      j["returnType"] = f.returnType;
      j["params"] = f.params;
      j["line"] = f.line;
      out.push_back(j);
    }
    emitJSON(out);
    return 0;
  }

  // ── Prefab scaffolding (Tier 1 + Tier 2 backfill) ──────────────────

  // Re-run the Tier 1 scaffold against an existing prefab. Useful for
  // prefabs that pre-date the auto-scaffold or that had their event graph
  // wiped. Idempotent: only adds what's missing on each side.
  int cmdPrefabScaffoldDefaults(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    auto added = Project::PrefabScaffolder::seedLifecycleFunctions(
      project.getPath(), e->name);
    bool graphChanged = Project::PrefabScaffolder::seedDefaultEventGraph(*e->prefab);
    if (graphChanged) savePrefabAt(e->path, *e->prefab);
    nlohmann::json out;
    out["asset"]        = e->name;
    out["functions"]    = added;
    out["graphSeeded"]  = graphChanged;
    emitJSON(out);
    return 0;
  }

  // Tier 2: report PrefabFunc nodes referencing missing P64_NODE stubs and
  // PrefabVarGet nodes referencing missing variables. With --autofix, append
  // the stubs / variable defs needed to satisfy them.
  int cmdPrefabGraphValidate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }

    auto fnRefs  = Project::PrefabScaffolder::findUnknownFuncRefs(
      project.getPath(), e->name, *e->prefab);
    auto varRefs = Project::PrefabScaffolder::findUnknownVarRefs(*e->prefab);

    nlohmann::json out;
    out["asset"] = e->name;
    {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto &r : fnRefs) {
        arr.push_back({{"funcName", r.funcName}, {"nodeUUID", r.nodeUUID}});
      }
      out["unknownFunctions"] = arr;
    }
    {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto &r : varRefs) {
        arr.push_back({
          {"varName", r.varName}, {"varKind", r.varKind},
          {"varUUID", r.varUUID}, {"nodeUUID", r.nodeUUID},
        });
      }
      out["unknownVariables"] = arr;
    }

    if (a.autofix) {
      auto fixedFns = Project::PrefabScaffolder::autofixFunctions(
        project.getPath(), e->name, fnRefs);
      auto fixedVars = Project::PrefabScaffolder::autofixVariables(
        *e->prefab, varRefs);
      if (!fixedVars.empty()) savePrefabAt(e->path, *e->prefab);
      out["addedFunctions"] = fixedFns;
      out["addedVariables"] = fixedVars;
      project.getAssets().reload();
    }
    emitJSON(out);
    return 0;
  }

  int cmdScriptCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty()) { emitErr("--name is required"); return 1; }
    bool isGlobal = (a.type == "codeGlobal" || a.type == "global");
    bool ok = project.getAssets().createScript(a.name, isGlobal, a.dir);
    if (!ok) { emitErr("createScript failed (name conflict or invalid)"); return 1; }
    nlohmann::json out;
    out["created"] = a.name + ".cpp";
    out["isGlobal"] = isGlobal;
    emitJSON(out);
    return 0;
  }

  // ── Resources ───────────────────────────────────────────────────────

  int cmdResourceCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty() || a.restype.empty()) {
      emitErr("--name and --restype are required"); return 1;
    }
    auto *typeEntry = resolveAsset(project, a.restype, Project::FileType::RESOURCE_TYPE);
    if (!typeEntry) { emitErr("resource type not found: " + a.restype); return 1; }
    uint64_t newUUID = project.getAssets().createResourceInstance(
      a.name, typeEntry->conf.uuid, a.dir);
    if (newUUID == 0) { emitErr("createResourceInstance failed"); return 1; }
    auto *e = project.getAssets().getEntryByUUID(newUUID);
    nlohmann::json out;
    out["created"] = a.name + ".p64res";
    if (e) out["asset"] = serializeAssetEntry(*e, true);
    emitJSON(out);
    return 0;
  }

  int cmdResourceSetProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field, --value are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset, Project::FileType::RESOURCE_INSTANCE);
    if (!e || !e->resource) { emitErr("resource instance not found: " + a.asset); return 1; }
    // Resource::Instance::values is a string→string map; we accept any JSON
    // and store its text form (matches BinaryFile::writeAs's parser).
    auto v = parseValueJSON(a.value);
    std::string stored = v.is_string() ? v.get<std::string>() : v.dump();
    e->resource->values[a.field] = stored;
    Utils::FS::saveTextFile(e->path, e->resource->serialize());
    project.getAssets().reload();
    auto *fresh = project.getAssets().getEntryByUUID(e->conf.uuid);
    nlohmann::json out;
    out["updated"] = a.field;
    if (fresh) out["asset"] = serializeAssetEntry(*fresh, true);
    emitJSON(out);
    return 0;
  }

  // ── Authored-asset shortcuts (graph, material) ─────────────────────

  int cmdGraphCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty()) { emitErr("--name is required"); return 1; }
    uint64_t uuid = project.getAssets().createNodeGraph(a.name);
    if (uuid == 0) { emitErr("createNodeGraph failed (name conflict?)"); return 1; }
    auto *e = project.getAssets().getEntryByUUID(uuid);
    nlohmann::json out;
    out["created"] = a.name + ".p64graph";
    if (e) out["asset"] = serializeAssetEntry(*e, false);
    emitJSON(out);
    return 0;
  }

  int cmdMaterialCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty()) { emitErr("--name is required"); return 1; }
    uint64_t uuid = project.getAssets().createMaterial(a.name);
    if (uuid == 0) { emitErr("createMaterial failed (name conflict?)"); return 1; }
    auto *e = project.getAssets().getEntryByUUID(uuid);
    nlohmann::json out;
    out["created"] = a.name + ".p64mat";
    if (e) out["asset"] = serializeAssetEntry(*e, false);
    emitJSON(out);
    return 0;
  }

  int cmdWidgetCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty()) { emitErr("--name is required"); return 1; }
    uint64_t uuid = project.getAssets().createWidgetBlueprint(a.name);
    if (uuid == 0) { emitErr("createWidgetBlueprint failed (name conflict?)"); return 1; }
    auto *e = project.getAssets().getEntryByUUID(uuid);
    nlohmann::json out;
    out["created"] = a.name + ".p64widget";
    if (e) out["asset"] = serializeAssetEntry(*e, false);
    emitJSON(out);
    return 0;
  }

  int cmdParticleSystemCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty()) { emitErr("--name is required"); return 1; }
    uint64_t uuid = project.getAssets().createParticleSystem(a.name, a.dir);
    if (uuid == 0) { emitErr("createParticleSystem failed (name conflict?)"); return 1; }
    auto *e = project.getAssets().getEntryByUUID(uuid);
    nlohmann::json out;
    out["created"] = a.name + ".p64ptx";
    if (e) out["asset"] = serializeAssetEntry(*e, false);
    emitJSON(out);
    return 0;
  }

  // ── Save file (.p64save) ────────────────────────────────────────────

  // Locally-scoped helpers so the same string<->enum mapping is used by
  // every save-file-* command without leaking into the shared header.
  const char *saveFieldTypeName(::Project::Assets::SaveFileAsset::FieldType t)
  {
    using FT = ::Project::Assets::SaveFileAsset::FieldType;
    switch (t) {
      case FT::FT_INT:    return "Int";
      case FT::FT_FLOAT:  return "Float";
      case FT::FT_BOOL:   return "Bool";
      case FT::FT_STRING: return "String";
      case FT::FT_VEC2:   return "Vec2";
      case FT::FT_VEC3:   return "Vec3";
    }
    return "?";
  }
  bool parseSaveFieldType(const std::string &raw, ::Project::Assets::SaveFileAsset::FieldType &out)
  {
    using FT = ::Project::Assets::SaveFileAsset::FieldType;
    auto eq = [&](const char *a) {
      if (raw.size() != strlen(a)) return false;
      for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i] >= 'A' && raw[i] <= 'Z' ? (char)(raw[i] - 'A' + 'a') : raw[i];
        char d = a[i]   >= 'A' && a[i]   <= 'Z' ? (char)(a[i]   - 'A' + 'a') : a[i];
        if (c != d) return false;
      }
      return true;
    };
    if (eq("int"))    { out = FT::FT_INT;    return true; }
    if (eq("float"))  { out = FT::FT_FLOAT;  return true; }
    if (eq("bool"))   { out = FT::FT_BOOL;   return true; }
    if (eq("string")) { out = FT::FT_STRING; return true; }
    if (eq("vec2"))   { out = FT::FT_VEC2;   return true; }
    if (eq("vec3"))   { out = FT::FT_VEC3;   return true; }
    return false;
  }
  nlohmann::json serializeSaveAsset(const Project::AssetManagerEntry &e)
  {
    nlohmann::json out;
    out["uuid"] = e.conf.uuid;
    out["name"] = e.name;
    out["path"] = e.path;
    if (!e.saveFileAsset) { out["fields"] = nlohmann::json::array(); return out; }
    out["groupName"] = e.saveFileAsset->groupName;
    nlohmann::json arr = nlohmann::json::array();
    uint32_t slot = 0;
    for (const auto &f : e.saveFileAsset->fields) {
      nlohmann::json fj;
      fj["name"]  = f.name;
      fj["type"]  = saveFieldTypeName(f.type);
      fj["slot"]  = slot;
      fj["slots"] = ::Project::Assets::SaveFileAsset::fieldSlotCount(f);
      switch (f.type) {
        case ::Project::Assets::SaveFileAsset::FT_INT:    fj["default"] = f.defInt; break;
        case ::Project::Assets::SaveFileAsset::FT_FLOAT:  fj["default"] = f.defFloat; break;
        case ::Project::Assets::SaveFileAsset::FT_BOOL:   fj["default"] = f.defBool; break;
        case ::Project::Assets::SaveFileAsset::FT_STRING:
          fj["default"]   = f.defString;
          fj["stringLen"] = f.stringLen;
          break;
        case ::Project::Assets::SaveFileAsset::FT_VEC2:
          fj["default"] = nlohmann::json::array({f.defVec[0], f.defVec[1]});
          break;
        case ::Project::Assets::SaveFileAsset::FT_VEC3:
          fj["default"] = nlohmann::json::array({f.defVec[0], f.defVec[1], f.defVec[2]});
          break;
      }
      arr.push_back(fj);
      slot += ::Project::Assets::SaveFileAsset::fieldSlotCount(f);
    }
    out["fields"]    = arr;
    out["slotsUsed"] = slot;
    return out;
  }
  void persistSaveAsset(Project::AssetManagerEntry &e)
  {
    if (!e.saveFileAsset) return;
    Utils::FS::saveTextFile(e.path, e.saveFileAsset->serialize());
  }

  int cmdSaveFileCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty()) { emitErr("--name is required"); return 1; }
    uint64_t uuid = project.getAssets().createSaveFile(a.name, a.dir);
    if (uuid == 0) { emitErr("createSaveFile failed (name conflict?)"); return 1; }
    auto *e = project.getAssets().getEntryByUUID(uuid);
    nlohmann::json out;
    out["created"] = a.name + ".p64save";
    if (e) out["asset"] = serializeSaveAsset(*e);
    emitJSON(out);
    return 0;
  }

  int cmdSaveFileList(const CLI::Commands::Args &/*a*/, Project::Project &project)
  {
    nlohmann::json out = nlohmann::json::array();
    for (const auto &e : project.getAssets().getTypeEntries(Project::FileType::SAVE_FILE)) {
      out.push_back(serializeSaveAsset(e));
    }
    emitJSON(out);
    return 0;
  }

  int cmdSaveFileDescribe(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolveAsset(project, a.asset, Project::FileType::SAVE_FILE);
    if (!e) { emitErr("save asset not found: " + a.asset); return 1; }
    emitJSON(serializeSaveAsset(*e));
    return 0;
  }

  int cmdSaveFileAddField(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    if (a.field.empty()) { emitErr("--field is required"); return 1; }
    auto *e = resolveAsset(project, a.asset, Project::FileType::SAVE_FILE);
    if (!e || !e->saveFileAsset) { emitErr("save asset not found: " + a.asset); return 1; }

    ::Project::Assets::SaveFileAsset::FieldType ft = ::Project::Assets::SaveFileAsset::FT_INT;
    if (!a.type.empty() && !parseSaveFieldType(a.type, ft)) {
      emitErr("--type must be Int|Float|Bool|String|Vec2|Vec3"); return 1;
    }

    for (const auto &f : e->saveFileAsset->fields) {
      if (f.name == a.field) {
        emitErr("field already exists: " + a.field); return 1;
      }
    }

    ::Project::Assets::SaveFileAsset::Field f{};
    f.name = a.field;
    f.type = ft;
    if (!a.value.empty()) {
      auto j = parseValueJSON(a.value);
      switch (ft) {
        case ::Project::Assets::SaveFileAsset::FT_INT:
          if (j.is_number()) f.defInt = j.get<int32_t>();
          break;
        case ::Project::Assets::SaveFileAsset::FT_FLOAT:
          if (j.is_number()) f.defFloat = j.get<float>();
          break;
        case ::Project::Assets::SaveFileAsset::FT_BOOL:
          if (j.is_boolean()) f.defBool = j.get<bool>();
          else if (j.is_number()) f.defBool = j.get<int>() != 0;
          break;
        case ::Project::Assets::SaveFileAsset::FT_STRING:
          if (j.is_string()) f.defString = j.get<std::string>();
          break;
        case ::Project::Assets::SaveFileAsset::FT_VEC2:
        case ::Project::Assets::SaveFileAsset::FT_VEC3: {
          if (j.is_array()) {
            for (size_t i = 0; i < j.size() && i < 3; ++i) {
              if (j[i].is_number()) f.defVec[i] = j[i].get<float>();
            }
          }
          break;
        }
      }
    }
    e->saveFileAsset->fields.push_back(std::move(f));
    persistSaveAsset(*e);
    emitJSON(serializeSaveAsset(*e));
    return 0;
  }

  int cmdSaveFileRemoveField(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    if (a.field.empty()) { emitErr("--field is required"); return 1; }
    auto *e = resolveAsset(project, a.asset, Project::FileType::SAVE_FILE);
    if (!e || !e->saveFileAsset) { emitErr("save asset not found: " + a.asset); return 1; }
    auto &fields = e->saveFileAsset->fields;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
      if (it->name == a.field) {
        fields.erase(it);
        persistSaveAsset(*e);
        emitJSON(serializeSaveAsset(*e));
        return 0;
      }
    }
    emitErr("field not found: " + a.field);
    return 1;
  }

  int cmdSaveFileSetField(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    if (a.field.empty()) { emitErr("--field is required"); return 1; }
    auto *e = resolveAsset(project, a.asset, Project::FileType::SAVE_FILE);
    if (!e || !e->saveFileAsset) { emitErr("save asset not found: " + a.asset); return 1; }

    ::Project::Assets::SaveFileAsset::Field *f = nullptr;
    for (auto &fld : e->saveFileAsset->fields) {
      if (fld.name == a.field) { f = &fld; break; }
    }
    if (!f) { emitErr("field not found: " + a.field); return 1; }

    if (!a.type.empty()) {
      ::Project::Assets::SaveFileAsset::FieldType ft;
      if (!parseSaveFieldType(a.type, ft)) {
        emitErr("--type must be Int|Float|Bool|String|Vec2|Vec3"); return 1;
      }
      f->type = ft;
    }
    if (!a.to.empty()) {
      // Rename (avoid collision)
      for (const auto &fld : e->saveFileAsset->fields) {
        if (&fld != f && fld.name == a.to) {
          emitErr("field name collision: " + a.to); return 1;
        }
      }
      f->name = a.to;
    }
    if (!a.value.empty()) {
      auto j = parseValueJSON(a.value);
      switch (f->type) {
        case ::Project::Assets::SaveFileAsset::FT_INT:
          if (j.is_number()) f->defInt = j.get<int32_t>();
          break;
        case ::Project::Assets::SaveFileAsset::FT_FLOAT:
          if (j.is_number()) f->defFloat = j.get<float>();
          break;
        case ::Project::Assets::SaveFileAsset::FT_BOOL:
          if (j.is_boolean()) f->defBool = j.get<bool>();
          else if (j.is_number()) f->defBool = j.get<int>() != 0;
          break;
        case ::Project::Assets::SaveFileAsset::FT_STRING:
          if (j.is_string()) f->defString = j.get<std::string>();
          break;
        case ::Project::Assets::SaveFileAsset::FT_VEC2:
        case ::Project::Assets::SaveFileAsset::FT_VEC3:
          if (j.is_array()) {
            for (size_t i = 0; i < j.size() && i < 3; ++i) {
              if (j[i].is_number()) f->defVec[i] = j[i].get<float>();
            }
          }
          break;
      }
    }
    persistSaveAsset(*e);
    emitJSON(serializeSaveAsset(*e));
    return 0;
  }

  // ── Scene lifecycle ────────────────────────────────────────────────

  int cmdSceneList(const CLI::Commands::Args &/*a*/, Project::Project &project)
  {
    nlohmann::json out = nlohmann::json::array();
    for (const auto &e : project.getScenes().getEntries()) {
      out.push_back(sceneEntryJSON(e));
    }
    emitJSON(out);
    return 0;
  }

  int cmdSceneCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty()) { emitErr("--name is required"); return 1; }
    auto &mgr = project.getScenes();
    mgr.add();
    // SceneManager::add() creates an empty directory. Pick the new scene
    // (highest id) and write a real scene.json with the requested name and
    // relPath so it shows up properly in the content browser.
    int newId = -1;
    for (const auto &e : mgr.getEntries()) {
      if (e.id > newId) newId = e.id;
    }
    if (newId < 0) { emitErr("scene create failed"); return 1; }
    auto scene = openScene(project, newId);
    scene->conf.name.value = a.name;
    if (!a.dir.empty()) scene->relPath = a.dir;
    saveScene(project, *scene);
    nlohmann::json out;
    out["created"] = newId;
    out["name"] = a.name;
    if (!a.dir.empty()) out["relPath"] = a.dir;
    emitJSON(out);
    return 0;
  }

  int cmdSceneDescribe(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required (scene id or name)"); return 1; }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    nlohmann::json out;
    out["id"] = id;
    out["name"] = scene->conf.name.value;
    if (!scene->relPath.empty()) out["relPath"] = scene->relPath;
    out["scene"] = nlohmann::json::parse(scene->serialize(), nullptr, false);
    emitJSON(out);
    return 0;
  }

  int cmdSceneRename(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.to.empty()) { emitErr("--asset and --to are required"); return 1; }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    project.getScenes().setSceneName(id, a.to);
    nlohmann::json out;
    out["renamed"] = {{"id", id}, {"to", a.to}};
    emitJSON(out);
    return 0;
  }

  int cmdSceneSetRelpath(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    // --value can be empty to move the scene back to the content-root folder.
    auto v = a.value.empty() ? std::string{} : parseValueJSON(a.value).get<std::string>();
    project.getScenes().setSceneRelPath(id, v);
    nlohmann::json out;
    out["updated"] = {{"id", id}, {"relPath", v}};
    emitJSON(out);
    return 0;
  }

  int cmdSceneDuplicate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto &mgr = project.getScenes();
    int beforeMax = -1;
    for (const auto &e : mgr.getEntries()) if (e.id > beforeMax) beforeMax = e.id;
    mgr.duplicate(id);
    int newId = -1;
    for (const auto &e : mgr.getEntries()) if (e.id > newId) newId = e.id;
    if (newId <= beforeMax) { emitErr("scene duplicate failed"); return 1; }
    nlohmann::json out;
    out["duplicated"] = {{"from", id}, {"to", newId}};
    emitJSON(out);
    return 0;
  }

  int cmdSceneDelete(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    if (project.getScenes().getEntries().size() <= 1) {
      emitErr("refusing to delete the only scene");
      return 1;
    }
    project.getScenes().remove(id);
    nlohmann::json out;
    out["deleted"] = id;
    emitJSON(out);
    return 0;
  }

  // Patch one field in SceneConf. Round-trips through the JSON form like
  // asset-set-conf so the same JSON shape works for every field.
  int cmdSceneSetConf(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field, --value are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto sceneJson = nlohmann::json::parse(scene->serialize(), nullptr, false);
    if (!sceneJson.is_object() || !sceneJson.contains("conf") || !sceneJson["conf"].is_object()) {
      emitErr("scene conf is not an object (corrupt scene?)"); return 1;
    }
    sceneJson["conf"][a.field] = parseValueJSON(a.value);
    scene->deserialize(sceneJson.dump());
    saveScene(project, *scene);
    nlohmann::json out;
    out["updated"] = a.field;
    out["scene"] = nlohmann::json::parse(scene->serialize(), nullptr, false);
    emitJSON(out);
    return 0;
  }

  // ── Scene tree editing ─────────────────────────────────────────────

  int cmdSceneAddObject(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty()) {
      emitErr("--asset and --name are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto *parent = findObjectByPath(&scene->getRootObject(), a.parent);
    if (!parent) { emitErr("parent path not found: " + a.parent); return 1; }
    auto obj = scene->addObject(*parent);
    obj->name = a.name;
    // --type canvas: shorthand for the Scene Graph "Add Canvas (2D)" menu —
    // marks the new object as a 2D canvas root so the 2D pipeline picks it up.
    if (a.type == "canvas" || a.type == "canvas2d") {
      obj->isCanvas2D = true;
    }
    saveScene(project, *scene);
    nlohmann::json out;
    out["addedObject"] = a.name;
    out["uuid"] = obj->uuid;
    out["sceneId"] = id;
    if (obj->isCanvas2D) out["isCanvas2D"] = true;
    emitJSON(out);
    return 0;
  }

  int cmdSceneRemoveObject(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.path.empty()) {
      emitErr("--asset and --path are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    Project::Object *parentObj = nullptr;
    size_t idx = 0;
    auto *target = findObjectByPathWithParent(&scene->getRootObject(), a.path, &parentObj, &idx);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    if (!parentObj) { emitErr("cannot remove the scene root"); return 1; }
    scene->removeObject(*target);
    saveScene(project, *scene);
    nlohmann::json out;
    out["removedPath"] = a.path;
    emitJSON(out);
    return 0;
  }

  int cmdSceneSetTransform(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field, --value are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto *target = findObjectByPath(&scene->getRootObject(), a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }

    auto v = parseValueJSON(a.value);
    bool ok = true;
    if (a.field == "pos" || a.field == "scale") {
      if (!v.is_array() || v.size() != 3) ok = false;
      else {
        glm::vec3 vec{ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
        if (a.field == "pos") target->pos.value = vec; else target->scale.value = vec;
      }
    } else if (a.field == "rot") {
      if (!v.is_array() || v.size() != 4) ok = false;
      else target->rot.value = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>()};
    } else if (a.field == "name") {
      if (!v.is_string()) ok = false; else target->name = v.get<std::string>();
    } else if (a.field == "enabled") {
      if (!v.is_boolean()) ok = false; else target->enabled = v.get<bool>();
    } else if (a.field == "selectable") {
      if (!v.is_boolean()) ok = false; else target->selectable = v.get<bool>();
    } else if (a.field == "isCanvas2D") {
      if (!v.is_boolean()) ok = false; else target->isCanvas2D = v.get<bool>();
    } else if (a.field == "anchor2D") {
      if (!v.is_number_integer()) ok = false; else target->anchor2D = (uint8_t)v.get<int>();
    } else if (a.field == "layerIndex2D") {
      if (!v.is_number_integer()) ok = false; else target->layerIndex2D = (uint8_t)v.get<int>();
    } else {
      emitErr("unknown field '" + a.field + "' (allowed: pos, rot, scale, name, enabled, selectable, isCanvas2D, anchor2D, layerIndex2D)");
      return 1;
    }
    if (!ok) { emitErr("--value did not match expected type for field '" + a.field + "'"); return 1; }

    saveScene(project, *scene);
    nlohmann::json out;
    out["updated"] = a.field;
    out["obj"] = target->serialize();
    emitJSON(out);
    return 0;
  }

  int cmdSceneAddComponent(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty()) {
      emitErr("--asset and --comp are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto *target = findObjectByPath(&scene->getRootObject(), a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    int compId = findCompId(a.comp);
    if (compId < 0) { emitErr("unknown component: " + a.comp); return 1; }
    target->addComponent(compId);
    saveScene(project, *scene);
    nlohmann::json out;
    out["added"] = Project::Component::TABLE[compId].name;
    out["obj"] = target->serialize();
    emitJSON(out);
    return 0;
  }

  int cmdSceneRemoveComponent(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty()) {
      emitErr("--asset and --comp are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto *target = findObjectByPath(&scene->getRootObject(), a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    auto *entry = findComponent(*target, a.comp);
    if (!entry) { emitErr("component not present: " + a.comp); return 1; }
    target->removeComponent(entry->uuid);
    saveScene(project, *scene);
    nlohmann::json out;
    out["removed"] = a.comp;
    out["obj"] = target->serialize();
    emitJSON(out);
    return 0;
  }

  int cmdSceneSetProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --comp, --field, --value are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto *target = findObjectByPath(&scene->getRootObject(), a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    auto *entry = findComponent(*target, a.comp);
    if (!entry) { emitErr("component not present: " + a.comp); return 1; }
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    if (!data.is_object()) { emitErr("component data is not a JSON object"); return 1; }
    if (!data.contains(a.field)) {
      emitErr("component '" + std::string(info.name) + "' has no field '" + a.field + "'");
      return 1;
    }
    data[a.field] = parseValueJSON(a.value);
    entry->data = info.funcDeserialize(data);
    saveScene(project, *scene);
    nlohmann::json out;
    out["updated"] = a.field;
    out["obj"] = target->serialize();
    emitJSON(out);
    return 0;
  }

  int cmdSceneMoveObject(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.path.empty()) {
      emitErr("--asset and --path are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto *target = findObjectByPath(&scene->getRootObject(), a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    auto *newParent = findObjectByPath(&scene->getRootObject(), a.parent);
    if (!newParent) { emitErr("parent path not found: " + a.parent); return 1; }
    if (!scene->moveObject(target->uuid, newParent->uuid, /*asChild=*/true)) {
      emitErr("move failed (target == new parent or new parent is a descendant)");
      return 1;
    }
    saveScene(project, *scene);
    nlohmann::json out;
    out["moved"] = a.path;
    out["under"] = a.parent;
    emitJSON(out);
    return 0;
  }

  // Spawn a prefab/widget instance under the chosen parent path. Mirrors the
  // editor's drag-drop-into-hierarchy gesture so headless callers can compose
  // scenes from prefabs the same way the GUI does.
  int cmdSceneAddPrefabInstance(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty()) {
      emitErr("--asset (scene) and --from (prefab/widget) are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto *prefabEntry = resolvePrefabOrWidget(project, a.from);
    if (!prefabEntry || !prefabEntry->prefab) {
      emitErr("prefab not found: " + a.from); return 1;
    }
    auto scene = openScene(project, id);
    auto *parent = findObjectByPath(&scene->getRootObject(), a.parent);
    if (!parent) { emitErr("parent path not found: " + a.parent); return 1; }

    auto inst = scene->addPrefabInstance(prefabEntry->prefab->uuid.value, parent);
    if (!inst) { emitErr("addPrefabInstance failed"); return 1; }
    if (!a.name.empty()) inst->name = a.name;
    saveScene(project, *scene);
    nlohmann::json out;
    out["instanced"] = inst->name;
    out["uuid"] = inst->uuid;
    out["fromPrefab"] = prefabEntry->name;
    emitJSON(out);
    return 0;
  }

  // ── Prefab reparent / promote ─────────────────────────────────────

  int cmdPrefabMoveObject(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.path.empty()) {
      emitErr("--asset and --path are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *srcParent = nullptr;
    size_t srcIdx = 0;
    auto *target = findObjectByPathWithParent(&e->prefab->obj, a.path, &srcParent, &srcIdx);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    if (!srcParent) { emitErr("cannot reparent the prefab root"); return 1; }
    auto *newParent = findObjectByPath(&e->prefab->obj, a.parent);
    if (!newParent) { emitErr("parent path not found: " + a.parent); return 1; }
    // Block moves into self/descendant: walk newParent up the parent chain.
    {
      auto isDescendant = [](const Project::Object *node, uint32_t ancestorUUID) {
        for (auto *cur = node; cur; cur = cur->parent) {
          if (cur->uuid == ancestorUUID) return true;
        }
        return false;
      };
      if (newParent == target || isDescendant(newParent, target->uuid)) {
        emitErr("cannot move into self or descendant"); return 1;
      }
    }
    auto held = srcParent->children[srcIdx];
    srcParent->children.erase(srcParent->children.begin() + srcIdx);
    held->parent = newParent;
    newParent->children.push_back(held);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["moved"] = a.path;
    out["under"] = a.parent;
    emitJSON(out);
    return 0;
  }

  // Make the named object the prefab's new top-level node, demoting the old
  // root to a child of it. Mirrors PrefabEditor's "Make Root" action.
  // Implemented via JSON round-trip because Object's user-declared
  // Object(Object&) ctor suppresses the implicit copy/move ctors and the
  // tree mutation needs deep clones.
  int cmdPrefabPromoteRoot(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.path.empty()) {
      emitErr("--asset and --path are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *srcParent = nullptr;
    size_t srcIdx = 0;
    auto *target = findObjectByPathWithParent(&e->prefab->obj, a.path, &srcParent, &srcIdx);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    if (target == &e->prefab->obj) { emitErr("already the prefab root"); return 1; }
    if (!srcParent) { emitErr("orphan target"); return 1; }

    // Snapshot the target's serialized form before mutating, then detach it
    // from its current parent so the leftover tree is the demoted subtree.
    auto targetJson = target->serialize();
    srcParent->children.erase(srcParent->children.begin() + srcIdx);
    auto oldRootJson = e->prefab->obj.serialize();

    // Build the new root: target's own subtree plus the demoted old root as
    // a final child, preserving every remaining lineage.
    nlohmann::json newJson = targetJson;
    if (!newJson.contains("children") || !newJson["children"].is_array()) {
      newJson["children"] = nlohmann::json::array();
    }
    newJson["children"].push_back(oldRootJson);

    e->prefab->obj = Project::Object{};
    e->prefab->obj.deserialize(nullptr, newJson);

    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["promoted"] = a.path;
    emitJSON(out);
    return 0;
  }

  // ── Path component granular ops ───────────────────────────────────

  // Resolve the Path component on the named object. Returns nullptr after
  // emitting a CLI error so callers can simply early-return on null.
  Project::Component::Entry *resolvePathComp(
    Project::AssetManagerEntry *e,
    const std::string &objPath,
    Project::Object **outObj)
  {
    auto *target = findObjectByPath(&e->prefab->obj, objPath);
    if (!target) { emitErr("path not found: " + objPath); return nullptr; }
    auto *entry = findComponent(*target, "Path");
    if (!entry) { emitErr("object has no Path component"); return nullptr; }
    *outObj = target;
    return entry;
  }

  void writePathBack(
    Project::Project &project,
    Project::AssetManagerEntry *e,
    Project::Component::Entry *entry,
    nlohmann::json &data)
  {
    const auto &info = Project::Component::TABLE[entry->id];
    entry->data = info.funcDeserialize(data);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
  }

  int cmdPathAddPoint(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.value.empty()) {
      emitErr("--asset and --value (e.g. '[x,y,z]') are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *obj = nullptr;
    auto *entry = resolvePathComp(e, a.path, &obj);
    if (!entry) return 1;
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    if (!data["points"].is_array()) data["points"] = nlohmann::json::array();
    auto v = parseValueJSON(a.value);
    nlohmann::json point;
    if (v.is_array() && v.size() == 3) {
      point = { {"pos", v}, {"tension", 0.5}, {"branchId", 0}, {"flags", 0} };
    } else if (v.is_object() && v.contains("pos")) {
      point = v;
      if (!point.contains("tension"))  point["tension"]  = 0.5;
      if (!point.contains("branchId")) point["branchId"] = 0;
      if (!point.contains("flags"))    point["flags"]    = 0;
    } else {
      emitErr("--value must be [x,y,z] or {pos:[x,y,z], tension, branchId, flags}");
      return 1;
    }
    data["points"].push_back(point);
    writePathBack(project, e, entry, data);
    nlohmann::json out;
    out["addedIndex"] = (int)data["points"].size() - 1;
    out["points"] = data["points"];
    emitJSON(out);
    return 0;
  }

  int cmdPathInsertPoint(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field (index), --value are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *obj = nullptr;
    auto *entry = resolvePathComp(e, a.path, &obj);
    if (!entry) return 1;
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    if (!data["points"].is_array()) data["points"] = nlohmann::json::array();
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx > (int)data["points"].size()) {
      emitErr("index out of range"); return 1;
    }
    auto v = parseValueJSON(a.value);
    nlohmann::json point;
    if (v.is_array() && v.size() == 3) {
      point = { {"pos", v}, {"tension", 0.5}, {"branchId", 0}, {"flags", 0} };
    } else if (v.is_object() && v.contains("pos")) {
      point = v;
      if (!point.contains("tension"))  point["tension"]  = 0.5;
      if (!point.contains("branchId")) point["branchId"] = 0;
      if (!point.contains("flags"))    point["flags"]    = 0;
    } else {
      emitErr("--value must be [x,y,z] or full point object"); return 1;
    }
    data["points"].insert(data["points"].begin() + idx, point);
    writePathBack(project, e, entry, data);
    nlohmann::json out;
    out["insertedAt"] = idx;
    out["points"] = data["points"];
    emitJSON(out);
    return 0;
  }

  int cmdPathSetPoint(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field (index), --value are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *obj = nullptr;
    auto *entry = resolvePathComp(e, a.path, &obj);
    if (!entry) return 1;
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    if (!data["points"].is_array()) { emitErr("no points to update"); return 1; }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx >= (int)data["points"].size()) {
      emitErr("index out of range"); return 1;
    }
    auto v = parseValueJSON(a.value);
    if (v.is_array() && v.size() == 3) {
      data["points"][idx]["pos"] = v;
    } else if (v.is_object()) {
      // Patch only the keys that were supplied so callers can tweak one knob.
      for (auto it = v.begin(); it != v.end(); ++it) {
        data["points"][idx][it.key()] = it.value();
      }
    } else {
      emitErr("--value must be [x,y,z] or partial point object"); return 1;
    }
    writePathBack(project, e, entry, data);
    nlohmann::json out;
    out["updated"] = idx;
    out["point"] = data["points"][idx];
    emitJSON(out);
    return 0;
  }

  int cmdPathRemovePoint(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty()) {
      emitErr("--asset and --field (index) are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *obj = nullptr;
    auto *entry = resolvePathComp(e, a.path, &obj);
    if (!entry) return 1;
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    if (!data["points"].is_array()) { emitErr("no points"); return 1; }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx >= (int)data["points"].size()) {
      emitErr("index out of range"); return 1;
    }
    data["points"].erase(data["points"].begin() + idx);
    writePathBack(project, e, entry, data);
    nlohmann::json out;
    out["removed"] = idx;
    out["points"] = data["points"];
    emitJSON(out);
    return 0;
  }

  int cmdPathAddBranch(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.value.empty()) {
      emitErr("--asset and --value (branch object) are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *obj = nullptr;
    auto *entry = resolvePathComp(e, a.path, &obj);
    if (!entry) return 1;
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    if (!data["branches"].is_array()) data["branches"] = nlohmann::json::array();
    auto v = parseValueJSON(a.value);
    if (!v.is_object()) {
      emitErr("--value must be an object: {fromIdx, branchId, op, flag, value}");
      return 1;
    }
    nlohmann::json br;
    br["fromIdx"]  = v.value("fromIdx",  0);
    br["branchId"] = v.value("branchId", 1);
    br["op"]       = v.value("op",       0);
    br["flag"]     = v.value("flag",     std::string{});
    br["value"]    = v.value("value",    0.0f);
    data["branches"].push_back(br);
    writePathBack(project, e, entry, data);
    nlohmann::json out;
    out["addedIndex"] = (int)data["branches"].size() - 1;
    out["branches"] = data["branches"];
    emitJSON(out);
    return 0;
  }

  int cmdPathSetBranch(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field (index), --value are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *obj = nullptr;
    auto *entry = resolvePathComp(e, a.path, &obj);
    if (!entry) return 1;
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    if (!data["branches"].is_array()) { emitErr("no branches"); return 1; }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx >= (int)data["branches"].size()) {
      emitErr("index out of range"); return 1;
    }
    auto v = parseValueJSON(a.value);
    if (!v.is_object()) { emitErr("--value must be a partial branch object"); return 1; }
    for (auto it = v.begin(); it != v.end(); ++it) {
      data["branches"][idx][it.key()] = it.value();
    }
    writePathBack(project, e, entry, data);
    nlohmann::json out;
    out["updated"] = idx;
    out["branch"] = data["branches"][idx];
    emitJSON(out);
    return 0;
  }

  int cmdPathRemoveBranch(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty()) {
      emitErr("--asset and --field (index) are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::Object *obj = nullptr;
    auto *entry = resolvePathComp(e, a.path, &obj);
    if (!entry) return 1;
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    if (!data["branches"].is_array()) { emitErr("no branches"); return 1; }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx >= (int)data["branches"].size()) {
      emitErr("index out of range"); return 1;
    }
    data["branches"].erase(data["branches"].begin() + idx);
    writePathBack(project, e, entry, data);
    nlohmann::json out;
    out["removed"] = idx;
    out["branches"] = data["branches"];
    emitJSON(out);
    return 0;
  }

  // ── Scene-side Path authoring ─────────────────────────────────────
  // Same granular ops as path-*, but operate on a Path component sitting on
  // an object inside a scene rather than inside a prefab/widget. Lets callers
  // place a one-off rail in a level without having to wrap it in a prefab.

  struct ScenePathCtx {
    Project::Scene *scene = nullptr;
    Project::Object *obj = nullptr;
    Project::Component::Entry *entry = nullptr;
  };

  bool resolveScenePathComp(
    Project::Project &project,
    const std::string &assetKey,
    const std::string &objPath,
    ScenePathCtx &out)
  {
    int id = resolveSceneId(project, assetKey);
    if (id < 0) { emitErr("scene not found: " + assetKey); return false; }
    auto *scene = openScene(project, id);
    if (!scene) { emitErr("could not open scene: " + assetKey); return false; }
    auto *target = findObjectByPath(&scene->getRootObject(), objPath);
    if (!target) { emitErr("path not found: " + objPath); return false; }
    auto *entry = findComponent(*target, "Path");
    if (!entry) { emitErr("object has no Path component"); return false; }
    out.scene = scene;
    out.obj = target;
    out.entry = entry;
    return true;
  }

  // Re-deserialize the Path data into the live entry and persist the scene.
  // saveScene() already triggers a reload, so unlike the prefab path we don't
  // need a second reload step here.
  void writeScenePathBack(
    Project::Project &project,
    ScenePathCtx &ctx,
    nlohmann::json &data)
  {
    const auto &info = Project::Component::TABLE[ctx.entry->id];
    ctx.entry->data = info.funcDeserialize(data);
    saveScene(project, *ctx.scene);
  }

  int cmdScenePathAddPoint(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.value.empty()) {
      emitErr("--asset (scene) and --value (e.g. '[x,y,z]') are required"); return 1;
    }
    ScenePathCtx ctx;
    if (!resolveScenePathComp(project, a.asset, a.path, ctx)) return 1;
    const auto &info = Project::Component::TABLE[ctx.entry->id];
    auto data = info.funcSerialize(*ctx.entry);
    if (!data["points"].is_array()) data["points"] = nlohmann::json::array();
    auto v = parseValueJSON(a.value);
    nlohmann::json point;
    if (v.is_array() && v.size() == 3) {
      point = { {"pos", v}, {"tension", 0.5}, {"branchId", 0}, {"flags", 0} };
    } else if (v.is_object() && v.contains("pos")) {
      point = v;
      if (!point.contains("tension"))  point["tension"]  = 0.5;
      if (!point.contains("branchId")) point["branchId"] = 0;
      if (!point.contains("flags"))    point["flags"]    = 0;
    } else {
      emitErr("--value must be [x,y,z] or {pos:[x,y,z], tension, branchId, flags}");
      return 1;
    }
    data["points"].push_back(point);
    writeScenePathBack(project, ctx, data);
    nlohmann::json out;
    out["addedIndex"] = (int)data["points"].size() - 1;
    out["points"] = data["points"];
    emitJSON(out);
    return 0;
  }

  int cmdScenePathInsertPoint(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset (scene), --field (index), --value are required"); return 1;
    }
    ScenePathCtx ctx;
    if (!resolveScenePathComp(project, a.asset, a.path, ctx)) return 1;
    const auto &info = Project::Component::TABLE[ctx.entry->id];
    auto data = info.funcSerialize(*ctx.entry);
    if (!data["points"].is_array()) data["points"] = nlohmann::json::array();
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx > (int)data["points"].size()) {
      emitErr("index out of range"); return 1;
    }
    auto v = parseValueJSON(a.value);
    nlohmann::json point;
    if (v.is_array() && v.size() == 3) {
      point = { {"pos", v}, {"tension", 0.5}, {"branchId", 0}, {"flags", 0} };
    } else if (v.is_object() && v.contains("pos")) {
      point = v;
      if (!point.contains("tension"))  point["tension"]  = 0.5;
      if (!point.contains("branchId")) point["branchId"] = 0;
      if (!point.contains("flags"))    point["flags"]    = 0;
    } else {
      emitErr("--value must be [x,y,z] or full point object"); return 1;
    }
    data["points"].insert(data["points"].begin() + idx, point);
    writeScenePathBack(project, ctx, data);
    nlohmann::json out;
    out["insertedAt"] = idx;
    out["points"] = data["points"];
    emitJSON(out);
    return 0;
  }

  int cmdScenePathSetPoint(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset (scene), --field (index), --value are required"); return 1;
    }
    ScenePathCtx ctx;
    if (!resolveScenePathComp(project, a.asset, a.path, ctx)) return 1;
    const auto &info = Project::Component::TABLE[ctx.entry->id];
    auto data = info.funcSerialize(*ctx.entry);
    if (!data["points"].is_array()) { emitErr("no points to update"); return 1; }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx >= (int)data["points"].size()) {
      emitErr("index out of range"); return 1;
    }
    auto v = parseValueJSON(a.value);
    if (v.is_array() && v.size() == 3) {
      data["points"][idx]["pos"] = v;
    } else if (v.is_object()) {
      for (auto it = v.begin(); it != v.end(); ++it) {
        data["points"][idx][it.key()] = it.value();
      }
    } else {
      emitErr("--value must be [x,y,z] or partial point object"); return 1;
    }
    writeScenePathBack(project, ctx, data);
    nlohmann::json out;
    out["updated"] = idx;
    out["point"] = data["points"][idx];
    emitJSON(out);
    return 0;
  }

  int cmdScenePathRemovePoint(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty()) {
      emitErr("--asset (scene) and --field (index) are required"); return 1;
    }
    ScenePathCtx ctx;
    if (!resolveScenePathComp(project, a.asset, a.path, ctx)) return 1;
    const auto &info = Project::Component::TABLE[ctx.entry->id];
    auto data = info.funcSerialize(*ctx.entry);
    if (!data["points"].is_array()) { emitErr("no points"); return 1; }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx >= (int)data["points"].size()) {
      emitErr("index out of range"); return 1;
    }
    data["points"].erase(data["points"].begin() + idx);
    writeScenePathBack(project, ctx, data);
    nlohmann::json out;
    out["removed"] = idx;
    out["points"] = data["points"];
    emitJSON(out);
    return 0;
  }

  int cmdScenePathAddBranch(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.value.empty()) {
      emitErr("--asset (scene) and --value (branch object) are required"); return 1;
    }
    ScenePathCtx ctx;
    if (!resolveScenePathComp(project, a.asset, a.path, ctx)) return 1;
    const auto &info = Project::Component::TABLE[ctx.entry->id];
    auto data = info.funcSerialize(*ctx.entry);
    if (!data["branches"].is_array()) data["branches"] = nlohmann::json::array();
    auto v = parseValueJSON(a.value);
    if (!v.is_object()) {
      emitErr("--value must be an object: {fromIdx, branchId, op, flag, value}");
      return 1;
    }
    nlohmann::json br;
    br["fromIdx"]  = v.value("fromIdx",  0);
    br["branchId"] = v.value("branchId", 1);
    br["op"]       = v.value("op",       0);
    br["flag"]     = v.value("flag",     std::string{});
    br["value"]    = v.value("value",    0.0f);
    data["branches"].push_back(br);
    writeScenePathBack(project, ctx, data);
    nlohmann::json out;
    out["addedIndex"] = (int)data["branches"].size() - 1;
    out["branches"] = data["branches"];
    emitJSON(out);
    return 0;
  }

  int cmdScenePathSetBranch(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset (scene), --field (index), --value are required"); return 1;
    }
    ScenePathCtx ctx;
    if (!resolveScenePathComp(project, a.asset, a.path, ctx)) return 1;
    const auto &info = Project::Component::TABLE[ctx.entry->id];
    auto data = info.funcSerialize(*ctx.entry);
    if (!data["branches"].is_array()) { emitErr("no branches"); return 1; }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx >= (int)data["branches"].size()) {
      emitErr("index out of range"); return 1;
    }
    auto v = parseValueJSON(a.value);
    if (!v.is_object()) { emitErr("--value must be a partial branch object"); return 1; }
    for (auto it = v.begin(); it != v.end(); ++it) {
      data["branches"][idx][it.key()] = it.value();
    }
    writeScenePathBack(project, ctx, data);
    nlohmann::json out;
    out["updated"] = idx;
    out["branch"] = data["branches"][idx];
    emitJSON(out);
    return 0;
  }

  int cmdScenePathRemoveBranch(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty()) {
      emitErr("--asset (scene) and --field (index) are required"); return 1;
    }
    ScenePathCtx ctx;
    if (!resolveScenePathComp(project, a.asset, a.path, ctx)) return 1;
    const auto &info = Project::Component::TABLE[ctx.entry->id];
    auto data = info.funcSerialize(*ctx.entry);
    if (!data["branches"].is_array()) { emitErr("no branches"); return 1; }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    if (idx < 0 || idx >= (int)data["branches"].size()) {
      emitErr("index out of range"); return 1;
    }
    data["branches"].erase(data["branches"].begin() + idx);
    writeScenePathBack(project, ctx, data);
    nlohmann::json out;
    out["removed"] = idx;
    out["branches"] = data["branches"];
    emitJSON(out);
    return 0;
  }

  // ── Resource type schema ──────────────────────────────────────────

  // Map a CLI-friendly type spelling to a VarKind. Keep names lowercase to
  // match the asset-list filter style used elsewhere in the CLI.
  bool parseVarKind(const std::string &s, Project::VarKind &out)
  {
    std::string n = s;
    std::transform(n.begin(), n.end(), n.begin(),
      [](unsigned char c){ return (char)std::tolower(c); });
    if (n == "int" || n == "int32")           { out = Project::VarKind::INT;        return true; }
    if (n == "float")                         { out = Project::VarKind::FLOAT;      return true; }
    if (n == "bool")                          { out = Project::VarKind::BOOL;       return true; }
    if (n == "vec3")                          { out = Project::VarKind::VEC3;       return true; }
    if (n == "quat")                          { out = Project::VarKind::QUAT;       return true; }
    if (n == "object_ref" || n == "objectref"){ out = Project::VarKind::OBJECT_REF; return true; }
    if (n == "prefab_ref" || n == "prefabref"){ out = Project::VarKind::PREFAB_REF; return true; }
    if (n == "asset_ref"  || n == "assetref") { out = Project::VarKind::ASSET_REF;  return true; }
    if (n == "array")                          { out = Project::VarKind::ARRAY;     return true; }
    return false;
  }

  // Map the --element-kind CLI string to the typeArg value the prefab
  // var stores. Mirrors ArrayMake / PrefabVarGet element-kind encoding:
  // 0=int, 1=float, 2=bool. Returns false on unknown spelling so the
  // caller can emit a clean error.
  bool parseElementKind(const std::string &s, uint16_t &out)
  {
    std::string n = s;
    std::transform(n.begin(), n.end(), n.begin(),
      [](unsigned char c){ return (char)std::tolower(c); });
    if (n == "int" || n == "int32") { out = 0; return true; }
    if (n == "float")               { out = 1; return true; }
    if (n == "bool")                { out = 2; return true; }
    return false;
  }

  // Round-trip the .p64restype JSON: load, mutate fields[], write back. We
  // touch the file directly because AssetManagerEntry::resourceType is
  // recomputed during reload(), so reading from-and-writing-to disk is the
  // source of truth.
  Project::Resource::Type loadRestype(Project::AssetManagerEntry *e)
  {
    Project::Resource::Type t{};
    t.deserialize(Utils::FS::loadTextFile(e->path));
    return t;
  }

  void saveRestype(Project::Project &project, Project::AssetManagerEntry *e, const Project::Resource::Type &t)
  {
    Utils::FS::saveTextFile(e->path, t.serialize());
    project.getAssets().reload();
  }

  int cmdRestypeCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.name.empty()) { emitErr("--name is required"); return 1; }
    uint64_t uuid = project.getAssets().createResourceType(a.name, a.dir);
    if (uuid == 0) { emitErr("createResourceType failed (name conflict?)"); return 1; }
    auto *e = project.getAssets().getEntryByUUID(uuid);
    nlohmann::json out;
    out["created"] = a.name + ".p64restype";
    if (e) out["asset"] = serializeAssetEntry(*e, true);
    emitJSON(out);
    return 0;
  }

  int cmdRestypeAddProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty() || a.type.empty()) {
      emitErr("--asset, --name, --type (int|float|bool|vec3|quat|object_ref|prefab_ref|asset_ref) are required");
      return 1;
    }
    auto *e = resolveAsset(project, a.asset, Project::FileType::RESOURCE_TYPE);
    if (!e) { emitErr("resource type not found: " + a.asset); return 1; }
    Project::VarKind kind{};
    if (!parseVarKind(a.type, kind)) { emitErr("unknown type: " + a.type); return 1; }
    auto t = loadRestype(e);
    for (const auto &f : t.fields) {
      if (f.name == a.name) { emitErr("field already exists: " + a.name); return 1; }
    }
    Project::VarDef v{};
    v.uuid = Utils::Hash::randomU64();
    v.name = a.name;
    v.kind = kind;
    if (!a.value.empty()) {
      // For PREFAB_REF / ASSET_REF the typeArg is a uuid; for the rest the
      // value is treated as the default. Numeric --value parses as typeArg
      // when it looks like a uuid, otherwise it is stored as the default.
      auto parsed = parseValueJSON(a.value);
      if ((kind == Project::VarKind::PREFAB_REF || kind == Project::VarKind::ASSET_REF)
          && parsed.is_number_unsigned()) {
        v.typeArg = parsed.get<uint64_t>();
      } else {
        v.defaultValue.deserialize(parsed.is_string() ? parsed.get<std::string>() : parsed.dump());
      }
    }
    t.fields.push_back(v);
    saveRestype(project, e, t);
    nlohmann::json out;
    out["added"] = a.name;
    out["uuid"] = v.uuid;
    emitJSON(out);
    return 0;
  }

  int cmdRestypeRemoveProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty()) {
      emitErr("--asset and --name are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset, Project::FileType::RESOURCE_TYPE);
    if (!e) { emitErr("resource type not found: " + a.asset); return 1; }
    auto t = loadRestype(e);
    auto before = t.fields.size();
    std::erase_if(t.fields, [&](const Project::VarDef &f){ return f.name == a.name; });
    if (t.fields.size() == before) { emitErr("field not found: " + a.name); return 1; }
    saveRestype(project, e, t);
    nlohmann::json out;
    out["removed"] = a.name;
    emitJSON(out);
    return 0;
  }

  int cmdRestypeDuplicateProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from (source field), --to (new field) are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset, Project::FileType::RESOURCE_TYPE);
    if (!e) { emitErr("resource type not found: " + a.asset); return 1; }
    auto t = loadRestype(e);
    const Project::VarDef *src = nullptr;
    for (const auto &f : t.fields) {
      if (f.name == a.from) { src = &f; }
      if (f.name == a.to)   { emitErr("field already exists: " + a.to); return 1; }
    }
    if (!src) { emitErr("field not found: " + a.from); return 1; }
    Project::VarDef copy = *src;
    copy.uuid = Utils::Hash::randomU64();
    copy.name = a.to;
    t.fields.push_back(copy);
    saveRestype(project, e, t);
    nlohmann::json out;
    out["duplicated"] = {{"from", a.from}, {"to", a.to}};
    out["newUUID"] = copy.uuid;
    emitJSON(out);
    return 0;
  }

  int cmdRestypeRenameProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from, --to are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset, Project::FileType::RESOURCE_TYPE);
    if (!e) { emitErr("resource type not found: " + a.asset); return 1; }
    auto t = loadRestype(e);
    bool renamed = false;
    for (auto &f : t.fields) {
      if (f.name == a.from) { f.name = a.to; renamed = true; break; }
    }
    if (!renamed) { emitErr("field not found: " + a.from); return 1; }
    saveRestype(project, e, t);
    nlohmann::json out;
    out["renamed"] = {{"from", a.from}, {"to", a.to}};
    emitJSON(out);
    return 0;
  }

  // ── Events ─────────────────────────────────────────────────────────

  // Map between event names and the on-device EVENT_TYPE_* sentinel values.
  // Mirrors n64/engine/include/scene/event.h.  Names are matched
  // case-insensitively against the suffix after EVENT_TYPE_.
  bool resolveEventValue(const std::string &key, uint16_t &out)
  {
    std::string n = key;
    std::transform(n.begin(), n.end(), n.begin(),
      [](unsigned char c){ return (char)std::toupper(c); });
    if (n == "ENABLE")  { out = (uint16_t)(0xFFFF - 0); return true; }
    if (n == "DISABLE") { out = (uint16_t)(0xFFFF - 1); return true; }
    if (n == "READY")   { out = (uint16_t)(0xFFFF - 2); return true; }
    return false;
  }

  int cmdEventList(const CLI::Commands::Args &/*a*/, Project::Project &/*project*/)
  {
    nlohmann::json out;
    out["builtins"] = nlohmann::json::array();
    auto add = [&](const char *name, uint16_t v, const char *desc) {
      nlohmann::json j;
      j["name"]  = name;
      j["value"] = v;
      j["description"] = desc;
      out["builtins"].push_back(j);
    };
    add("ENABLE",  (uint16_t)(0xFFFF - 0), "fires when an Object is enabled at runtime");
    add("DISABLE", (uint16_t)(0xFFFF - 1), "fires when an Object is disabled at runtime");
    add("READY",   (uint16_t)(0xFFFF - 2), "fires once after the scene finishes loading");
    out["customRange"] = {{"start", 0x0000}, {"end", 0xF000}};
    emitJSON(out);
    return 0;
  }

  // Sugar for setting Button2D-style eventType fields. Resolves --to from
  // either an event name (ENABLE/DISABLE/READY) or a raw integer in the
  // custom range.  Defaults --comp to Button2D so menu authoring stays
  // terse, but explicitly accepts other components that expose an
  // eventType-shaped field.
  int cmdWidgetBindEvent(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.to.empty()) {
      emitErr("--asset and --to (event name or numeric value) are required");
      return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    auto *target = findObjectByPath(&e->prefab->obj, a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    std::string compKey = a.comp.empty() ? std::string{"Button2D"} : a.comp;
    auto *entry = findComponent(*target, compKey);
    if (!entry) { emitErr("component not present: " + compKey); return 1; }
    const auto &info = Project::Component::TABLE[entry->id];
    auto data = info.funcSerialize(*entry);
    std::string fieldName = a.field.empty() ? std::string{"eventType"} : a.field;
    if (!data.is_object() || !data.contains(fieldName)) {
      emitErr("component '" + std::string(info.name) + "' has no field '" + fieldName + "'");
      return 1;
    }
    uint16_t evt = 0;
    if (resolveEventValue(a.to, evt)) {
      data[fieldName] = (int)evt;
    } else {
      auto v = parseValueJSON(a.to);
      if (!v.is_number_integer()) {
        emitErr("--to must be ENABLE/DISABLE/READY or a 16-bit integer");
        return 1;
      }
      data[fieldName] = v;
    }
    entry->data = info.funcDeserialize(data);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["bound"] = a.to;
    out["field"] = fieldName;
    out["value"] = data[fieldName];
    emitJSON(out);
    return 0;
  }

  // ── Prefab class variables ─────────────────────────────────────────

  // Set GenericValue contents from a JSON value, given the variable kind.
  // Returns false on type mismatch so callers can emit a helpful error.
  bool genericValueFromJSON(GenericValue &out, Project::VarKind kind, const nlohmann::json &v)
  {
    switch (kind) {
      case Project::VarKind::INT:
        if (!v.is_number_integer()) return false;
        out.set<int32_t>(v.get<int32_t>()); return true;
      case Project::VarKind::FLOAT:
        if (!v.is_number()) return false;
        out.set<float>(v.get<float>()); return true;
      case Project::VarKind::BOOL:
        if (!v.is_boolean()) return false;
        out.set<bool>(v.get<bool>()); return true;
      case Project::VarKind::VEC3:
        if (!v.is_array() || v.size() != 3) return false;
        out.set<glm::vec3>({v[0].get<float>(), v[1].get<float>(), v[2].get<float>()});
        return true;
      case Project::VarKind::QUAT:
        if (!v.is_array() || v.size() != 4) return false;
        out.set<glm::quat>({v[3].get<float>(), v[0].get<float>(), v[1].get<float>(), v[2].get<float>()});
        return true;
      case Project::VarKind::OBJECT_REF:
      case Project::VarKind::PREFAB_REF:
      case Project::VarKind::ASSET_REF:
        if (!v.is_number_unsigned()) return false;
        out.set<uint64_t>(v.get<uint64_t>()); return true;
    }
    return false;
  }

  int cmdPrefabAddVariable(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty() || a.type.empty()) {
      emitErr("--asset, --name, --type (int|float|bool|vec3|quat|object_ref|prefab_ref|asset_ref|array) are required");
      return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::VarKind kind{};
    if (!parseVarKind(a.type, kind)) { emitErr("unknown type: " + a.type); return 1; }
    for (const auto &v : e->prefab->variables) {
      if (v.name == a.name) { emitErr("variable already exists: " + a.name); return 1; }
    }
    Project::PrefabVarDef v{};
    v.uuid = Utils::Hash::randomU64();
    v.name = a.name;
    v.kind = kind;
    if (kind == Project::VarKind::ARRAY) {
      if (a.elementKind.empty()) {
        emitErr("--element-kind (int|float|bool) is required when --type=array");
        return 1;
      }
      uint16_t ek = 1;
      if (!parseElementKind(a.elementKind, ek)) {
        emitErr("unknown element kind: " + a.elementKind); return 1;
      }
      v.typeArg = ek;
    }
    if (!a.value.empty()) {
      auto parsed = parseValueJSON(a.value);
      if ((kind == Project::VarKind::PREFAB_REF || kind == Project::VarKind::ASSET_REF)
          && parsed.is_number_unsigned()) {
        v.typeArg = parsed.get<uint64_t>();
      } else if (kind == Project::VarKind::ARRAY) {
        // ARRAY default is always empty in v1; ignore --value rather
        // than failing so prefab-set-prop can author per-element initial
        // contents at build time later.
      } else if (!genericValueFromJSON(v.defaultValue, kind, parsed)) {
        emitErr("--value type does not match var kind"); return 1;
      }
    }
    e->prefab->variables.push_back(v);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["added"] = a.name;
    out["uuid"] = v.uuid;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabRemoveVariable(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty()) {
      emitErr("--asset and --name are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    auto before = e->prefab->variables.size();
    std::erase_if(e->prefab->variables,
      [&](const Project::PrefabVarDef &v){ return v.name == a.name; });
    if (e->prefab->variables.size() == before) {
      emitErr("variable not found: " + a.name); return 1;
    }
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["removed"] = a.name;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabRenameVariable(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from, --to are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    bool renamed = false;
    for (auto &v : e->prefab->variables) {
      if (v.name == a.from) { v.name = a.to; renamed = true; break; }
    }
    if (!renamed) { emitErr("variable not found: " + a.from); return 1; }
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["renamed"] = {{"from", a.from}, {"to", a.to}};
    emitJSON(out);
    return 0;
  }

  // Clone an existing prefab variable into a new entry with a fresh uuid.
  // The clone copies kind, typeArg, and default value verbatim.
  int cmdPrefabDuplicateVariable(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from (source name), --to (new name) are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    const Project::PrefabVarDef *src = nullptr;
    for (const auto &v : e->prefab->variables) {
      if (v.name == a.from) { src = &v; break; }
      if (v.name == a.to)   { emitErr("variable already exists: " + a.to); return 1; }
    }
    if (!src) { emitErr("variable not found: " + a.from); return 1; }
    Project::PrefabVarDef copy = *src;
    copy.uuid = Utils::Hash::randomU64();
    copy.name = a.to;
    e->prefab->variables.push_back(copy);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["duplicated"] = {{"from", a.from}, {"to", a.to}};
    out["newUUID"] = copy.uuid;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabSetVariableDefault(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty() || a.value.empty()) {
      emitErr("--asset, --name, --value are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    Project::PrefabVarDef *target = nullptr;
    for (auto &v : e->prefab->variables) {
      if (v.name == a.name) { target = &v; break; }
    }
    if (!target) { emitErr("variable not found: " + a.name); return 1; }
    auto parsed = parseValueJSON(a.value);
    if (!genericValueFromJSON(target->defaultValue, target->kind, parsed)) {
      emitErr("--value type does not match var kind"); return 1;
    }
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["updated"] = a.name;
    emitJSON(out);
    return 0;
  }

  // Per-instance variable override on a scene Object that points at a prefab.
  // Mirrors the editor's "Override" toggle on the prefab variable inspector.
  int cmdSceneSetVarOverride(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty() || a.value.empty()) {
      emitErr("--asset, --name (var name), --value are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto *target = findObjectByPath(&scene->getRootObject(), a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    if (target->uuidPrefab.value == 0) {
      emitErr("object is not a prefab instance"); return 1;
    }
    auto *prefabEntry = project.getAssets().getEntryByUUID(target->uuidPrefab.value);
    if (!prefabEntry || !prefabEntry->prefab) {
      emitErr("backing prefab not found"); return 1;
    }
    const Project::PrefabVarDef *varDef = nullptr;
    for (const auto &v : prefabEntry->prefab->variables) {
      if (v.name == a.name) { varDef = &v; break; }
    }
    if (!varDef) { emitErr("prefab has no variable: " + a.name); return 1; }
    GenericValue gv{};
    if (!genericValueFromJSON(gv, varDef->kind, parseValueJSON(a.value))) {
      emitErr("--value type does not match var kind"); return 1;
    }
    target->varOverrides[varDef->uuid] = gv;
    saveScene(project, *scene);
    nlohmann::json out;
    out["overridden"] = a.name;
    out["objUUID"] = target->uuid;
    emitJSON(out);
    return 0;
  }

  int cmdSceneClearVarOverride(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty()) {
      emitErr("--asset, --name (var name) are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto *target = findObjectByPath(&scene->getRootObject(), a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    if (target->uuidPrefab.value == 0) {
      emitErr("object is not a prefab instance"); return 1;
    }
    auto *prefabEntry = project.getAssets().getEntryByUUID(target->uuidPrefab.value);
    if (!prefabEntry || !prefabEntry->prefab) {
      emitErr("backing prefab not found"); return 1;
    }
    uint64_t varUUID = 0;
    for (const auto &v : prefabEntry->prefab->variables) {
      if (v.name == a.name) { varUUID = v.uuid; break; }
    }
    if (varUUID == 0) { emitErr("prefab has no variable: " + a.name); return 1; }
    if (!target->varOverrides.erase(varUUID)) {
      emitErr("no override set for: " + a.name); return 1;
    }
    saveScene(project, *scene);
    nlohmann::json out;
    out["cleared"] = a.name;
    emitJSON(out);
    return 0;
  }

  // ── Scene render layers ────────────────────────────────────────────

  // Validate the layer-bucket name (3D, ptx, 2D) and return a JSON pointer
  // path so set-conf-style mutation can locate the array.
  bool resolveLayerBucket(const std::string &type, std::string &outKey)
  {
    if (type == "3d" || type == "3D" || type == "layers3D") { outKey = "layers3D"; return true; }
    if (type == "2d" || type == "2D" || type == "layers2D") { outKey = "layers2D"; return true; }
    if (type == "ptx" || type == "particles" || type == "layersPtx") { outKey = "layersPtx"; return true; }
    return false;
  }

  // Build a fresh LayerConf as JSON with sensible defaults for the 3D bucket.
  nlohmann::json defaultLayerJSON(const std::string &name)
  {
    return {
      {"name", name},
      {"depthCompare", true},
      {"depthWrite", true},
      {"blender", 0},
      {"fog", false},
      {"fogColorMode", 0},
      {"fogColor", {0, 0, 0, 1}},
      {"fogMin", 0.0},
      {"fogMax", 0.0},
      {"lightMode", 0},
    };
  }

  int cmdSceneAddLayer(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.type.empty() || a.name.empty()) {
      emitErr("--asset (scene), --type (3d|2d|ptx), --name are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    std::string bucket;
    if (!resolveLayerBucket(a.type, bucket)) {
      emitErr("--type must be 3d|2d|ptx"); return 1;
    }
    auto scene = openScene(project, id);
    auto sceneJson = nlohmann::json::parse(scene->serialize(), nullptr, false);
    if (!sceneJson["conf"][bucket].is_array()) {
      sceneJson["conf"][bucket] = nlohmann::json::array();
    }
    sceneJson["conf"][bucket].push_back(defaultLayerJSON(a.name));
    scene->deserialize(sceneJson.dump());
    saveScene(project, *scene);
    nlohmann::json out;
    out["addedIndex"] = (int)sceneJson["conf"][bucket].size() - 1;
    out["bucket"] = bucket;
    emitJSON(out);
    return 0;
  }

  int cmdSceneSetLayer(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.type.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --type (bucket), --field (index), --value (partial layer object) are required");
      return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    std::string bucket;
    if (!resolveLayerBucket(a.type, bucket)) {
      emitErr("--type must be 3d|2d|ptx"); return 1;
    }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    auto scene = openScene(project, id);
    auto sceneJson = nlohmann::json::parse(scene->serialize(), nullptr, false);
    auto &arr = sceneJson["conf"][bucket];
    if (!arr.is_array() || idx < 0 || idx >= (int)arr.size()) {
      emitErr("layer index out of range"); return 1;
    }
    auto v = parseValueJSON(a.value);
    if (!v.is_object()) { emitErr("--value must be a partial layer object"); return 1; }
    for (auto it = v.begin(); it != v.end(); ++it) {
      arr[idx][it.key()] = it.value();
    }
    scene->deserialize(sceneJson.dump());
    saveScene(project, *scene);
    nlohmann::json out;
    out["updated"] = idx;
    out["layer"] = arr[idx];
    emitJSON(out);
    return 0;
  }

  int cmdSceneRemoveLayer(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.type.empty() || a.field.empty()) {
      emitErr("--asset, --type, --field (index) are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    std::string bucket;
    if (!resolveLayerBucket(a.type, bucket)) {
      emitErr("--type must be 3d|2d|ptx"); return 1;
    }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    auto scene = openScene(project, id);
    auto sceneJson = nlohmann::json::parse(scene->serialize(), nullptr, false);
    auto &arr = sceneJson["conf"][bucket];
    if (!arr.is_array() || idx < 0 || idx >= (int)arr.size()) {
      emitErr("layer index out of range"); return 1;
    }
    arr.erase(arr.begin() + idx);
    scene->deserialize(sceneJson.dump());
    saveScene(project, *scene);
    nlohmann::json out;
    out["removed"] = idx;
    out["bucket"] = bucket;
    emitJSON(out);
    return 0;
  }

  // Clone an existing layer entry. --field (source index) is required;
  // --name (new layer name) optional — if absent the clone reuses the
  // source's name with " (copy)" appended.
  int cmdSceneDuplicateLayer(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.type.empty() || a.field.empty()) {
      emitErr("--asset, --type (3d|2d|ptx), --field (source index) are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    std::string bucket;
    if (!resolveLayerBucket(a.type, bucket)) {
      emitErr("--type must be 3d|2d|ptx"); return 1;
    }
    int idx = 0;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index"); return 1;
    }
    auto scene = openScene(project, id);
    auto sceneJson = nlohmann::json::parse(scene->serialize(), nullptr, false);
    auto &arr = sceneJson["conf"][bucket];
    if (!arr.is_array() || idx < 0 || idx >= (int)arr.size()) {
      emitErr("layer index out of range"); return 1;
    }
    nlohmann::json copy = arr[idx];
    if (!a.name.empty()) {
      copy["name"] = a.name;
    } else if (copy.contains("name") && copy["name"].is_string()) {
      copy["name"] = copy["name"].get<std::string>() + " (copy)";
    }
    arr.push_back(copy);
    scene->deserialize(sceneJson.dump());
    saveScene(project, *scene);
    nlohmann::json out;
    out["duplicated"] = idx;
    out["addedIndex"] = (int)arr.size() - 1;
    out["bucket"] = bucket;
    emitJSON(out);
    return 0;
  }

  // Replicate the layer panel's "Reset all layers": shrink each layer
  // table to engine defaults and let Scene::resetLayers() handle the
  // ref-remapping semantics (component layerIdx fields stay in range).
  int cmdSceneResetLayers(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    scene->resetLayers();
    saveScene(project, *scene);
    auto sceneJson = nlohmann::json::parse(scene->serialize(), nullptr, false);
    nlohmann::json out;
    out["sceneId"] = id;
    out["layers3D"] = sceneJson["conf"].value("layers3D", nlohmann::json::array());
    out["layers2D"] = sceneJson["conf"].value("layers2D", nlohmann::json::array());
    out["layersPtx"] = sceneJson["conf"].value("layersPtx", nlohmann::json::array());
    emitJSON(out);
    return 0;
  }

  // ── Project conf ───────────────────────────────────────────────────

  int cmdProjectDescribe(const CLI::Commands::Args &/*a*/, Project::Project &project)
  {
    nlohmann::json out;
    out["path"] = project.getPath();
    out["conf"] = nlohmann::json::parse(project.conf.serialize(), nullptr, false);
    out["sceneCount"] = (int)project.getScenes().getEntries().size();
    emitJSON(out);
    return 0;
  }

  int cmdProjectSetConf(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.field.empty() || a.value.empty()) {
      emitErr("--field, --value are required"); return 1;
    }
    // Patch project.p64proj directly: the in-memory Project::deserialize is
    // private, so we round-trip through the on-disk JSON. The next CLI
    // invocation will pick up the change naturally; for the response we echo
    // the patched JSON so the caller can verify.
    auto cfgPath = fs::path(project.getPath()) / "project.p64proj";
    auto j = Utils::JSON::loadFile(cfgPath);
    if (!j.is_object()) { emitErr("project conf is not an object"); return 1; }
    j[a.field] = parseValueJSON(a.value);
    Utils::FS::saveTextFile(cfgPath.string(), j.dump(2));
    nlohmann::json out;
    out["updated"] = a.field;
    out["conf"] = j;
    emitJSON(out);
    return 0;
  }

  // Per-index helper for the 8 collLayerN entries on the project conf.
  // The generic project-set-conf would also work (key = collLayer0..7),
  // but this command validates the index and gives a cleaner echo.
  int cmdProjectSetCollisionLayer(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.field.empty() || a.name.empty()) {
      emitErr("--field (index 0-7), --name (layer name) are required"); return 1;
    }
    int idx = -1;
    try { idx = std::stoi(a.field); } catch (...) {
      emitErr("--field must be a numeric index 0-7"); return 1;
    }
    if (idx < 0 || idx > 7) { emitErr("collision layer index out of range (0-7)"); return 1; }
    auto cfgPath = fs::path(project.getPath()) / "project.p64proj";
    auto j = Utils::JSON::loadFile(cfgPath);
    if (!j.is_object()) { emitErr("project conf is not an object"); return 1; }
    std::string key = "collLayer" + std::to_string(idx);
    j[key] = a.name;
    Utils::FS::saveTextFile(cfgPath.string(), j.dump(2));
    nlohmann::json out;
    out["updated"] = key;
    out["value"] = a.name;
    emitJSON(out);
    return 0;
  }

  // ── Folders ────────────────────────────────────────────────────────

  // Resolve the on-disk folder pair (assets/<rel>, src/user/<rel>) for a
  // virtual folder path. The CLI presents one logical path; under the hood
  // we mirror across both physical roots so the editor's content browser
  // sees a consistent tree.
  std::pair<fs::path, fs::path> folderPaths(Project::Project &project, const std::string &rel)
  {
    fs::path projRoot{project.getPath()};
    return { projRoot / "assets" / rel, projRoot / "src" / "user" / rel };
  }

  int cmdFolderCreate(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.path.empty()) { emitErr("--path is required"); return 1; }
    auto [aDir, sDir] = folderPaths(project, a.path);
    std::error_code ec;
    fs::create_directories(aDir, ec);
    fs::create_directories(sDir, ec);
    project.getAssets().reload();
    nlohmann::json out;
    out["created"] = a.path;
    emitJSON(out);
    return 0;
  }

  int cmdFolderRename(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.path.empty() || a.to.empty()) {
      emitErr("--path (existing folder) and --to (new last segment) are required"); return 1;
    }
    fs::path rel{a.path};
    fs::path newRel = rel.parent_path() / a.to;
    auto [aOld, sOld] = folderPaths(project, rel.string());
    auto [aNew, sNew] = folderPaths(project, newRel.string());
    if (!fs::exists(aOld) && !fs::exists(sOld)) {
      emitErr("folder not found: " + a.path); return 1;
    }
    if (fs::exists(aNew) || fs::exists(sNew)) {
      emitErr("destination exists"); return 1;
    }
    std::error_code ec;
    if (fs::exists(aOld)) fs::rename(aOld, aNew, ec);
    if (fs::exists(sOld)) fs::rename(sOld, sNew, ec);
    project.getScenes().renameSceneFolder(rel.string(), newRel.string());
    project.getAssets().reload();
    nlohmann::json out;
    out["renamed"] = {{"from", rel.string()}, {"to", newRel.string()}};
    emitJSON(out);
    return 0;
  }

  int cmdFolderMove(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.path.empty() || a.dest.empty()) {
      emitErr("--path (folder) and --dest (new parent folder) are required"); return 1;
    }
    fs::path rel{a.path};
    std::string folderName = rel.filename().string();
    fs::path newRel = fs::path(a.dest) / folderName;
    auto [aOld, sOld] = folderPaths(project, rel.string());
    auto [aNew, sNew] = folderPaths(project, newRel.string());
    if (!fs::exists(aOld) && !fs::exists(sOld)) {
      emitErr("folder not found: " + a.path); return 1;
    }
    if (fs::exists(aNew) || fs::exists(sNew)) {
      emitErr("destination exists"); return 1;
    }
    std::error_code ec;
    fs::create_directories(aNew.parent_path(), ec);
    fs::create_directories(sNew.parent_path(), ec);
    if (fs::exists(aOld)) fs::rename(aOld, aNew, ec);
    if (fs::exists(sOld)) fs::rename(sOld, sNew, ec);
    project.getScenes().renameSceneFolder(rel.string(), newRel.string());
    project.getAssets().reload();
    nlohmann::json out;
    out["moved"] = {{"from", rel.string()}, {"to", newRel.string()}};
    emitJSON(out);
    return 0;
  }

  int cmdFolderDelete(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.path.empty()) { emitErr("--path is required"); return 1; }
    auto [aDir, sDir] = folderPaths(project, a.path);
    if (!fs::exists(aDir) && !fs::exists(sDir)) {
      emitErr("folder not found: " + a.path); return 1;
    }
    std::error_code ec;
    uintmax_t removed = 0;
    if (fs::exists(aDir)) removed += fs::remove_all(aDir, ec);
    if (fs::exists(sDir)) removed += fs::remove_all(sDir, ec);
    project.getAssets().reload();
    nlohmann::json out;
    out["deleted"] = a.path;
    out["removedCount"] = removed;
    emitJSON(out);
    return 0;
  }

  // ── Asset move ─────────────────────────────────────────────────────

  // Relocate an asset to a different folder under <project>/assets while
  // preserving its filename and side-car .conf. Companion files outside
  // <project>/assets (e.g. src/user/.cpp pairs for prefabs) are left alone
  // because asset folders and source folders are tracked independently.
  int cmdAssetMove(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.dest.empty()) {
      emitErr("--asset and --dest are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    fs::path src{e->path};
    fs::path destDir{a.dest};
    if (!destDir.is_absolute()) destDir = fs::path(project.getPath()) / destDir;
    std::error_code ec;
    fs::create_directories(destDir, ec);
    fs::path dest = destDir / src.filename();
    if (fs::exists(dest)) { emitErr("destination exists: " + dest.string()); return 1; }
    fs::rename(src, dest, ec);
    if (ec) { emitErr("move failed: " + ec.message()); return 1; }
    fs::path srcConf = src.string() + ".conf";
    if (fs::exists(srcConf)) {
      fs::rename(srcConf, dest.string() + ".conf", ec);
    }
    project.getAssets().reload();
    auto *fresh = project.getAssets().getByPath(dest.string());
    nlohmann::json out;
    out["moved"] = {{"from", src.string()}, {"to", dest.string()}};
    if (fresh) out["asset"] = serializeAssetEntry(*fresh, false);
    emitJSON(out);
    return 0;
  }

  // ── Material editing ───────────────────────────────────────────────

  int cmdMaterialSetProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field, --value are required"); return 1;
    }
    auto *e = resolveAsset(project, a.asset, Project::FileType::MATERIAL);
    if (!e || !e->materialAsset) {
      emitErr("material not found: " + a.asset); return 1;
    }
    auto compiled = e->materialAsset->compiled.serialize();
    if (!compiled.contains(a.field)) {
      emitErr("material has no field '" + a.field + "'"); return 1;
    }
    compiled[a.field] = parseValueJSON(a.value);
    e->materialAsset->compiled.deserialize(compiled);
    Utils::FS::saveTextFile(e->path, e->materialAsset->serialize());
    project.getAssets().reload();
    nlohmann::json out;
    out["updated"] = a.field;
    out["material"] = compiled;
    emitJSON(out);
    return 0;
  }

  // ── Conf schema introspection ──────────────────────────────────────

  // Return the keys (and current types) of an asset's .conf. Lets headless
  // callers discover what `asset-set-conf --field X` will accept without
  // having to rummage through the .conf file by hand.
  int cmdAssetDescribeConf(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolveAsset(project, a.asset);
    if (!e) { emitErr("asset not found: " + a.asset); return 1; }
    auto j = nlohmann::json::parse(e->conf.serialize(), nullptr, false);
    nlohmann::json out;
    out["uuid"] = e->conf.uuid;
    out["name"] = e->name;
    out["type"] = fileTypeName(e->type);
    nlohmann::json fields = nlohmann::json::array();
    if (j.is_object()) {
      for (auto it = j.begin(); it != j.end(); ++it) {
        nlohmann::json f;
        f["name"] = it.key();
        f["jsonType"] = it.value().type_name();
        f["current"] = it.value();
        fields.push_back(f);
      }
    }
    out["fields"] = fields;
    emitJSON(out);
    return 0;
  }

  // ── Queries ────────────────────────────────────────────────────────

  // Walk every Object in a tree, calling `visit(obj, parentPathSlashed)`.
  void walkTree(Project::Object &obj, const std::string &parentPath,
                const std::function<void(Project::Object&, const std::string&)> &visit)
  {
    std::string here = parentPath.empty() ? obj.name : (parentPath + "/" + obj.name);
    visit(obj, here);
    for (auto &c : obj.children) {
      walkTree(*c, here, visit);
    }
  }

  int cmdSceneFind(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty()) {
      emitErr("--asset (scene), --comp are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    int compId = findCompId(a.comp);
    if (compId < 0) { emitErr("unknown component: " + a.comp); return 1; }
    auto scene = openScene(project, id);
    nlohmann::json hits = nlohmann::json::array();
    walkTree(scene->getRootObject(), "",
      [&](Project::Object &o, const std::string &path) {
        for (const auto &c : o.components) {
          if (c.id == compId) {
            nlohmann::json hit;
            hit["path"] = path;
            hit["uuid"] = o.uuid;
            hits.push_back(hit);
            break;
          }
        }
      });
    nlohmann::json out;
    out["matches"] = hits;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabFind(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty()) {
      emitErr("--asset (prefab/widget), --comp are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    int compId = findCompId(a.comp);
    if (compId < 0) { emitErr("unknown component: " + a.comp); return 1; }
    nlohmann::json hits = nlohmann::json::array();
    walkTree(e->prefab->obj, "",
      [&](Project::Object &o, const std::string &path) {
        for (const auto &c : o.components) {
          if (c.id == compId) {
            nlohmann::json hit;
            hit["path"] = path;
            hit["uuid"] = o.uuid;
            hits.push_back(hit);
            break;
          }
        }
      });
    nlohmann::json out;
    out["matches"] = hits;
    emitJSON(out);
    return 0;
  }

  // List every scene+object that instantiates the named prefab. Useful for
  // safe-rename / safe-delete workflows where the caller wants to know
  // what will break before committing.
  int cmdPrefabFindReferences(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    uint64_t targetUUID = e->prefab->uuid.value;
    nlohmann::json refs = nlohmann::json::array();
    for (const auto &sceneEntry : project.getScenes().getEntries()) {
      auto scene = openScene(project, sceneEntry.id);
      walkTree(scene->getRootObject(), "",
        [&](Project::Object &o, const std::string &path) {
          if (o.uuidPrefab.value == targetUUID) {
            nlohmann::json hit;
            hit["sceneId"]   = sceneEntry.id;
            hit["sceneName"] = sceneEntry.name;
            hit["path"]      = path;
            hit["uuid"]      = o.uuid;
            refs.push_back(hit);
          }
        });
    }
    // Also scan other prefabs for variant inheritance and embedded instances.
    for (const auto &pe : project.getAssets().getTypeEntries(Project::FileType::PREFAB)) {
      if (!pe.prefab) continue;
      if (pe.conf.uuid == e->conf.uuid) continue;
      if (pe.prefab->uuidParentPrefab.value == targetUUID) {
        nlohmann::json hit;
        hit["variantOf"] = e->name;
        hit["prefab"]    = pe.name;
        hit["uuid"]      = pe.conf.uuid;
        refs.push_back(hit);
      }
    }
    nlohmann::json out;
    out["target"] = e->name;
    out["targetUUID"] = targetUUID;
    out["references"] = refs;
    emitJSON(out);
    return 0;
  }

  // List assets that no other asset/scene references. Conservative: only
  // checks for prefab uuid and component data uuid mentions. Caller treats
  // the result as a candidate list, not a hard "safe to delete" verdict.
  int cmdAssetFindUnused(const CLI::Commands::Args &a, Project::Project &project)
  {
    Project::FileType filter = Project::FileType::UNKNOWN;
    if (!a.type.empty()) {
      filter = parseFileTypeName(a.type);
      if (filter == Project::FileType::UNKNOWN && a.type != "unknown") {
        emitErr("unknown asset type '" + a.type + "'"); return 1;
      }
    }

    // Build a corpus of every JSON document we'd want to grep.
    std::string corpus;
    auto append = [&](const std::string &s){ corpus += s; corpus += '\n'; };
    for (const auto &sceneEntry : project.getScenes().getEntries()) {
      auto scene = openScene(project, sceneEntry.id);
      append(scene->serialize());
    }
    auto &am = project.getAssets();
    for (int i = 0; i < (int)Project::FileType::_SIZE; ++i) {
      for (const auto &pe : am.getTypeEntries((Project::FileType)i)) {
        if (pe.prefab) append(pe.prefab->serialize());
        if (pe.materialAsset) append(pe.materialAsset->serialize());
        if (pe.resource) append(pe.resource->serialize());
      }
    }

    nlohmann::json out = nlohmann::json::array();
    for (int i = 0; i < (int)Project::FileType::_SIZE; ++i) {
      auto t = (Project::FileType)i;
      if (filter != Project::FileType::UNKNOWN && t != filter) continue;
      for (const auto &pe : am.getTypeEntries(t)) {
        std::string needle = std::to_string(pe.conf.uuid);
        if (corpus.find(needle) == std::string::npos) {
          nlohmann::json j;
          j["uuid"] = pe.conf.uuid;
          j["name"] = pe.name;
          j["type"] = fileTypeName(pe.type);
          j["path"] = pe.path;
          out.push_back(j);
        }
      }
    }
    emitJSON(out);
    return 0;
  }

  // ── Project bootstrap ──────────────────────────────────────────────

  // Mirrors PROJECT_CREATE in editor/globalActions.cpp, but standalone so
  // it does not require ctx.project state. Resolves the empty-template path
  // relative to the editor binary's working directory.
  // Project-less variant (the dispatch's bootstrap path uses this; the
  // registered wrapper just forwards). Keeps the rest of the registry
  // uniform while letting cli.cpp invoke it before any Project ctor.
  int doProjectCreate(const CLI::Commands::Args &a)
  {
    if (a.path.empty() || a.name.empty()) {
      emitErr("--path (target dir) and --name are required"); return 1;
    }
    fs::path target{a.path};
    std::error_code ec;
    fs::create_directories(target, ec);
    if (ec) { emitErr("failed to create dir: " + ec.message()); return 1; }
    if (!fs::is_empty(target, ec)) {
      emitErr("target directory is not empty"); return 1;
    }
    fs::path templ = fs::path("n64") / "examples" / "empty";
    if (!fs::exists(templ)) {
      emitErr("template missing: " + templ.string() + " (run from repo root)");
      return 1;
    }
    fs::copy(templ, target,
      fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) { emitErr("template copy failed: " + ec.message()); return 1; }
    fs::remove(target / "p64_project.z64", ec);
    fs::remove(target / "Makefile", ec);
    fs::remove_all(target / "build", ec);
    fs::remove_all(target / "filesystem", ec);

    auto cfgPath = target / "project.p64proj";
    auto cfg = Utils::JSON::loadFile(cfgPath);
    if (!cfg.is_object()) cfg = nlohmann::json::object();
    cfg["name"] = a.name;
    if (!a.value.empty()) cfg["romName"] = a.value;
    Utils::FS::saveTextFile(cfgPath.string(), cfg.dump(2));

    nlohmann::json out;
    out["created"] = cfgPath.string();
    out["name"] = a.name;
    emitJSON(out);
    return 0;
  }

  // Registry wrapper — forwards to doProjectCreate so the standard
  // (Args, Project&) signature works for `dispatch()` lookups. The
  // Project& is unused (a fresh project has nothing to read from).
  int cmdProjectCreate(const CLI::Commands::Args &a, Project::Project &/*project*/)
  {
    return doProjectCreate(a);
  }

  // ── Variant inheritance commands ───────────────────────────────────
  //
  // The legacy RFC-6902 patchOps commands (list/add/remove-patch) are gone;
  // the structured override model replaces them. Shims below print a clear
  // deprecation message so existing scripts get a useful error instead of
  // "unknown command".

  int cmdPrefabListPatches(const CLI::Commands::Args &/*a*/, Project::Project &/*project*/)
  {
    emitErr("prefab-list-patches is removed — use prefab-describe-inheritance");
    return 1;
  }

  int cmdPrefabAddPatch(const CLI::Commands::Args &/*a*/, Project::Project &/*project*/)
  {
    emitErr("prefab-add-patch is removed — use prefab-override-prop / prefab-remove-inherited-{object,component}");
    return 1;
  }

  int cmdPrefabRemovePatch(const CLI::Commands::Args &/*a*/, Project::Project &/*project*/)
  {
    emitErr("prefab-remove-patch is removed — use prefab-reset-prop");
    return 1;
  }

  // Helper: parse "name|id" into a component slot uuid on an inherited
  // object. Returns 0 (= Object-level transform) when comp string is empty.
  uint64_t resolveCompUuidOnObject(Project::Object *target, const std::string &comp)
  {
    if (comp.empty()) return 0;
    auto *entry = findComponent(*target, comp);
    return entry ? entry->uuid : 0;
  }

  int cmdPrefabOverrideProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field, --value are required (use --comp to target a component, --path to choose an object)");
      return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    if (!e->prefab->isVariant()) {
      emitErr("prefab-override-prop only valid on variant (child) prefabs; use prefab-set-prop / prefab-set-transform on standalone prefabs");
      return 1;
    }
    auto *target = findObjectByPath(&e->prefab->obj, a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    uint64_t compUuid = a.comp.empty() ? 0 : resolveCompUuidOnObject(target, a.comp);
    if (!a.comp.empty() && compUuid == 0) {
      emitErr("component not present on object: " + a.comp);
      return 1;
    }
    auto v = parseValueJSON(a.value);

    // Apply directly to the resolved tree first (so users see immediate effect)
    // — savePrefabAt will rebuild structured overrides from the diff.
    if (compUuid == 0) {
      // Transform field on the Object itself.
      if (a.field == "pos" || a.field == "scale") {
        if (!v.is_array() || v.size() != 3) {
          emitErr("--value must be [x,y,z] for pos/scale"); return 1;
        }
        glm::vec3 vec{ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
        if (a.field == "pos") target->pos.value = vec; else target->scale.value = vec;
      } else if (a.field == "rot") {
        if (!v.is_array() || v.size() != 4) {
          emitErr("--value must be [x,y,z,w] for rot"); return 1;
        }
        target->rot.value = {v[0].get<float>(), v[1].get<float>(),
                             v[2].get<float>(), v[3].get<float>()};
      } else {
        emitErr("--field must be pos/rot/scale when --comp omitted");
        return 1;
      }
    } else {
      Project::Component::Entry *entry = findComponent(*target, a.comp);
      if (!entry) { emitErr("component vanished: " + a.comp); return 1; }
      auto &def = Project::Component::TABLE[entry->id];
      auto data = def.funcSerialize(*entry);
      if (!data.is_object()) {
        emitErr("component has no editable fields"); return 1;
      }
      data[a.field] = v;
      entry->data = def.funcDeserialize(data);
    }
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["overrode"] = a.field;
    out["objUuid"]  = target->uuid;
    if (compUuid) out["compUuid"] = compUuid;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabResetProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty()) {
      emitErr("--asset and --field are required (--comp + --path to target a component)"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    if (!e->prefab->isVariant()) {
      emitErr("prefab-reset-prop only valid on variant prefabs"); return 1;
    }
    auto parent = project.getAssets().getPrefabByUUID(e->prefab->uuidParentPrefab.value);
    if (!parent) { emitErr("parent prefab missing"); return 1; }

    auto *target = findObjectByPath(&e->prefab->obj, a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    // Look up the parent's same-uuid object to copy back the inherited value.
    std::function<Project::Object*(Project::Object&, uint32_t)> findByUuid =
      [&](Project::Object &o, uint32_t u) -> Project::Object* {
        if (o.uuid == u) return &o;
        for (auto &c : o.children) if (auto *r = findByUuid(*c, u)) return r;
        return nullptr;
      };
    auto *parentObj = findByUuid(parent->obj, target->uuid);
    if (!parentObj) { emitErr("inherited object not in parent — cannot reset"); return 1; }

    if (a.comp.empty()) {
      if (a.field == "pos")   target->pos.value   = parentObj->pos.value;
      else if (a.field == "rot")   target->rot.value   = parentObj->rot.value;
      else if (a.field == "scale") target->scale.value = parentObj->scale.value;
      else { emitErr("--field must be pos/rot/scale when --comp omitted"); return 1; }
    } else {
      auto *entry = findComponent(*target, a.comp);
      if (!entry) { emitErr("component not on object"); return 1; }
      const Project::Component::Entry *pe = nullptr;
      for (auto &p : parentObj->components) {
        if (p.uuid == entry->uuid) { pe = &p; break; }
      }
      if (!pe) { emitErr("matching component not in parent — child added it; remove it instead"); return 1; }
      auto &def = Project::Component::TABLE[entry->id];
      auto cd = def.funcSerialize(*entry);
      auto pd = def.funcSerialize(*pe);
      if (!cd.is_object() || !pd.is_object() || !pd.contains(a.field)) {
        emitErr("field not present on parent component"); return 1;
      }
      cd[a.field] = pd[a.field];
      entry->data = def.funcDeserialize(cd);
    }
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["resetField"] = a.field;
    out["objUuid"] = target->uuid;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabOverrideVarDefault(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty() || a.value.empty()) {
      emitErr("--asset, --name (variable), --value are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    if (!e->prefab->isVariant()) {
      emitErr("prefab-override-var-default only valid on variant prefabs"); return 1;
    }
    Project::PrefabVarDef *def = nullptr;
    for (auto &v : e->prefab->variables) if (v.name == a.name) { def = &v; break; }
    if (!def) { emitErr("variable not found: " + a.name); return 1; }
    auto v = parseValueJSON(a.value);
    def->defaultValue.deserialize(v.is_string() ? v.get<std::string>() : v.dump());
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["overrodeVar"] = a.name;
    out["uuid"] = def->uuid;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabResetVarDefault(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty()) {
      emitErr("--asset and --name are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    if (!e->prefab->isVariant()) {
      emitErr("prefab-reset-var-default only valid on variant prefabs"); return 1;
    }
    auto parent = project.getAssets().getPrefabByUUID(e->prefab->uuidParentPrefab.value);
    if (!parent) { emitErr("parent prefab missing"); return 1; }
    Project::PrefabVarDef *child = nullptr;
    for (auto &v : e->prefab->variables) if (v.name == a.name) { child = &v; break; }
    if (!child) { emitErr("variable not found: " + a.name); return 1; }
    const Project::PrefabVarDef *par = nullptr;
    for (auto &v : parent->variables) if (v.uuid == child->uuid) { par = &v; break; }
    if (!par) { emitErr("variable was added by child — remove it instead"); return 1; }
    child->defaultValue = par->defaultValue;
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["resetVar"] = a.name;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabRemoveInheritedObject(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.path.empty()) {
      emitErr("--asset and --path are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    if (!e->prefab->isVariant()) {
      emitErr("only valid on variant prefabs; use prefab-remove-object on standalone");
      return 1;
    }
    Project::Object *parentObj = nullptr;
    size_t idx = 0;
    auto *target = findObjectByPathWithParent(&e->prefab->obj, a.path, &parentObj, &idx);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    if (!parentObj) { emitErr("cannot remove the root object"); return 1; }
    // Drop from the resolved tree; rebuildOverridesFromCurrent on save will
    // record this as a removedObjects entry against the parent prefab.
    parentObj->children.erase(parentObj->children.begin() + idx);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["removedPath"] = a.path;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabRemoveInheritedComponent(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty()) {
      emitErr("--asset and --comp are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    if (!e->prefab->isVariant()) {
      emitErr("only valid on variant prefabs; use prefab-remove-component on standalone");
      return 1;
    }
    auto *target = findObjectByPath(&e->prefab->obj, a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    auto *entry = findComponent(*target, a.comp);
    if (!entry) { emitErr("component not present: " + a.comp); return 1; }
    target->removeComponent(entry->uuid);
    savePrefabAt(e->path, *e->prefab);
    project.getAssets().reload();
    nlohmann::json out;
    out["removedComp"] = a.comp;
    emitJSON(out);
    return 0;
  }

  int cmdPrefabDescribeInheritance(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab not found: " + a.asset); return 1; }
    nlohmann::json out;
    out["uuid"] = e->prefab->uuid.value;
    out["name"] = e->name;
    out["isVariant"] = e->prefab->isVariant();
    if (!e->prefab->isVariant()) { emitJSON(out); return 0; }
    out["uuidParentPrefab"] = e->prefab->uuidParentPrefab.value;
    if (auto *parent = project.getAssets().getEntryByUUID(e->prefab->uuidParentPrefab.value)) {
      out["parentName"] = parent->name;
      out["parentPath"] = parent->path;
    }

    // Rebuild structured overrides on-the-fly so the output reflects the
    // current resolved tree (in case the caller mutated obj and hasn't
    // saved yet).
    if (auto parent = project.getAssets().getPrefabByUUID(e->prefab->uuidParentPrefab.value)) {
      e->prefab->rebuildOverridesFromCurrent(*parent);
    }
    out["propOverrideCount"]   = (int)e->prefab->propOverrides.size();
    out["addedComponentCount"] = (int)e->prefab->addedComponents.size();
    out["addedObjectCount"]    = (int)e->prefab->addedObjects.size();
    out["removedObjectCount"]  = (int)e->prefab->removedObjects.size();
    out["removedCompCount"]    = (int)e->prefab->removedComponents.size();
    out["varDefaultOverrideCount"] = (int)e->prefab->varDefaultOverrides.size();
    out["addedVariableCount"]  = (int)e->prefab->addedVariables.size();
    out["removedVariableCount"]= (int)e->prefab->removedVariables.size();

    auto propJ = nlohmann::json::array();
    for (auto &o : e->prefab->propOverrides) {
      propJ.push_back({
        {"obj",  o.objUuid},
        {"comp", o.compUuid},
        {"pid",  o.propId},
      });
    }
    out["propOverrides"] = propJ;

    auto removedObjJ = nlohmann::json::array();
    for (auto u : e->prefab->removedObjects) removedObjJ.push_back(u);
    out["removedObjects"] = removedObjJ;

    auto removedCompJ = nlohmann::json::array();
    for (auto u : e->prefab->removedComponents) removedCompJ.push_back(u);
    out["removedComponents"] = removedCompJ;

    emitJSON(out);
    return 0;
  }

  // ── Scene component duplicate (mirrors Object Inspector "Duplicate") ──

  int cmdSceneDuplicateComponent(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.comp.empty()) {
      emitErr("--asset, --comp are required"); return 1;
    }
    int sceneId = resolveSceneId(project, a.asset);
    if (sceneId < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, sceneId);
    auto *target = findObjectByPath(&scene->getRootObject(), a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    auto *src = findComponent(*target, a.comp);
    if (!src) { emitErr("component not present: " + a.comp); return 1; }
    const auto &info = Project::Component::TABLE[src->id];
    auto data = info.funcSerialize(*src);
    size_t before = target->components.size();
    target->addComponent(src->id);
    if (target->components.size() == before) {
      emitErr("addComponent refused (singleton type already present)"); return 1;
    }
    auto &fresh = target->components.back();
    fresh.data = info.funcDeserialize(data);
    saveScene(project, *scene);
    nlohmann::json out;
    out["duplicated"] = info.name;
    out["sourceUUID"] = src->uuid;
    out["newUUID"] = fresh.uuid;
    emitJSON(out);
    return 0;
  }

  // ── Scene extract-to-prefab (mirrors Scene Graph "To Prefab") ──────

  int cmdSceneExtractPrefab(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.path.empty()) {
      emitErr("--asset (scene) and --path (object path) are required"); return 1;
    }
    int id = resolveSceneId(project, a.asset);
    if (id < 0) { emitErr("scene not found: " + a.asset); return 1; }
    auto scene = openScene(project, id);
    auto *target = findObjectByPath(&scene->getRootObject(), a.path);
    if (!target) { emitErr("path not found: " + a.path); return 1; }
    if (target->isPrefabInstance()) {
      emitErr("object is already a prefab instance"); return 1;
    }
    if (!target->parent) {
      emitErr("cannot extract scene root; pick a child path"); return 1;
    }
    uint32_t targetUUID = target->uuid;
    std::string srcName = target->name;
    scene->createPrefabFromObject(targetUUID);
    saveScene(project, *scene);
    // Look up the just-created prefab by the sanitised filename
    // (mirrors Scene::createPrefabFromObject's sanitiser).
    std::string sanitized = srcName;
    sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(),
      [](char c){ return !std::isalnum((unsigned char)c) && c != '_'; }),
      sanitized.end());
    nlohmann::json out;
    out["extracted"] = sanitized.empty() ? srcName : sanitized;
    out["fromPath"] = a.path;
    out["sceneId"] = id;
    auto *fresh = project.getAssets().getByName(sanitized + ".prefab");
    if (fresh) out["asset"] = serializeAssetEntry(*fresh, false);
    emitJSON(out);
    return 0;
  }

  // ── Standalone NodeGraph (.p64graph) node-level ops ────────────────
  //
  // .p64graph is a flat JSON file: {"nodes":[{uuid,type,pos,...}], "links":[{src,srcPort,dst,dstPort}]}.
  // We patch it directly; Project::Graph::Graph::getNodeNames() supplies
  // the type-name <-> index mapping but we never instantiate ImFlow here
  // (no GPU / ImGui context). Newly-added nodes carry only base fields;
  // type-specific defaults are filled in by each node's deserialize() the
  // next time the editor opens the graph (the standard `j.value(key, dflt)`
  // pattern means missing keys round-trip to the C++ default).

  Project::AssetManagerEntry* resolveGraphAsset(Project::Project &project, const std::string &key)
  {
    return resolveAsset(project, key, Project::FileType::NODE_GRAPH);
  }

  bool loadGraphJSON(const std::string &path, nlohmann::json &doc, std::string &err)
  {
    auto raw = Utils::FS::loadTextFile(path);
    if (raw.empty()) { err = "empty graph file: " + path; return false; }
    try { doc = nlohmann::json::parse(raw); }
    catch (const std::exception &e) { err = std::string("parse failed: ") + e.what(); return false; }
    if (!doc.is_object()) { err = "graph root must be an object"; return false; }
    if (!doc.contains("nodes") || !doc["nodes"].is_array()) doc["nodes"] = nlohmann::json::array();
    if (!doc.contains("links") || !doc["links"].is_array()) doc["links"] = nlohmann::json::array();
    return true;
  }

  bool saveGraphJSON(const std::string &path, const nlohmann::json &doc)
  {
    return Utils::FS::saveTextFile(path, doc.dump(2));
  }

  int graphTypeFromArg(const std::string &arg)
  {
    if (arg.empty()) return -1;
    if (auto u = tryParseUUID(arg)) return (int)*u;
    const auto &names = Project::Graph::Graph::getNodeNames();
    // Persisted names carry an MDI icon glyph + space + human label
    // (e.g. " Start"). Match the trailing label so CLI callers don't
    // have to embed the UTF-8 icon. Falls back to exact match first.
    auto stripIcon = [](const std::string &s) -> std::string {
      auto sp = s.find(' ');
      return (sp == std::string::npos) ? s : s.substr(sp + 1);
    };
    for (size_t i = 0; i < names.size(); ++i) {
      if (names[i] == arg) return (int)i;
    }
    for (size_t i = 0; i < names.size(); ++i) {
      if (stripIcon(names[i]) == arg) return (int)i;
    }
    return -1;
  }

  // --from / --to format: "<nodeUUID>:<pinIdx>".
  bool parsePinSpec(const std::string &s, uint64_t &nodeUUID, uint32_t &pinIdx, std::string &err)
  {
    auto colon = s.find(':');
    if (colon == std::string::npos) { err = "expected <nodeUUID>:<pinIdx>"; return false; }
    auto nodePart = s.substr(0, colon);
    auto pinPart  = s.substr(colon + 1);
    auto u = tryParseUUID(nodePart);
    if (!u) { err = "invalid node uuid: " + nodePart; return false; }
    nodeUUID = *u;
    try { pinIdx = (uint32_t)std::stoul(pinPart); }
    catch (...) { err = "invalid pin index: " + pinPart; return false; }
    return true;
  }

  int cmdGraphListNodes(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolveGraphAsset(project, a.asset);
    if (!e) { emitErr("graph not found: " + a.asset); return 1; }
    nlohmann::json doc;
    std::string err;
    if (!loadGraphJSON(e->path, doc, err)) { emitErr(err); return 1; }
    const auto &names = Project::Graph::Graph::getNodeNames();
    nlohmann::json outNodes = nlohmann::json::array();
    for (const auto &n : doc["nodes"]) {
      nlohmann::json row;
      row["uuid"] = n.value("uuid", 0ull);
      uint32_t t = n.value("type", 0u);
      row["type"] = t;
      row["typeName"] = (t < names.size()) ? names[t] : "<unknown>";
      if (n.contains("pos")) row["pos"] = n["pos"];
      outNodes.push_back(row);
    }
    nlohmann::json out;
    out["asset"] = e->name;
    out["nodes"] = outNodes;
    out["links"] = doc["links"];
    emitJSON(out);
    return 0;
  }

  int cmdGraphAddNode(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.type.empty()) {
      emitErr("--asset and --type (node type name or index) are required"); return 1;
    }
    auto *e = resolveGraphAsset(project, a.asset);
    if (!e) { emitErr("graph not found: " + a.asset); return 1; }
    int typeId = graphTypeFromArg(a.type);
    const auto &names = Project::Graph::Graph::getNodeNames();
    if (typeId < 0 || typeId >= (int)names.size()) {
      emitErr("unknown node type: " + a.type); return 1;
    }
    nlohmann::json doc;
    std::string err;
    if (!loadGraphJSON(e->path, doc, err)) { emitErr(err); return 1; }
    nlohmann::json node;
    node["uuid"] = Utils::Hash::randomU64();
    node["type"] = typeId;
    // --value (optional) is a JSON array [x,y] for the on-canvas position.
    if (!a.value.empty()) {
      auto pos = parseValueJSON(a.value);
      if (!pos.is_array() || pos.size() != 2) { emitErr("--value must be [x,y]"); return 1; }
      node["pos"] = pos;
    } else {
      node["pos"] = {0.0f, 0.0f};
    }
    doc["nodes"].push_back(node);
    if (!saveGraphJSON(e->path, doc)) { emitErr("save failed"); return 1; }
    project.getAssets().reload();
    nlohmann::json out;
    out["added"] = node["uuid"];
    out["typeName"] = names[typeId];
    out["asset"] = e->name;
    emitJSON(out);
    return 0;
  }

  int cmdGraphRemoveNode(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.value.empty()) {
      emitErr("--asset and --value (node uuid) are required"); return 1;
    }
    auto *e = resolveGraphAsset(project, a.asset);
    if (!e) { emitErr("graph not found: " + a.asset); return 1; }
    auto u = tryParseUUID(a.value);
    if (!u) { emitErr("--value must be a uuid"); return 1; }
    uint64_t target = *u;
    nlohmann::json doc;
    std::string err;
    if (!loadGraphJSON(e->path, doc, err)) { emitErr(err); return 1; }
    auto &nodes = doc["nodes"];
    size_t before = nodes.size();
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
      [&](const nlohmann::json &n){ return n.value("uuid", 0ull) == target; }), nodes.end());
    if (nodes.size() == before) { emitErr("node not found: " + a.value); return 1; }
    // Cascade-delete any link referencing the removed node.
    auto &links = doc["links"];
    size_t linksBefore = links.size();
    links.erase(std::remove_if(links.begin(), links.end(),
      [&](const nlohmann::json &l){
        return l.value("src", 0ull) == target || l.value("dst", 0ull) == target;
      }), links.end());
    if (!saveGraphJSON(e->path, doc)) { emitErr("save failed"); return 1; }
    project.getAssets().reload();
    nlohmann::json out;
    out["removed"] = target;
    out["removedLinks"] = (int)(linksBefore - links.size());
    emitJSON(out);
    return 0;
  }

  int cmdGraphConnect(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from <nodeUUID>:<pinIdx>, --to <nodeUUID>:<pinIdx> are required"); return 1;
    }
    auto *e = resolveGraphAsset(project, a.asset);
    if (!e) { emitErr("graph not found: " + a.asset); return 1; }
    uint64_t srcUUID = 0, dstUUID = 0;
    uint32_t srcPin = 0, dstPin = 0;
    std::string err;
    if (!parsePinSpec(a.from, srcUUID, srcPin, err)) { emitErr("--from: " + err); return 1; }
    if (!parsePinSpec(a.to,   dstUUID, dstPin, err)) { emitErr("--to: "   + err); return 1; }
    nlohmann::json doc;
    if (!loadGraphJSON(e->path, doc, err)) { emitErr(err); return 1; }
    auto nodeExists = [&](uint64_t uuid){
      for (const auto &n : doc["nodes"]) if (n.value("uuid", 0ull) == uuid) return true;
      return false;
    };
    if (!nodeExists(srcUUID)) { emitErr("src node not in graph"); return 1; }
    if (!nodeExists(dstUUID)) { emitErr("dst node not in graph"); return 1; }
    nlohmann::json link;
    link["src"] = srcUUID;
    link["srcPort"] = srcPin;
    link["dst"] = dstUUID;
    link["dstPort"] = dstPin;
    doc["links"].push_back(link);
    if (!saveGraphJSON(e->path, doc)) { emitErr("save failed"); return 1; }
    project.getAssets().reload();
    nlohmann::json out;
    out["connected"] = link;
    out["index"] = (int)doc["links"].size() - 1;
    emitJSON(out);
    return 0;
  }

  int cmdGraphDisconnect(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from <nodeUUID>:<pinIdx>, --to <nodeUUID>:<pinIdx> are required"); return 1;
    }
    auto *e = resolveGraphAsset(project, a.asset);
    if (!e) { emitErr("graph not found: " + a.asset); return 1; }
    uint64_t srcUUID = 0, dstUUID = 0;
    uint32_t srcPin = 0, dstPin = 0;
    std::string err;
    if (!parsePinSpec(a.from, srcUUID, srcPin, err)) { emitErr("--from: " + err); return 1; }
    if (!parsePinSpec(a.to,   dstUUID, dstPin, err)) { emitErr("--to: "   + err); return 1; }
    nlohmann::json doc;
    if (!loadGraphJSON(e->path, doc, err)) { emitErr(err); return 1; }
    auto &links = doc["links"];
    size_t before = links.size();
    links.erase(std::remove_if(links.begin(), links.end(),
      [&](const nlohmann::json &l){
        return l.value("src", 0ull) == srcUUID && l.value("dst", 0ull) == dstUUID
            && l.value("srcPort", 0u) == srcPin && l.value("dstPort", 0u) == dstPin;
      }), links.end());
    if (links.size() == before) { emitErr("matching link not found"); return 1; }
    if (!saveGraphJSON(e->path, doc)) { emitErr("save failed"); return 1; }
    project.getAssets().reload();
    nlohmann::json out;
    out["disconnected"] = (int)(before - links.size());
    emitJSON(out);
    return 0;
  }

  // ── Generic graph-JSON mutators ────────────────────────────────────
  //
  // Same on-disk shape ({nodes,links}) drives the standalone .p64graph
  // asset, the prefab event-graph (lives inside .prefab as eventGraph),
  // and the material-graph (lives inside .p64mat as graph). These
  // helpers operate on a parsed JSON document so the per-asset wrappers
  // only have to do load/save + reload.

  bool gjEnsureShape(nlohmann::json &doc)
  {
    if (!doc.is_object()) return false;
    if (!doc.contains("nodes") || !doc["nodes"].is_array()) doc["nodes"] = nlohmann::json::array();
    if (!doc.contains("links") || !doc["links"].is_array()) doc["links"] = nlohmann::json::array();
    return true;
  }

  nlohmann::json gjListNodes(const nlohmann::json &doc, const std::vector<std::string> &names)
  {
    nlohmann::json outNodes = nlohmann::json::array();
    if (!doc.contains("nodes") || !doc["nodes"].is_array()) return outNodes;
    for (const auto &n : doc["nodes"]) {
      nlohmann::json row;
      row["uuid"] = n.value("uuid", 0ull);
      uint32_t t = n.value("type", 0u);
      row["type"] = t;
      row["typeName"] = (t < names.size()) ? names[t] : "<unknown>";
      if (n.contains("pos")) row["pos"] = n["pos"];
      outNodes.push_back(row);
    }
    return outNodes;
  }

  bool gjAddNode(nlohmann::json &doc, int typeId, const std::string &posJson, uint64_t &outUUID, std::string &err)
  {
    if (!gjEnsureShape(doc)) { err = "graph JSON shape invalid"; return false; }
    nlohmann::json node;
    node["uuid"] = Utils::Hash::randomU64();
    node["type"] = typeId;
    if (!posJson.empty()) {
      auto pos = parseValueJSON(posJson);
      if (!pos.is_array() || pos.size() != 2) { err = "--value must be [x,y]"; return false; }
      node["pos"] = pos;
    } else {
      node["pos"] = {0.0f, 0.0f};
    }
    outUUID = node["uuid"];
    doc["nodes"].push_back(node);
    return true;
  }

  bool gjRemoveNode(nlohmann::json &doc, uint64_t target, int &removedLinks, std::string &err)
  {
    if (!gjEnsureShape(doc)) { err = "graph JSON shape invalid"; return false; }
    auto &nodes = doc["nodes"];
    size_t before = nodes.size();
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
      [&](const nlohmann::json &n){ return n.value("uuid", 0ull) == target; }), nodes.end());
    if (nodes.size() == before) { err = "node not found"; return false; }
    auto &links = doc["links"];
    size_t lbefore = links.size();
    links.erase(std::remove_if(links.begin(), links.end(),
      [&](const nlohmann::json &l){
        return l.value("src", 0ull) == target || l.value("dst", 0ull) == target;
      }), links.end());
    removedLinks = (int)(lbefore - links.size());
    return true;
  }

  bool gjConnect(nlohmann::json &doc, uint64_t srcUUID, uint32_t srcPin,
                 uint64_t dstUUID, uint32_t dstPin, std::string &err)
  {
    if (!gjEnsureShape(doc)) { err = "graph JSON shape invalid"; return false; }
    auto nodeExists = [&](uint64_t uuid){
      for (const auto &n : doc["nodes"]) if (n.value("uuid", 0ull) == uuid) return true;
      return false;
    };
    if (!nodeExists(srcUUID)) { err = "src node not in graph"; return false; }
    if (!nodeExists(dstUUID)) { err = "dst node not in graph"; return false; }
    nlohmann::json link;
    link["src"] = srcUUID;
    link["srcPort"] = srcPin;
    link["dst"] = dstUUID;
    link["dstPort"] = dstPin;
    doc["links"].push_back(link);
    return true;
  }

  int gjDisconnect(nlohmann::json &doc, uint64_t srcUUID, uint32_t srcPin,
                   uint64_t dstUUID, uint32_t dstPin)
  {
    if (!gjEnsureShape(doc)) return 0;
    auto &links = doc["links"];
    size_t before = links.size();
    links.erase(std::remove_if(links.begin(), links.end(),
      [&](const nlohmann::json &l){
        return l.value("src", 0ull) == srcUUID && l.value("dst", 0ull) == dstUUID
            && l.value("srcPort", 0u) == srcPin && l.value("dstPort", 0u) == dstPin;
      }), links.end());
    return (int)(before - links.size());
  }

  // Find a node by uuid. Returns nullptr if absent.
  nlohmann::json* gjFindNode(nlohmann::json &doc, uint64_t target)
  {
    if (!doc.contains("nodes") || !doc["nodes"].is_array()) return nullptr;
    for (auto &n : doc["nodes"]) {
      if (n.value("uuid", 0ull) == target) return &n;
    }
    return nullptr;
  }

  bool gjSetNodeProp(nlohmann::json &doc, uint64_t target, const std::string &field,
                     const nlohmann::json &value, nlohmann::json &outNode, std::string &err)
  {
    if (!gjEnsureShape(doc)) { err = "graph JSON shape invalid"; return false; }
    if (field == "uuid" || field == "type" || field == "pos") {
      err = "refusing to set structural field '" + field + "' via set-node-prop; use the dedicated command";
      return false;
    }
    auto *node = gjFindNode(doc, target);
    if (!node) { err = "node not found"; return false; }
    (*node)[field] = value;
    outNode = *node;
    return true;
  }

  bool gjDuplicateNode(nlohmann::json &doc, uint64_t target, const std::string &posJson,
                       uint64_t &newUUID, std::string &err)
  {
    if (!gjEnsureShape(doc)) { err = "graph JSON shape invalid"; return false; }
    auto *src = gjFindNode(doc, target);
    if (!src) { err = "node not found"; return false; }
    nlohmann::json copy = *src;
    newUUID = Utils::Hash::randomU64();
    copy["uuid"] = newUUID;
    if (!posJson.empty()) {
      auto pos = parseValueJSON(posJson);
      if (!pos.is_array() || pos.size() != 2) { err = "--value must be [x,y]"; return false; }
      copy["pos"] = pos;
    } else if (copy.contains("pos") && copy["pos"].is_array() && copy["pos"].size() == 2) {
      // Nudge so the clone is visible next to the source instead of on top.
      copy["pos"][0] = copy["pos"][0].get<float>() + 24.0f;
      copy["pos"][1] = copy["pos"][1].get<float>() + 24.0f;
    } else {
      copy["pos"] = {24.0f, 24.0f};
    }
    doc["nodes"].push_back(copy);
    return true;
  }

  bool gjSetNodePos(nlohmann::json &doc, uint64_t target, const std::string &posJson,
                    nlohmann::json &outNode, std::string &err)
  {
    if (!gjEnsureShape(doc)) { err = "graph JSON shape invalid"; return false; }
    if (posJson.empty()) { err = "--value (pos as [x,y]) is required"; return false; }
    auto pos = parseValueJSON(posJson);
    if (!pos.is_array() || pos.size() != 2) { err = "--value must be [x,y]"; return false; }
    auto *node = gjFindNode(doc, target);
    if (!node) { err = "node not found"; return false; }
    (*node)["pos"] = pos;
    outNode = *node;
    return true;
  }

  // Structural validation of a saved graph (mirrors the JSON-walkable
  // subset of Project::Graph::Graph::validate; pin-style reachability
  // would need ImFlow, so we report only what the JSON can prove).
  // entryTypes = the type IDs counted as a valid entry node (e.g. Start
  // for the standalone graph; Start + PrefabEvent for prefab event graph).
  nlohmann::json gjValidate(const nlohmann::json &doc,
                            const std::vector<std::string> &names,
                            const std::vector<uint32_t> &entryTypes,
                            uint32_t startType)
  {
    nlohmann::json out;
    nlohmann::json diags = nlohmann::json::array();
    auto pushDiag = [&](const std::string &sev, const std::string &msg, uint64_t nodeUUID = 0){
      nlohmann::json d;
      d["severity"] = sev;
      d["message"] = msg;
      if (nodeUUID) d["nodeUUID"] = nodeUUID;
      diags.push_back(d);
    };

    int nodeCount = (int)(doc.contains("nodes") && doc["nodes"].is_array() ? doc["nodes"].size() : 0);
    int linkCount = (int)(doc.contains("links") && doc["links"].is_array() ? doc["links"].size() : 0);
    out["nodes"] = nodeCount;
    out["links"] = linkCount;

    if (nodeCount == 0) {
      pushDiag("info", "Graph is empty.");
      out["diagnostics"] = diags;
      out["ok"] = true;
      return out;
    }

    bool hasEntry = false;
    int startCount = 0;
    uint64_t firstExtraStart = 0;
    std::unordered_set<uint64_t> nodeUUIDs;
    for (const auto &n : doc["nodes"]) {
      uint64_t uu = n.value("uuid", 0ull);
      nodeUUIDs.insert(uu);
      uint32_t t = n.value("type", UINT32_MAX);
      if (t >= names.size()) {
        pushDiag("error", "Unknown node type " + std::to_string(t), uu);
        continue;
      }
      for (uint32_t et : entryTypes) {
        if (t == et) { hasEntry = true; break; }
      }
      if (t == startType) {
        ++startCount;
        if (startCount == 2) firstExtraStart = uu;
      }
    }
    if (!hasEntry) pushDiag("error", "Graph has no entry node.");
    if (startCount > 1) {
      pushDiag("warning",
        "Graph has " + std::to_string(startCount) + " Start nodes; only the first is reachable.",
        firstExtraStart);
    }
    // Link sanity: every src/dst should reference an existing node.
    if (doc.contains("links") && doc["links"].is_array()) {
      int idx = 0;
      for (const auto &l : doc["links"]) {
        uint64_t s = l.value("src", 0ull);
        uint64_t d = l.value("dst", 0ull);
        if (!nodeUUIDs.count(s)) pushDiag("error", "Link " + std::to_string(idx) + ": src node missing");
        if (!nodeUUIDs.count(d)) pushDiag("error", "Link " + std::to_string(idx) + ": dst node missing");
        ++idx;
      }
    }

    out["diagnostics"] = diags;
    // ok flag is true only when there are zero "error" diagnostics; warnings
    // / info do not flip it.
    bool ok = true;
    for (const auto &d : diags) {
      if (d.value("severity", "") == "error") { ok = false; break; }
    }
    out["ok"] = ok;
    return out;
  }

  // Same suffix-matching as graphTypeFromArg but parameterised on the
  // name table — material-graph and event-graph reuse this with their
  // respective NODE_TABLEs.
  int graphTypeFromArgGeneric(const std::string &arg, const std::vector<std::string> &names)
  {
    if (arg.empty()) return -1;
    if (auto u = tryParseUUID(arg)) return (int)*u;
    auto stripIcon = [](const std::string &s) -> std::string {
      auto sp = s.find(' ');
      return (sp == std::string::npos) ? s : s.substr(sp + 1);
    };
    for (size_t i = 0; i < names.size(); ++i) if (names[i] == arg) return (int)i;
    for (size_t i = 0; i < names.size(); ++i) if (stripIcon(names[i]) == arg) return (int)i;
    return -1;
  }

  // ── Prefab event-graph node-level ops ──────────────────────────────
  //
  // The event graph is stored as a JSON string on the Prefab object
  // (eventGraphJSON) and round-trips through Prefab::serialize. Same
  // node-table as the standalone NodeGraph, since the editor instantiates
  // a Project::Graph::Graph for both.

  bool loadPrefabEventGraph(Project::AssetManagerEntry &e, nlohmann::json &doc, std::string &err)
  {
    if (!e.prefab) { err = "prefab missing"; return false; }
    if (e.prefab->eventGraphJSON.empty()) {
      doc = nlohmann::json::object({{"nodes", nlohmann::json::array()},
                                    {"links", nlohmann::json::array()}});
      return true;
    }
    doc = nlohmann::json::parse(e.prefab->eventGraphJSON, nullptr, false);
    if (!doc.is_object()) { err = "eventGraphJSON not an object"; return false; }
    return true;
  }

  void saveEventGraphInto(Project::AssetManagerEntry &e, const nlohmann::json &doc)
  {
    e.prefab->eventGraphJSON = doc.dump();
    savePrefabAt(e.path, *e.prefab);
  }

  int cmdEventGraphListNodes(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    nlohmann::json out;
    out["asset"] = e->name;
    out["nodes"] = gjListNodes(doc, Project::Graph::Graph::getNodeNames());
    out["links"] = doc.value("links", nlohmann::json::array());
    emitJSON(out);
    return 0;
  }

  int cmdEventGraphAddNode(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.type.empty()) {
      emitErr("--asset and --type (node name or index) are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    const auto &names = Project::Graph::Graph::getNodeNames();
    int typeId = graphTypeFromArgGeneric(a.type, names);
    if (typeId < 0 || typeId >= (int)names.size()) { emitErr("unknown node type: " + a.type); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    uint64_t newUUID = 0;
    if (!gjAddNode(doc, typeId, a.value, newUUID, err)) { emitErr(err); return 1; }
    saveEventGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["added"] = newUUID;
    out["typeName"] = names[typeId];
    out["asset"] = e->name;
    emitJSON(out);
    return 0;
  }

  int cmdEventGraphRemoveNode(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.value.empty()) {
      emitErr("--asset and --value (node uuid) are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    auto u = tryParseUUID(a.value);
    if (!u) { emitErr("--value must be a uuid"); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    int removedLinks = 0;
    if (!gjRemoveNode(doc, *u, removedLinks, err)) { emitErr(err); return 1; }
    saveEventGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["removed"] = *u;
    out["removedLinks"] = removedLinks;
    emitJSON(out);
    return 0;
  }

  int cmdEventGraphConnect(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from <nodeUUID>:<pinIdx>, --to <nodeUUID>:<pinIdx> are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    uint64_t srcUUID = 0, dstUUID = 0; uint32_t srcPin = 0, dstPin = 0;
    std::string err;
    if (!parsePinSpec(a.from, srcUUID, srcPin, err)) { emitErr("--from: " + err); return 1; }
    if (!parsePinSpec(a.to,   dstUUID, dstPin, err)) { emitErr("--to: "   + err); return 1; }
    nlohmann::json doc;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    if (!gjConnect(doc, srcUUID, srcPin, dstUUID, dstPin, err)) { emitErr(err); return 1; }
    saveEventGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["connected"] = {{"src", srcUUID}, {"srcPort", srcPin}, {"dst", dstUUID}, {"dstPort", dstPin}};
    out["index"] = (int)doc["links"].size() - 1;
    emitJSON(out);
    return 0;
  }

  int cmdEventGraphDisconnect(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from <nodeUUID>:<pinIdx>, --to <nodeUUID>:<pinIdx> are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    uint64_t srcUUID = 0, dstUUID = 0; uint32_t srcPin = 0, dstPin = 0;
    std::string err;
    if (!parsePinSpec(a.from, srcUUID, srcPin, err)) { emitErr("--from: " + err); return 1; }
    if (!parsePinSpec(a.to,   dstUUID, dstPin, err)) { emitErr("--to: "   + err); return 1; }
    nlohmann::json doc;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    int removed = gjDisconnect(doc, srcUUID, srcPin, dstUUID, dstPin);
    if (removed == 0) { emitErr("matching link not found"); return 1; }
    saveEventGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["disconnected"] = removed;
    emitJSON(out);
    return 0;
  }

  // ── Material-graph node-level ops ──────────────────────────────────
  //
  // Graph JSON lives at MaterialAsset::graphJSON (wrapped under "graph"
  // in the saved .p64mat). Distinct NODE_TABLE from the standalone
  // NodeGraph — MaterialGraph::Graph::getNodeNames() supplies it.
  //
  // Note: edits here do NOT trigger a recompile of MaterialAsset::compiled.
  // The editor recompiles on save; runtime/compiled material is only
  // refreshed once the graph is re-opened and Compile is pressed.

  Project::AssetManagerEntry* resolveMaterialAsset(Project::Project &project, const std::string &key)
  {
    return resolveAsset(project, key, Project::FileType::MATERIAL);
  }

  bool loadMaterialGraph(Project::AssetManagerEntry &e, nlohmann::json &doc, std::string &err)
  {
    if (!e.materialAsset) { err = "material asset missing"; return false; }
    if (e.materialAsset->graphJSON.empty()) {
      doc = nlohmann::json::object({{"nodes", nlohmann::json::array()},
                                    {"links", nlohmann::json::array()}});
      return true;
    }
    doc = nlohmann::json::parse(e.materialAsset->graphJSON, nullptr, false);
    if (!doc.is_object()) { err = "material graphJSON not an object"; return false; }
    return true;
  }

  void saveMaterialGraphInto(Project::AssetManagerEntry &e, const nlohmann::json &doc)
  {
    e.materialAsset->graphJSON = doc.dump();
    // Refresh the compiled cache so downstream consumers (model reload,
    // --cmd build, asset previews) see the same compiled material the
    // GUI Compile button would produce. ImNodeFlow is data-only here —
    // no ImGui context required (same pattern as nodeGraphBuilder.cpp).
    Project::MaterialGraph::Graph live{};
    if (live.deserialize(e.materialAsset->graphJSON)) {
      live.compile(e.materialAsset->compiled);
    }
    Utils::FS::saveTextFile(e.path, e.materialAsset->serialize());
  }

  int cmdMaterialGraphListNodes(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolveMaterialAsset(project, a.asset);
    if (!e) { emitErr("material not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadMaterialGraph(*e, doc, err)) { emitErr(err); return 1; }
    nlohmann::json out;
    out["asset"] = e->name;
    out["nodes"] = gjListNodes(doc, Project::MaterialGraph::Graph::getNodeNames());
    out["links"] = doc.value("links", nlohmann::json::array());
    emitJSON(out);
    return 0;
  }

  int cmdMaterialGraphAddNode(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.type.empty()) {
      emitErr("--asset and --type are required"); return 1;
    }
    auto *e = resolveMaterialAsset(project, a.asset);
    if (!e) { emitErr("material not found: " + a.asset); return 1; }
    const auto &names = Project::MaterialGraph::Graph::getNodeNames();
    int typeId = graphTypeFromArgGeneric(a.type, names);
    if (typeId < 0 || typeId >= (int)names.size()) { emitErr("unknown node type: " + a.type); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadMaterialGraph(*e, doc, err)) { emitErr(err); return 1; }
    uint64_t newUUID = 0;
    if (!gjAddNode(doc, typeId, a.value, newUUID, err)) { emitErr(err); return 1; }
    saveMaterialGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["added"] = newUUID;
    out["typeName"] = names[typeId];
    out["asset"] = e->name;
    emitJSON(out);
    return 0;
  }

  int cmdMaterialGraphRemoveNode(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.value.empty()) {
      emitErr("--asset and --value (node uuid) are required"); return 1;
    }
    auto *e = resolveMaterialAsset(project, a.asset);
    if (!e) { emitErr("material not found: " + a.asset); return 1; }
    auto u = tryParseUUID(a.value);
    if (!u) { emitErr("--value must be a uuid"); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadMaterialGraph(*e, doc, err)) { emitErr(err); return 1; }
    int removedLinks = 0;
    if (!gjRemoveNode(doc, *u, removedLinks, err)) { emitErr(err); return 1; }
    saveMaterialGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["removed"] = *u;
    out["removedLinks"] = removedLinks;
    emitJSON(out);
    return 0;
  }

  int cmdMaterialGraphConnect(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from <nodeUUID>:<pinIdx>, --to <nodeUUID>:<pinIdx> are required"); return 1;
    }
    auto *e = resolveMaterialAsset(project, a.asset);
    if (!e) { emitErr("material not found: " + a.asset); return 1; }
    uint64_t srcUUID = 0, dstUUID = 0; uint32_t srcPin = 0, dstPin = 0;
    std::string err;
    if (!parsePinSpec(a.from, srcUUID, srcPin, err)) { emitErr("--from: " + err); return 1; }
    if (!parsePinSpec(a.to,   dstUUID, dstPin, err)) { emitErr("--to: "   + err); return 1; }
    nlohmann::json doc;
    if (!loadMaterialGraph(*e, doc, err)) { emitErr(err); return 1; }
    if (!gjConnect(doc, srcUUID, srcPin, dstUUID, dstPin, err)) { emitErr(err); return 1; }
    saveMaterialGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["connected"] = {{"src", srcUUID}, {"srcPort", srcPin}, {"dst", dstUUID}, {"dstPort", dstPin}};
    out["index"] = (int)doc["links"].size() - 1;
    emitJSON(out);
    return 0;
  }

  int cmdMaterialGraphDisconnect(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.from.empty() || a.to.empty()) {
      emitErr("--asset, --from <nodeUUID>:<pinIdx>, --to <nodeUUID>:<pinIdx> are required"); return 1;
    }
    auto *e = resolveMaterialAsset(project, a.asset);
    if (!e) { emitErr("material not found: " + a.asset); return 1; }
    uint64_t srcUUID = 0, dstUUID = 0; uint32_t srcPin = 0, dstPin = 0;
    std::string err;
    if (!parsePinSpec(a.from, srcUUID, srcPin, err)) { emitErr("--from: " + err); return 1; }
    if (!parsePinSpec(a.to,   dstUUID, dstPin, err)) { emitErr("--to: "   + err); return 1; }
    nlohmann::json doc;
    if (!loadMaterialGraph(*e, doc, err)) { emitErr(err); return 1; }
    int removed = gjDisconnect(doc, srcUUID, srcPin, dstUUID, dstPin);
    if (removed == 0) { emitErr("matching link not found"); return 1; }
    saveMaterialGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["disconnected"] = removed;
    emitJSON(out);
    return 0;
  }

  // ── Per-graph: set-node-prop / duplicate-node / set-node-pos / compile ──

  // Args common to all three: --asset, --node (uuid), and either --field+--value,
  // or --value (pos array), or --value (new-pos for duplicate, optional).

  int cmdGraphSetNodeProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty()) {
      emitErr("--asset, --field, --value are required (use --parent for node uuid)"); return 1;
    }
    auto u = tryParseUUID(a.parent);
    if (!u) { emitErr("--parent must be a node uuid"); return 1; }
    auto *e = resolveGraphAsset(project, a.asset);
    if (!e) { emitErr("graph not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadGraphJSON(e->path, doc, err)) { emitErr(err); return 1; }
    nlohmann::json outNode;
    if (!gjSetNodeProp(doc, *u, a.field, parseValueJSON(a.value), outNode, err)) { emitErr(err); return 1; }
    if (!saveGraphJSON(e->path, doc)) { emitErr("save failed"); return 1; }
    project.getAssets().reload();
    nlohmann::json out;
    out["updated"] = a.field;
    out["node"] = outNode;
    emitJSON(out);
    return 0;
  }

  int cmdGraphDuplicateNode(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.parent.empty()) {
      emitErr("--asset and --parent (source node uuid) are required"); return 1;
    }
    auto u = tryParseUUID(a.parent);
    if (!u) { emitErr("--parent must be a node uuid"); return 1; }
    auto *e = resolveGraphAsset(project, a.asset);
    if (!e) { emitErr("graph not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadGraphJSON(e->path, doc, err)) { emitErr(err); return 1; }
    uint64_t newUUID = 0;
    if (!gjDuplicateNode(doc, *u, a.value, newUUID, err)) { emitErr(err); return 1; }
    if (!saveGraphJSON(e->path, doc)) { emitErr("save failed"); return 1; }
    project.getAssets().reload();
    nlohmann::json out;
    out["duplicated"] = *u;
    out["newUUID"] = newUUID;
    emitJSON(out);
    return 0;
  }

  int cmdGraphSetNodePos(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.parent.empty() || a.value.empty()) {
      emitErr("--asset, --parent (node uuid), --value [x,y] are required"); return 1;
    }
    auto u = tryParseUUID(a.parent);
    if (!u) { emitErr("--parent must be a node uuid"); return 1; }
    auto *e = resolveGraphAsset(project, a.asset);
    if (!e) { emitErr("graph not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadGraphJSON(e->path, doc, err)) { emitErr(err); return 1; }
    nlohmann::json outNode;
    if (!gjSetNodePos(doc, *u, a.value, outNode, err)) { emitErr(err); return 1; }
    if (!saveGraphJSON(e->path, doc)) { emitErr("save failed"); return 1; }
    project.getAssets().reload();
    nlohmann::json out;
    out["node"] = outNode;
    emitJSON(out);
    return 0;
  }

  // Serialises a Compile::ErrorList into the same {diagnostics,ok,...}
  // shape gjValidate emits, so the response schema stays stable.
  nlohmann::json compileErrorsToJSON(const Project::Compile::ErrorList &errs)
  {
    nlohmann::json diags = nlohmann::json::array();
    for (const auto &e : errs.all()) {
      nlohmann::json d;
      d["severity"] = (e.severity == Project::Compile::Severity::ERROR) ? "error" : "warning";
      d["message"] = e.message;
      if (e.nodeUUID) d["nodeUUID"] = e.nodeUUID;
      if (e.assetUUID) d["assetUUID"] = e.assetUUID;
      diags.push_back(d);
    }
    nlohmann::json out;
    out["diagnostics"] = diags;
    out["ok"] = errs.errorCount() == 0;
    out["errorCount"] = (uint64_t)errs.errorCount();
    out["warningCount"] = (uint64_t)errs.warningCount();
    return out;
  }

  // Runs the editor's full Graph::validate on a CLI-loaded graph. This
  // exercises pin-style reachability (unreachable-node detection), which
  // a JSON-only walker can't see. ImNodeFlow is constructible without an
  // ImGui frame — the build pipeline (nodeGraphBuilder.cpp) does the same
  // thing during --cmd build.
  int cmdGraphCompile(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolveGraphAsset(project, a.asset);
    if (!e) { emitErr("graph not found: " + a.asset); return 1; }
    auto json = Utils::FS::loadTextFile(e->path);
    if (json.empty()) { emitErr("empty graph file: " + e->path); return 1; }
    Project::Graph::Graph graph{};
    if (!graph.deserialize(json)) { emitErr("graph deserialize failed"); return 1; }
    Project::Compile::ErrorList errs{};
    graph.validate(&errs, e->getUUID());
    auto out = compileErrorsToJSON(errs);
    out["asset"] = e->name;
    emitJSON(out);
    return out.value("ok", true) ? 0 : 1;
  }

  // Event-graph variants — same shape, different loader/saver.

  int cmdEventGraphSetNodeProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty() || a.parent.empty()) {
      emitErr("--asset, --parent (node uuid), --field, --value are required"); return 1;
    }
    auto u = tryParseUUID(a.parent);
    if (!u) { emitErr("--parent must be a node uuid"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    nlohmann::json outNode;
    if (!gjSetNodeProp(doc, *u, a.field, parseValueJSON(a.value), outNode, err)) { emitErr(err); return 1; }
    saveEventGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["updated"] = a.field;
    out["node"] = outNode;
    emitJSON(out);
    return 0;
  }

  int cmdEventGraphDuplicateNode(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.parent.empty()) {
      emitErr("--asset and --parent (source node uuid) are required"); return 1;
    }
    auto u = tryParseUUID(a.parent);
    if (!u) { emitErr("--parent must be a node uuid"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    uint64_t newUUID = 0;
    if (!gjDuplicateNode(doc, *u, a.value, newUUID, err)) { emitErr(err); return 1; }
    saveEventGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["duplicated"] = *u;
    out["newUUID"] = newUUID;
    emitJSON(out);
    return 0;
  }

  int cmdEventGraphSetNodePos(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.parent.empty() || a.value.empty()) {
      emitErr("--asset, --parent (node uuid), --value [x,y] are required"); return 1;
    }
    auto u = tryParseUUID(a.parent);
    if (!u) { emitErr("--parent must be a node uuid"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    nlohmann::json outNode;
    if (!gjSetNodePos(doc, *u, a.value, outNode, err)) { emitErr(err); return 1; }
    saveEventGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["node"] = outNode;
    emitJSON(out);
    return 0;
  }

  int cmdEventGraphCompile(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty()) { emitErr("--asset is required"); return 1; }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    // Empty event-graph: nothing to validate, but report ok so callers can
    // chain compile without first having to check for graph existence.
    if (e->prefab->eventGraphJSON.empty()) {
      nlohmann::json out;
      out["diagnostics"] = nlohmann::json::array();
      out["ok"] = true;
      out["errorCount"] = 0;
      out["warningCount"] = 0;
      out["asset"] = e->name;
      out["note"] = "empty event graph";
      emitJSON(out);
      return 0;
    }
    Project::Graph::Graph graph{};
    if (!graph.deserialize(e->prefab->eventGraphJSON)) {
      emitErr("event graph deserialize failed"); return 1;
    }
    Project::Compile::ErrorList errs{};
    graph.validate(&errs, e->getUUID());
    auto out = compileErrorsToJSON(errs);
    out["asset"] = e->name;
    emitJSON(out);
    return out.value("ok", true) ? 0 : 1;
  }

  // Material-graph variants — same shape, MaterialGraph::Graph node table.

  int cmdMaterialGraphSetNodeProp(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.field.empty() || a.value.empty() || a.parent.empty()) {
      emitErr("--asset, --parent (node uuid), --field, --value are required"); return 1;
    }
    auto u = tryParseUUID(a.parent);
    if (!u) { emitErr("--parent must be a node uuid"); return 1; }
    auto *e = resolveMaterialAsset(project, a.asset);
    if (!e) { emitErr("material not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadMaterialGraph(*e, doc, err)) { emitErr(err); return 1; }
    nlohmann::json outNode;
    if (!gjSetNodeProp(doc, *u, a.field, parseValueJSON(a.value), outNode, err)) { emitErr(err); return 1; }
    saveMaterialGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["updated"] = a.field;
    out["node"] = outNode;
    emitJSON(out);
    return 0;
  }

  int cmdMaterialGraphDuplicateNode(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.parent.empty()) {
      emitErr("--asset and --parent (source node uuid) are required"); return 1;
    }
    auto u = tryParseUUID(a.parent);
    if (!u) { emitErr("--parent must be a node uuid"); return 1; }
    auto *e = resolveMaterialAsset(project, a.asset);
    if (!e) { emitErr("material not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadMaterialGraph(*e, doc, err)) { emitErr(err); return 1; }
    uint64_t newUUID = 0;
    if (!gjDuplicateNode(doc, *u, a.value, newUUID, err)) { emitErr(err); return 1; }
    saveMaterialGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["duplicated"] = *u;
    out["newUUID"] = newUUID;
    emitJSON(out);
    return 0;
  }

  int cmdMaterialGraphSetNodePos(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.parent.empty() || a.value.empty()) {
      emitErr("--asset, --parent (node uuid), --value [x,y] are required"); return 1;
    }
    auto u = tryParseUUID(a.parent);
    if (!u) { emitErr("--parent must be a node uuid"); return 1; }
    auto *e = resolveMaterialAsset(project, a.asset);
    if (!e) { emitErr("material not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadMaterialGraph(*e, doc, err)) { emitErr(err); return 1; }
    nlohmann::json outNode;
    if (!gjSetNodePos(doc, *u, a.value, outNode, err)) { emitErr(err); return 1; }
    saveMaterialGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["node"] = outNode;
    emitJSON(out);
    return 0;
  }

  // ── Event-graph drag-drop convenience wrappers ─────────────────────
  //
  // The GUI lets you drag a prefab variable / function onto the event-
  // graph canvas to spawn a pre-bound PrefabVarGet / PrefabFunc node.
  // These two commands mirror that UX so headless callers don't have to
  // know which type index encodes "Get Variable" or "Prefab Function"
  // nor which JSON field carries the binding.

  int cmdEventGraphAddVarGet(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.name.empty()) {
      emitErr("--asset (prefab/widget), --name (variable name) are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    const Project::PrefabVarDef *varDef = nullptr;
    for (const auto &v : e->prefab->variables) {
      if (v.name == a.name) { varDef = &v; break; }
    }
    if (!varDef) { emitErr("prefab has no variable: " + a.name); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    nlohmann::json node;
    node["uuid"] = Utils::Hash::randomU64();
    node["type"] = Project::Graph::TYPE_PREFAB_VAR_GET;
    if (!a.value.empty()) {
      auto pos = parseValueJSON(a.value);
      if (!pos.is_array() || pos.size() != 2) { emitErr("--value must be [x,y]"); return 1; }
      node["pos"] = pos;
    } else {
      node["pos"] = {0.0f, 0.0f};
    }
    // PrefabVarGet's deserialize reads "varUUID". Bind it to the resolved var.
    node["varUUID"] = varDef->uuid;
    doc["nodes"].push_back(node);
    saveEventGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["added"] = node["uuid"];
    out["typeName"] = "PrefabVarGet";
    out["var"] = a.name;
    out["varUUID"] = varDef->uuid;
    emitJSON(out);
    return 0;
  }

  int cmdEventGraphAddFuncCall(const CLI::Commands::Args &a, Project::Project &project)
  {
    if (a.asset.empty() || a.func.empty()) {
      emitErr("--asset (prefab/widget), --func (P64_NODE function name) are required"); return 1;
    }
    auto *e = resolvePrefabOrWidget(project, a.asset);
    if (!e || !e->prefab) { emitErr("prefab/widget not found: " + a.asset); return 1; }
    nlohmann::json doc; std::string err;
    if (!loadPrefabEventGraph(*e, doc, err)) { emitErr(err); return 1; }
    nlohmann::json node;
    node["uuid"] = Utils::Hash::randomU64();
    node["type"] = Project::Graph::TYPE_PREFAB_FUNC;
    if (!a.value.empty()) {
      auto pos = parseValueJSON(a.value);
      if (!pos.is_array() || pos.size() != 2) { emitErr("--value must be [x,y]"); return 1; }
      node["pos"] = pos;
    } else {
      node["pos"] = {0.0f, 0.0f};
    }
    // PrefabFunc's deserialize reads "funcName".
    node["funcName"] = a.func;
    doc["nodes"].push_back(node);
    saveEventGraphInto(*e, doc);
    project.getAssets().reload();
    nlohmann::json out;
    out["added"] = node["uuid"];
    out["typeName"] = "PrefabFunc";
    out["func"] = a.func;
    emitJSON(out);
    return 0;
  }
}

namespace CLI::Commands
{
  // Single source of truth for the extended command set.
  static const std::vector<std::pair<std::string, int(*)(const Args&, Project::Project&)>> kCmds = {
    {"asset-list",              cmdAssetList},
    {"asset-describe",          cmdAssetDescribe},
    {"asset-delete",            cmdAssetDelete},
    {"asset-rename",            cmdAssetRename},
    {"asset-import",            cmdAssetImport},
    {"asset-set-conf",          cmdAssetSetConf},
    {"component-list",          cmdComponentList},
    {"component-describe",      cmdComponentDescribe},
    {"prefab-create",           cmdPrefabCreate},
    {"prefab-duplicate",        cmdPrefabDuplicate},
    {"prefab-variant",          cmdPrefabVariant},
    {"prefab-describe",         cmdPrefabDescribe},
    {"prefab-add-object",       cmdPrefabAddObject},
    {"prefab-remove-object",    cmdPrefabRemoveObject},
    {"prefab-set-transform",    cmdPrefabSetTransform},
    {"prefab-add-component",    cmdPrefabAddComponent},
    {"prefab-remove-component", cmdPrefabRemoveComponent},
    {"prefab-set-prop",         cmdPrefabSetProp},
    {"code-add-function",       cmdCodeAddFunction},
    {"code-rename-function",    cmdCodeRenameFunction},
    {"code-remove-function",    cmdCodeRemoveFunction},
    {"code-list-functions",     cmdCodeListFunctions},
    {"script-create",           cmdScriptCreate},
    {"resource-create",         cmdResourceCreate},
    {"resource-set-prop",       cmdResourceSetProp},
    {"graph-create",            cmdGraphCreate},
    {"material-create",         cmdMaterialCreate},
    {"widget-create",           cmdWidgetCreate},
    {"particle-system-create",  cmdParticleSystemCreate},
    {"save-file-create",        cmdSaveFileCreate},
    {"save-file-list",          cmdSaveFileList},
    {"save-file-describe",      cmdSaveFileDescribe},
    {"save-file-add-field",     cmdSaveFileAddField},
    {"save-file-remove-field",  cmdSaveFileRemoveField},
    {"save-file-set-field",     cmdSaveFileSetField},
    // Widget structural editing reuses the prefab commands (widgets share
    // the prefab on-disk shape; resolvePrefabOrWidget accepts either type).
    {"widget-describe",         cmdPrefabDescribe},
    {"widget-add-object",       cmdPrefabAddObject},
    {"widget-remove-object",    cmdPrefabRemoveObject},
    {"widget-add-component",    cmdPrefabAddComponent},
    {"widget-remove-component", cmdPrefabRemoveComponent},
    {"widget-set-prop",         cmdPrefabSetProp},
    {"widget-set-transform",    cmdPrefabSetTransform},
    {"widget-move-object",      cmdPrefabMoveObject},
    {"widget-promote-root",     cmdPrefabPromoteRoot},
    {"widget-bind-event",       cmdWidgetBindEvent},
    // Scenes (full CLI parity with prefabs so headless callers can
    // compose entire levels without the editor).
    {"scene-list",              cmdSceneList},
    {"scene-create",            cmdSceneCreate},
    {"scene-describe",          cmdSceneDescribe},
    {"scene-rename",            cmdSceneRename},
    {"scene-set-relpath",       cmdSceneSetRelpath},
    {"scene-duplicate",         cmdSceneDuplicate},
    {"scene-delete",            cmdSceneDelete},
    {"scene-set-conf",          cmdSceneSetConf},
    {"scene-add-object",        cmdSceneAddObject},
    {"scene-remove-object",     cmdSceneRemoveObject},
    {"scene-set-transform",     cmdSceneSetTransform},
    {"scene-add-component",     cmdSceneAddComponent},
    {"scene-remove-component",  cmdSceneRemoveComponent},
    {"scene-set-prop",          cmdSceneSetProp},
    {"scene-move-object",       cmdSceneMoveObject},
    {"scene-add-prefab-instance", cmdSceneAddPrefabInstance},
    // Prefab structural extras.
    {"prefab-move-object",      cmdPrefabMoveObject},
    {"prefab-promote-root",     cmdPrefabPromoteRoot},
    // Path component granular ops (Catmull-Rom spline authoring).
    {"path-add-point",          cmdPathAddPoint},
    {"path-insert-point",       cmdPathInsertPoint},
    {"path-set-point",          cmdPathSetPoint},
    {"path-remove-point",       cmdPathRemovePoint},
    {"path-add-branch",         cmdPathAddBranch},
    {"path-set-branch",         cmdPathSetBranch},
    {"path-remove-branch",      cmdPathRemoveBranch},
    // Scene-side Path authoring (same ops, but the Path component lives on
    // an object inside a scene rather than inside a prefab/widget).
    {"scene-path-add-point",    cmdScenePathAddPoint},
    {"scene-path-insert-point", cmdScenePathInsertPoint},
    {"scene-path-set-point",    cmdScenePathSetPoint},
    {"scene-path-remove-point", cmdScenePathRemovePoint},
    {"scene-path-add-branch",   cmdScenePathAddBranch},
    {"scene-path-set-branch",   cmdScenePathSetBranch},
    {"scene-path-remove-branch", cmdScenePathRemoveBranch},
    // Resource type schema authoring.
    {"restype-create",          cmdRestypeCreate},
    {"restype-add-prop",        cmdRestypeAddProp},
    {"restype-remove-prop",     cmdRestypeRemoveProp},
    {"restype-rename-prop",     cmdRestypeRenameProp},
    // Events.
    {"event-list",              cmdEventList},
    // Prefab scaffolding (Tier 1 defaults + Tier 2 backfill).
    {"prefab-scaffold-defaults", cmdPrefabScaffoldDefaults},
    {"prefab-graph-validate",    cmdPrefabGraphValidate},
    {"widget-scaffold-defaults", cmdPrefabScaffoldDefaults},
    {"widget-graph-validate",    cmdPrefabGraphValidate},
    // Prefab class variables (Blueprint-style typed props).
    {"prefab-add-variable",     cmdPrefabAddVariable},
    {"prefab-remove-variable",  cmdPrefabRemoveVariable},
    {"prefab-rename-variable",  cmdPrefabRenameVariable},
    {"prefab-set-variable-default", cmdPrefabSetVariableDefault},
    {"widget-add-variable",     cmdPrefabAddVariable},
    {"widget-remove-variable",  cmdPrefabRemoveVariable},
    {"widget-rename-variable",  cmdPrefabRenameVariable},
    {"widget-set-variable-default", cmdPrefabSetVariableDefault},
    // Per-instance variable overrides on scene objects.
    {"scene-set-var-override",   cmdSceneSetVarOverride},
    {"scene-clear-var-override", cmdSceneClearVarOverride},
    // Scene render layer arrays (3D / 2D / particles).
    {"scene-add-layer",         cmdSceneAddLayer},
    {"scene-set-layer",         cmdSceneSetLayer},
    {"scene-remove-layer",      cmdSceneRemoveLayer},
    // Project-level conf (ROM metadata, boot scenes, cart size, etc.).
    {"project-describe",        cmdProjectDescribe},
    {"project-set-conf",        cmdProjectSetConf},
    // Asset folders (mirrored across assets/ and src/user/).
    {"folder-create",           cmdFolderCreate},
    {"folder-rename",           cmdFolderRename},
    {"folder-move",             cmdFolderMove},
    {"folder-delete",           cmdFolderDelete},
    // Asset relocation across folders.
    {"asset-move",              cmdAssetMove},
    // Material editing (compiled material fields on .p64mat).
    {"material-set-prop",       cmdMaterialSetProp},
    // Conf schema discovery.
    {"asset-describe-conf",     cmdAssetDescribeConf},
    // Search / refactor helpers.
    {"scene-find",              cmdSceneFind},
    {"prefab-find",             cmdPrefabFind},
    {"prefab-find-references",  cmdPrefabFindReferences},
    {"asset-find-unused",       cmdAssetFindUnused},
    // Bootstrap a fresh project from the empty template.
    {"project-create",          cmdProjectCreate},
    // Variant prefab inheritance — structured sparse overrides keyed by
    // stable Object/Component uuids (Blueprint-Actor style).
    {"prefab-override-prop",            cmdPrefabOverrideProp},
    {"prefab-reset-prop",               cmdPrefabResetProp},
    {"prefab-override-var-default",     cmdPrefabOverrideVarDefault},
    {"prefab-reset-var-default",        cmdPrefabResetVarDefault},
    {"prefab-remove-inherited-object",  cmdPrefabRemoveInheritedObject},
    {"prefab-remove-inherited-component", cmdPrefabRemoveInheritedComponent},
    {"prefab-describe-inheritance",     cmdPrefabDescribeInheritance},
    // Removed: prefab-{list,add,remove}-patch (RFC-6902 model retired).
    // Kept as shims so existing scripts get a useful deprecation message.
    {"prefab-list-patches",     cmdPrefabListPatches},
    {"prefab-add-patch",        cmdPrefabAddPatch},
    {"prefab-remove-patch",     cmdPrefabRemovePatch},
    // Scene "To Prefab" — extract a subtree into a new .prefab asset.
    {"scene-extract-prefab",    cmdSceneExtractPrefab},
    // Object Inspector "Duplicate Component" — clones a component + its data.
    {"scene-duplicate-component", cmdSceneDuplicateComponent},
    // Standalone NodeGraph (.p64graph) node-level ops.
    {"graph-list-nodes",        cmdGraphListNodes},
    {"graph-add-node",          cmdGraphAddNode},
    {"graph-remove-node",       cmdGraphRemoveNode},
    {"graph-connect",           cmdGraphConnect},
    {"graph-disconnect",        cmdGraphDisconnect},
    // Prefab event-graph node-level ops (graph inside the .prefab file).
    {"event-graph-list-nodes",  cmdEventGraphListNodes},
    {"event-graph-add-node",    cmdEventGraphAddNode},
    {"event-graph-remove-node", cmdEventGraphRemoveNode},
    {"event-graph-connect",     cmdEventGraphConnect},
    {"event-graph-disconnect",  cmdEventGraphDisconnect},
    // Material-graph node-level ops (graph inside the .p64mat file).
    {"material-graph-list-nodes",  cmdMaterialGraphListNodes},
    {"material-graph-add-node",    cmdMaterialGraphAddNode},
    {"material-graph-remove-node", cmdMaterialGraphRemoveNode},
    {"material-graph-connect",     cmdMaterialGraphConnect},
    {"material-graph-disconnect",  cmdMaterialGraphDisconnect},
    // Per-graph: set-node-prop / duplicate-node / set-node-pos / compile.
    // --parent carries the source-node uuid for these commands (the existing
    // --asset slot is already used for the containing graph asset).
    {"graph-set-node-prop",        cmdGraphSetNodeProp},
    {"graph-duplicate-node",       cmdGraphDuplicateNode},
    {"graph-set-node-pos",         cmdGraphSetNodePos},
    {"graph-compile",              cmdGraphCompile},
    {"event-graph-set-node-prop",  cmdEventGraphSetNodeProp},
    {"event-graph-duplicate-node", cmdEventGraphDuplicateNode},
    {"event-graph-set-node-pos",   cmdEventGraphSetNodePos},
    {"event-graph-compile",        cmdEventGraphCompile},
    {"event-graph-add-var-get",    cmdEventGraphAddVarGet},
    {"event-graph-add-func-call",  cmdEventGraphAddFuncCall},
    {"material-graph-set-node-prop",  cmdMaterialGraphSetNodeProp},
    {"material-graph-duplicate-node", cmdMaterialGraphDuplicateNode},
    {"material-graph-set-node-pos",   cmdMaterialGraphSetNodePos},
    // Duplicate / reset helpers (P2 ergonomics).
    {"scene-duplicate-layer",      cmdSceneDuplicateLayer},
    {"scene-reset-layers",         cmdSceneResetLayers},
    {"restype-duplicate-prop",     cmdRestypeDuplicateProp},
    {"prefab-duplicate-variable",  cmdPrefabDuplicateVariable},
    {"widget-duplicate-variable",  cmdPrefabDuplicateVariable},
    {"project-set-collision-layer", cmdProjectSetCollisionLayer},
  };

  bool isExtendedCmd(const std::string &cmd)
  {
    for (const auto &[n, _] : kCmds) if (n == cmd) return true;
    return false;
  }

  void registerFlags(argparse::ArgumentParser &prog)
  {
    auto add = [&](const char *name, const char *help) {
      prog.add_argument(name).default_value(std::string{}).help(help);
    };
    add("--asset",   "Target asset (name or uuid).");
    add("--type",    "Asset type filter (e.g. image, prefab, model3d, codeGlobal).");
    add("--name",    "New asset / object / function name.");
    add("--dir",     "Subdirectory under the project for create/import.");
    add("--file",    "Source file path for asset-import.");
    add("--dest",    "Destination directory for asset-import.");
    add("--field",   "Field name (conf field, transform component, comp data field).");
    add("--value",   "Field value as JSON (numbers, booleans, arrays, strings).");
    add("--path",    "Slash-separated Object name path inside a prefab tree (root implied if empty).");
    add("--parent",  "Parent path or parent prefab uuid/name.");
    add("--comp",    "Component name or numeric id.");
    add("--func",    "Function name (P64_NODE-tagged).");
    add("--from",    "Old name (for *-rename-* commands).");
    add("--to",      "New name (for *-rename-* commands).");
    add("--restype", "Resource type asset (uuid or name) for resource-create.");
    add("--element-kind", "Element scalar kind for ARRAY-typed prefab vars (int|float|bool). Required when --type=array.");
    // Boolean toggles. argparse's implicit_value/default_value duo makes
    // these `--flag`-only (no value needed); the readArgs side pulls them
    // straight as bools.
    prog.add_argument("--no-scaffold")
      .default_value(false)
      .implicit_value(true)
      .help("Skip scaffolding default lifecycle events / P64_NODE stubs when creating a prefab.");
    prog.add_argument("--autofix")
      .default_value(false)
      .implicit_value(true)
      .help("Apply autofixes (e.g. backfill missing P64_NODE stubs or variable defs) instead of just reporting.");
  }

  void readArgs(argparse::ArgumentParser &prog, Args &args)
  {
    auto get = [&](const char *flag) { return prog.get<std::string>(flag); };
    args.asset   = get("--asset");
    args.type    = get("--type");
    args.name    = get("--name");
    args.dir     = get("--dir");
    args.file    = get("--file");
    args.dest    = get("--dest");
    args.field   = get("--field");
    args.value   = get("--value");
    args.path    = get("--path");
    args.parent  = get("--parent");
    args.comp    = get("--comp");
    args.func    = get("--func");
    args.from    = get("--from");
    args.to      = get("--to");
    args.restype = get("--restype");
    args.elementKind = get("--element-kind");
    args.noScaffold = prog.get<bool>("--no-scaffold");
    args.autofix    = prog.get<bool>("--autofix");
  }

  int dispatch(const Args &args, Project::Project &project)
  {
    for (const auto &[n, fn] : kCmds) if (n == args.cmd) return fn(args, project);
    emitErr("unknown command: " + args.cmd);
    return 1;
  }

  int dispatchBootstrap(const Args &args)
  {
    if (args.cmd == "project-create") return doProjectCreate(args);
    emitErr("not a bootstrap command: " + args.cmd);
    return 1;
  }

  void printExtendedHelp()
  {
    fputs("\nAsset/prefab tooling commands (use with --cli --cmd <name>):\n", stdout);
    for (const auto &[n, _] : kCmds) {
      fprintf(stdout, "  %s\n", n.c_str());
    }
    fputs(
      "\nFlags accepted by the above (only relevant ones are read per command):\n"
      "  --asset --type --name --dir --file --dest --field --value\n"
      "  --path --parent --comp --func --from --to --restype\n"
      "\nValues are parsed as JSON, falling back to raw strings. Use --help on\n"
      "individual command examples in the CLAUDE.md or commit message.\n",
      stdout
    );
  }
}
