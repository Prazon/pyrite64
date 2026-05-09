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
#include "../project/assets/resourceInstance.h"
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
