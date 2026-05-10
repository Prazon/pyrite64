#include "cliCommands.h"

#include "argparse/argparse.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

  // Slash-separated name path. Empty / "/" / the root's own name resolves to
  // the root. Returns the root itself if not found via a deeper match.
  Project::Object *findObjectByPath(Project::Object *root, const std::string &path)
  {
    if (!root) return nullptr;
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
  // out of subdirectories).
  void savePrefabAt(const std::string &absPath, const Project::Prefab &prefab)
  {
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

  // Build, mutate, save lifecycle. The CLI always works on a fresh Scene
  // instance rather than driving SceneManager::loadedScene so we never
  // disturb editor state (selection, undo history) and so concurrent CLI
  // invocations don't collide.
  std::shared_ptr<Project::Scene> openScene(Project::Project &project, int id)
  {
    auto scene = std::make_shared<Project::Scene>(id, project.getPath());
    return scene;
  }

  void saveScene(Project::Project &project, Project::Scene &scene)
  {
    scene.save();
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
    Utils::FS::saveTextFile(outPath.string(), prefab.serialize());
    Project::ensurePrefabUserSource(project.getPath(), outPath.filename().string());
    project.getAssets().reload();
    auto *e = project.getAssets().getByPath(outPath.string());
    nlohmann::json out;
    out["created"] = outPath.string();
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
    variant.patchOps = nlohmann::json::array();
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
    saveScene(project, *scene);
    nlohmann::json out;
    out["addedObject"] = a.name;
    out["uuid"] = obj->uuid;
    out["sceneId"] = id;
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
    // Resource type schema authoring.
    {"restype-create",          cmdRestypeCreate},
    {"restype-add-prop",        cmdRestypeAddProp},
    {"restype-remove-prop",     cmdRestypeRemoveProp},
    {"restype-rename-prop",     cmdRestypeRenameProp},
    // Events.
    {"event-list",              cmdEventList},
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
  }

  int dispatch(const Args &args, Project::Project &project)
  {
    for (const auto &[n, fn] : kCmds) if (n == args.cmd) return fn(args, project);
    emitErr("unknown command: " + args.cmd);
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
