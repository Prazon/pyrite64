/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "project.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "../build/projectBuilder.h"
#include "../utils/fs.h"
#include "../utils/hash.h"
#include "../utils/json.h"
#include "../utils/jsonBuilder.h"
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
}

std::string Project::ProjectConf::serialize() const {
  return Utils::JSON::Builder{}
    .set("name", name)
    .set("romName", romName)
    .set("pathEmu", pathEmu)
    .set("pathN64Inst", pathN64Inst)
    .set("sceneIdOnBoot", sceneIdOnBoot)
    .set("sceneIdOnReset", sceneIdOnReset)
    .set("sceneIdLastOpened", sceneIdLastOpened)
    .set("collLayer0", collLayerNames[0])
    .set("collLayer1", collLayerNames[1])
    .set("collLayer2", collLayerNames[2])
    .set("collLayer3", collLayerNames[3])
    .set("collLayer4", collLayerNames[4])
    .set("collLayer5", collLayerNames[5])
    .set("collLayer6", collLayerNames[6])
    .set("collLayer7", collLayerNames[7])
    .toString();
}

void Project::Project::deserialize(const nlohmann::json &doc) {
  conf.name = doc.value("name", "New Project");
  conf.romName = doc.value("romName", "pyrite64");
  conf.pathEmu = doc.value("pathEmu", "ares");
  conf.pathN64Inst = doc.value("pathN64Inst", "");
  conf.sceneIdOnBoot = doc.value("sceneIdOnBoot", 1);
  conf.sceneIdOnReset = doc.value("sceneIdOnReset", 1);
  conf.sceneIdLastOpened = doc.value("sceneIdLastOpened", 1);

  for(int i=0; i<8; ++i) {
    conf.collLayerNames[i] = doc.value("collLayer" + std::to_string(i), "Layer " + std::to_string(i));
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
  Utils::FS::ensureFile(f / "Makefile.custom", "data/build/baseMakefile.custom");
  Utils::FS::ensureFile(f / "assets" / "p64" / "font.ia4.png", "data/build/assets/font.ia4.png");

  deserialize(configJSON);
  savedState = conf.serialize();

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
  auto serializedConfig = conf.serialize();
  Utils::FS::saveTextFile(pathConfig, serializedConfig);
  savedState = serializedConfig;
}

void Project::Project::save() {
  saveConfig();
  assets.save();
  scenes.save();
  markSaved();
}

