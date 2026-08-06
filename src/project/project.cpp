/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "project.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <string>

#include "../build/projectBuilder.h"
#include "../editor/undoRedo.h"
#include "../utils/fs.h"
#include "../utils/hash.h"
#include "../utils/json.h"
#include "../utils/jsonBuilder.h"
#include "../utils/logger.h"
#include "../utils/string.h"
#include "../context.h"

namespace
{
  // Files/directories under <project>/engine/ that are NOT mirrored from
  // n64/engine/. Anything matching is left alone during the engine sync so
  // user-facing build artifacts and Makefile customizations survive.
  bool isEnginePathPreserved(const std::string &name) {
    return name == "build" || name == ".gitignore";
  }

  /**
   * Mirror engine files from src to dst:
   *  - copy missing/changed files (returns count of changed-or-added files)
   *  - delete dst files/dirs that no longer exist in src (so engine refactors
   *    that remove a header/source don't leave stale copies in the project,
   *    which previously caused builds to fail with mixed-version Frankenstein
   *    headers)
   *
   * Doing the content-hash check avoids unnecessary recompiles when nothing
   * actually changed. Whitelist isEnginePathPreserved keeps build outputs and
   * other non-source files intact.
   *
   * @returns amount of files added/changed/removed
   */
  int copyChangedEngineFiles(const fs::path& src, const fs::path& dst) {

    if (!fs::exists(src)) return 0;

    if (fs::is_directory(src)) {
      fs::create_directories(dst);
      int count = 0;

      // First pass: copy / recurse into source entries.
      for (const auto& entry : fs::directory_iterator(src)) {
        count += copyChangedEngineFiles(entry.path(), dst / entry.path().filename());
      }

      // Second pass: prune dst entries that no longer exist in src.
      if (fs::exists(dst) && fs::is_directory(dst)) {
        std::error_code ec;
        for (const auto& dstEntry : fs::directory_iterator(dst, ec)) {
          if (ec) break;
          auto name = dstEntry.path().filename().string();
          if (isEnginePathPreserved(name)) continue;
          if (!fs::exists(src / name)) {
            // Stale: remove (file or directory). Counted as change so a
            // clean build is forced when a refactor deletes engine sources.
            std::error_code rmEc;
            if (dstEntry.is_directory(rmEc)) {
              auto removed = fs::remove_all(dstEntry.path(), rmEc);
              if (!rmEc) count += static_cast<int>(removed);
            } else {
              if (fs::remove(dstEntry.path(), rmEc)) count++;
            }
          }
        }
      }

      return count;
    }

    std::string srcHash{};
    std::string dstHash{};

    // Read destination file if exists
    if (fs::exists(dst)) {
      std::ifstream ifs(dst, std::ios::binary);
      dstHash = std::string((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
      {
        std::ifstream ifsSrc(src, std::ios::binary);
        srcHash = std::string((std::istreambuf_iterator(ifsSrc)), std::istreambuf_iterator<char>());
      }
    }

    if (dstHash.empty() || srcHash != dstHash) {
      //printf("Copying updated engine file: %s\n", src.string().c_str());
      fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
      return (dst.extension() == ".h");
    }
    return 0;
  }

  nlohmann::json romHeaderToJson(const Project::RomHeaderConf &h) {
    return {
      {"category", h.category}, {"region", h.region}, {"saveType", h.saveType},
      {"regionFree", h.regionFree}, {"rtc", h.rtc},
      {"controllers", {h.controllers[0], h.controllers[1], h.controllers[2], h.controllers[3]}},
    };
  }

  void romHeaderFromJson(const nlohmann::json &j, Project::RomHeaderConf &h) {
    h.category = j.value("category", 0);
    h.region = j.value("region", 0);
    h.saveType = j.value("saveType", 0);
    h.regionFree = j.value("regionFree", true);
    h.rtc = j.value("rtc", false);
    auto ctrl = j.value("controllers", nlohmann::json::array());
    for (int i = 0; i < 4; ++i) h.controllers[i] = (i < (int)ctrl.size()) ? ctrl[i].get<int>() : 0;
  }

  nlohmann::json metaLangToJson(const Project::MetaLang &l) {
    return {
      {"lang", l.lang}, {"name", l.name}, {"author", l.author}, {"releaseDate", l.releaseDate},
      {"osiLicense", l.osiLicense}, {"website", l.website}, {"shortDesc", l.shortDesc},
      {"longDesc", l.longDesc}, {"ageRating", l.ageRating}, {"screenshots", l.screenshots},
      {"boxFront", l.boxFront}, {"boxBack", l.boxBack}, {"boxTop", l.boxTop},
      {"boxBottom", l.boxBottom}, {"boxLeft", l.boxLeft}, {"boxRight", l.boxRight},
      {"cartFront", l.cartFront}, {"cartBack", l.cartBack},
    };
  }

  Project::MetaLang metaLangFromJson(const nlohmann::json &j) {
    Project::MetaLang l{};
    l.lang = j.value("lang", "");
    l.name = j.value("name", "");
    l.author = j.value("author", "");
    l.releaseDate = j.value("releaseDate", "");
    l.osiLicense = j.value("osiLicense", "");
    l.website = j.value("website", "");
    l.shortDesc = j.value("shortDesc", "");
    l.longDesc = j.value("longDesc", "");
    l.ageRating = j.value("ageRating", 0);
    l.screenshots = j.value("screenshots", std::vector<uint64_t>{});
    l.boxFront = j.value("boxFront", 0ull); l.boxBack = j.value("boxBack", 0ull);
    l.boxTop = j.value("boxTop", 0ull); l.boxBottom = j.value("boxBottom", 0ull);
    l.boxLeft = j.value("boxLeft", 0ull); l.boxRight = j.value("boxRight", 0ull);
    l.cartFront = j.value("cartFront", 0ull); l.cartBack = j.value("cartBack", 0ull);
    return l;
  }

  nlohmann::json metadataToJson(const Project::MetadataConf &m) {
    auto langs = nlohmann::json::array();
    for (const auto &l : m.langs) langs.push_back(metaLangToJson(l));
    return {{"enabled", m.enabled}, {"langs", langs}};
  }

  void metadataFromJson(const nlohmann::json &j, Project::MetadataConf &m) {
    m.enabled = j.value("enabled", false);
    m.langs.clear();
    for (const auto &lj : j.value("langs", nlohmann::json::array())) {
      m.langs.push_back(metaLangFromJson(lj));
    }
    if (m.langs.empty()) m.langs.push_back(Project::MetaLang{}); // always keep a default entry
  }
}

const char *Project::saveTypeToMakefileString(uint32_t saveType) {
  switch (static_cast<SaveType>(saveType)) {
    case SaveType::EEPROM4K:  return "eeprom4k";
    case SaveType::EEPROM16K: return "eeprom16k";
    case SaveType::SRAM256K:  return "sram256k";
    case SaveType::SRAM768K:  return "sram768k";
    case SaveType::SRAM1M:    return "sram1m";
    case SaveType::FlashRAM:  return "flashram";
    case SaveType::None:
    default:                  return "none";
  }
}

std::string Project::ProjectConf::serialize() const {
  return Utils::JSON::Builder{}
    .set("name", name)
    .set("romName", romName)
    .set("pathEmu", pathEmu)
    .set("pathN64Inst", pathN64Inst)
    .set("author", author)
    .set("version", version)
    .set("description", description)
    .set("gameImageUUID", gameImageUUID)
    .set("editorVersion", PYRITE_VERSION)
    .set("romHeader", romHeaderToJson(romHeader))
    .set("metadata", metadataToJson(metadata))
    .set("sceneIdOnBoot", sceneIdOnBoot)
    .set("sceneIdOnReset", sceneIdOnReset)
    .set("sceneIdLastOpened", sceneIdLastOpened)
    .set("debugMenu", debugMenu)
    .set("cartSize", cartSize)
    .set("collLayer0", collLayerNames[0])
    .set("collLayer1", collLayerNames[1])
    .set("collLayer2", collLayerNames[2])
    .set("collLayer3", collLayerNames[3])
    .set("collLayer4", collLayerNames[4])
    .set("collLayer5", collLayerNames[5])
    .set("collLayer6", collLayerNames[6])
    .set("collLayer7", collLayerNames[7])
    .set("visLayer0", visLayerNames[0])
    .set("visLayer1", visLayerNames[1])
    .set("visLayer2", visLayerNames[2])
    .set("visLayer3", visLayerNames[3])
    .set("visLayer4", visLayerNames[4])
    .set("visLayer5", visLayerNames[5])
    .set("visLayer6", visLayerNames[6])
    .set("visLayer7", visLayerNames[7])
    .toString();
}

void Project::Project::deserialize(const nlohmann::json &doc) {
  conf.name = doc.value("name", "New Project");
  conf.romName = doc.value("romName", "pyrite64");
  conf.pathEmu = doc.value("pathEmu", "ares");
  conf.pathN64Inst = doc.value("pathN64Inst", "");
  conf.author = doc.value("author", "");
  conf.version = doc.value("version", "");
  conf.description = doc.value("description", "");
  conf.gameImageUUID = doc.value("gameImageUUID", 0ull);
  conf.editorVersion = doc.value("editorVersion", "");
  romHeaderFromJson(doc.value("romHeader", nlohmann::json::object()), conf.romHeader);
  metadataFromJson(doc.value("metadata", nlohmann::json::object()), conf.metadata);
  // Legacy fork fields (pre-romHeader): fold into romHeader so older projects
  // keep their save type / region / RTC settings. Index order matches
  // Project::RomMeta::SAVETYPE, so the value carries over verbatim.
  if (!doc.contains("romHeader")) {
    conf.romHeader.saveType = (int)doc.value("saveType", 0u);
    conf.romHeader.regionFree = doc.value("regionFree", conf.romHeader.regionFree);
    conf.romHeader.rtc = doc.value("rtcSupport", false);
  }
  conf.sceneIdOnBoot = doc.value("sceneIdOnBoot", 1);
  conf.sceneIdOnReset = doc.value("sceneIdOnReset", 1);
  conf.sceneIdLastOpened = doc.value("sceneIdLastOpened", 1);
  conf.debugMenu = doc.value("debugMenu", true);
  conf.cartSize = doc.value("cartSize", 3u);
  if (conf.cartSize >= (uint32_t)CART_SIZE_COUNT) conf.cartSize = CART_SIZE_COUNT - 1;

  for(int i=0; i<8; ++i) {
    conf.collLayerNames[i] = doc.value("collLayer" + std::to_string(i), "Layer " + std::to_string(i));
    conf.visLayerNames[i] = doc.value("visLayer" + std::to_string(i), i == 0 ? "Default" : "");
  }
}

Project::Project::Project(const std::string &p64projPath)
  : pathConfig{p64projPath}
{
  path = fs::path(p64projPath).parent_path().string();

  auto configJSON = Utils::JSON::loadFile(pathConfig);
  if (configJSON.empty()) {
    throw std::runtime_error("Not a valid project!");
  }

  // ensure relevant directories and files exist + some basic self-repair
  fs::path f{path};
  fs::create_directories(f);
  fs::create_directories(f / "data");
  fs::create_directories(f / "data" / "scenes");
  fs::create_directories(f / "assets");
  fs::create_directories(f / "assets" / "p64");
  fs::create_directories(f / "src");
  fs::create_directories(f / "src" / "p64");
  fs::create_directories(f / "src" / "user");

  Utils::FS::ensureFile(f / ".gitignore", "data/build/baseGitignore");
  // ensureFile only writes a missing file, so older projects keep their old .gitignore.
  // Make sure the generated metadata/ dir is ignored by appending the entry if absent.
  {
    auto gitignorePath = f / ".gitignore";
    auto content = Utils::FS::loadTextFile(gitignorePath);
    bool hasEntry = false;
    for (auto &line : Utils::splitString(content, '\n')) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line == "metadata") { hasEntry = true; break; }
    }
    if (!hasEntry) {
      if (!content.empty() && content.back() != '\n') content += "\n";
      content += "metadata\n";
      Utils::FS::saveTextFile(gitignorePath, content);
    }
  }
  Utils::FS::ensureFile(f / "Makefile.custom", "data/build/baseMakefile.custom");
  Utils::FS::ensureFile(f / "assets" / "p64" / "font.ia4.png", "data/build/assets/font.ia4.png");

  deserialize(configJSON);
  savedState = conf.serialize();

  int verCmp = conf.editorVersion.empty() ? -1 : Utils::compareSemVer(conf.editorVersion, PYRITE_VERSION);
  if (verCmp < 0) {
    printf("Project saved with older editor version (%s < %s), forcing clean\n",
      conf.editorVersion.empty() ? "none" : conf.editorVersion.c_str(), PYRITE_VERSION);
    Build::cleanProject(*this, {
      .code = true,
      .assets = true,
      .engine = true,
      .engineSrc = true,
    });
  } else if (verCmp > 0) {
    openedFromNewerVersion = true;
    printf("Warning: project saved with newer editor version (%s > %s)\n",
      conf.editorVersion.c_str(), PYRITE_VERSION);
  }

  //auto t = SDL_GetTicksNS();
  if(copyChangedEngineFiles("n64/engine", f / "engine") > 0)
  {
    printf("New Engine files copied, force clean build build\n");
    Build::cleanProject(*this, {
      .code = true,
      .assets = false,
      .engine = true,
    });
  }
  //t = SDL_GetTicksNS() - t;
  //printf("Engine files sync took %.2f ms\n", t / 1e6);


  assets.reload();
  scenes.reload();
}

void Project::Project::saveConfig()
{
  conf.editorVersion = PYRITE_VERSION;
  openedFromNewerVersion = false;
  auto serializedConfig = conf.serialize();
  Utils::FS::saveTextFile(pathConfig, serializedConfig);
  savedState = serializedConfig;
  noteSelfWrite(pathConfig);
}

void Project::Project::save() {
  saveConfig();
  assets.save();
  scenes.save();
  markSaved();
}

void Project::Project::noteSelfWrite(const std::string &absPath)
{
  if (!externalInitialized) return;
  std::error_code ec;
  if (!fs::exists(absPath, ec)) {
    externalWatch.erase(absPath);
    return;
  }
  externalWatch[absPath] = Utils::FS::getFileAge(absPath);
}

void Project::Project::reloadConfigFromDisk()
{
  auto doc = Utils::JSON::loadFile(pathConfig);
  if (doc.empty()) return;
  deserialize(doc);
  savedState = conf.serialize();
  dirty = false;
}

namespace
{
  enum class ConflictChoice { Reload, KeepMine, Cancel };

  ConflictChoice askConflict(const std::string &title, const std::string &message)
  {
    const SDL_MessageBoxButtonData buttons[] = {
      { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Reload from disk" },
      { 0,                                       2, "Keep mine" },
      { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 3, "Cancel" },
    };
    SDL_MessageBoxData mb{};
    mb.flags = SDL_MESSAGEBOX_WARNING;
    mb.window = ctx.window;
    mb.title = title.c_str();
    mb.message = message.c_str();
    mb.numbuttons = SDL_arraysize(buttons);
    mb.buttons = buttons;
    int btn = -1;
    if (!SDL_ShowMessageBox(&mb, &btn)) {
      return ConflictChoice::Cancel;
    }
    if (btn == 1) return ConflictChoice::Reload;
    if (btn == 2) return ConflictChoice::KeepMine;
    return ConflictChoice::Cancel;
  }
}

void Project::Project::pollExternalChanges(bool forceNow)
{
  using Clock = std::chrono::steady_clock;
  constexpr auto kMinInterval = std::chrono::milliseconds(2000);

  auto now = Clock::now();
  bool force = forceNow || externalForceNext;
  externalForceNext = false;
  if (externalInitialized && !force && (now - externalLastCheck) < kMinInterval) {
    return;
  }
  externalLastCheck = now;

  std::unordered_map<std::string, uint64_t> current{};

  auto scenesPath = fs::path{path} / "data" / "scenes";
  if (fs::exists(scenesPath)) {
    for (const auto &dirEntry : fs::directory_iterator{scenesPath}) {
      if (!dirEntry.is_directory()) continue;
      auto sceneJson = dirEntry.path() / "scene.json";
      if (!fs::exists(sceneJson)) continue;
      current[sceneJson.string()] = Utils::FS::getFileAge(sceneJson);
    }
  }

  if (fs::exists(pathConfig)) {
    current[pathConfig] = Utils::FS::getFileAge(pathConfig);
  }

  if (!externalInitialized) {
    externalWatch = std::move(current);
    externalInitialized = true;
    return;
  }

  std::vector<std::string> sceneAdded{};
  std::vector<std::string> sceneModified{};
  std::vector<std::string> sceneRemoved{};
  bool projectModified = false;
  bool projectRemoved = false;

  for (const auto &[p, age] : current) {
    auto it = externalWatch.find(p);
    if (p == pathConfig) {
      if (it == externalWatch.end() || it->second != age) projectModified = true;
      continue;
    }
    if (it == externalWatch.end()) sceneAdded.push_back(p);
    else if (it->second != age)    sceneModified.push_back(p);
  }
  for (const auto &[p, age] : externalWatch) {
    if (current.find(p) != current.end()) continue;
    if (p == pathConfig) projectRemoved = true;
    else                 sceneRemoved.push_back(p);
  }

  // Returns the integer scene id encoded in <project>/data/scenes/<id>/scene.json,
  // or -1 if the path does not match.
  auto sceneIdOf = [](const fs::path &p) -> int {
    auto parent = p.parent_path().filename().string();
    try { return std::stoi(parent); } catch (...) { return -1; }
  };

  Scene *loaded = scenes.getLoadedScene();
  bool needsEntriesRefresh = !sceneAdded.empty() || !sceneRemoved.empty();

  for (const auto &p : sceneModified) {
    int id = sceneIdOf(p);
    if (id < 0) { externalWatch[p] = current[p]; continue; }

    bool isLoaded = (loaded && loaded->getId() == id);
    bool dirtyHere = isLoaded && Editor::UndoRedo::getHistory().isDirty();

    if (!dirtyHere) {
      if (isLoaded) {
        Utils::Logger::log("Scene " + std::to_string(id) + " changed on disk, reloading");
        scenes.reloadFromDisk(id);
      } else {
        needsEntriesRefresh = true;
      }
      externalWatch[p] = current[p];
      continue;
    }

    auto choice = askConflict(
      "Scene Changed on Disk",
      "Scene '" + loaded->getName() + "' was modified outside the editor while you have unsaved changes.\n\n"
      "Reload from disk to load the external version (your changes will be lost), or Keep mine to "
      "preserve your in-memory version (the next save will overwrite the external file)."
    );
    if (choice == ConflictChoice::Reload) {
      scenes.reloadFromDisk(id);
      externalWatch[p] = current[p];
    } else if (choice == ConflictChoice::KeepMine) {
      externalWatch[p] = current[p];
    }
    // Cancel: leave watch entry stale so we re-prompt next tick.
  }

  for (const auto &p : sceneAdded) {
    externalWatch[p] = current[p];
  }
  for (const auto &p : sceneRemoved) {
    int id = sceneIdOf(p);
    if (loaded && id >= 0 && loaded->getId() == id) {
      Utils::Logger::log("Loaded scene " + std::to_string(id) + " was removed from disk");
    }
    externalWatch.erase(p);
  }
  if (needsEntriesRefresh) {
    scenes.reload();
  }

  if (projectRemoved) {
    externalWatch.erase(pathConfig);
  } else if (projectModified) {
    if (!isConfigDirty()) {
      Utils::Logger::log(".p64proj changed on disk, reloading");
      reloadConfigFromDisk();
      externalWatch[pathConfig] = current[pathConfig];
    } else {
      auto choice = askConflict(
        "Project File Changed on Disk",
        "The project file (.p64proj) was modified outside the editor while you have unsaved "
        "project changes.\n\nReload from disk to load the external version (your changes will be "
        "lost), or Keep mine to preserve your in-memory version (the next save will overwrite "
        "the external file)."
      );
      if (choice == ConflictChoice::Reload) {
        reloadConfigFromDisk();
        externalWatch[pathConfig] = current[pathConfig];
      } else if (choice == ConflictChoice::KeepMine) {
        externalWatch[pathConfig] = current[pathConfig];
      }
    }
  }
}

