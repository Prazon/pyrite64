/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "projectBuilder.h"

#include <filesystem>
#include <thread>
#include "../utils/fs.h"
#include "../utils/logger.h"
#include "../utils/proc.h"
#include "../utils/string.h"
#include "../utils/textureFormats.h"
#include "../context.h"

namespace fs = std::filesystem;
using AT = Project::FileType;

namespace
{
  struct AssetBuilder
  {
    Build::BuildFunc func;
    const char* name;
  };

  constexpr auto assetBuilders = std::to_array<AssetBuilder>({
    {Build::buildT3DMAssets,    "3D Model"},
    {Build::buildFontAssets,    "Font"},
    {Build::buildTextureAssets, "Texture"},
    {Build::buildAudioAssets,   "Audio"},
    // must be last: (@TODO: handle prefab referencing prefab, [not in the editor yet])
    {Build::buildPrefabAssets,  "Prefab"},
  });
}

void Build::SceneCtx::addAsset(const Project::AssetManagerEntry &entry)
{
  assetUUIDToIdx[entry.getUUID()] = assetList.size();
  if(entry.romPath.size() > 5) {
    auto outNameNoPrefix = entry.romPath.substr(5); // remove "rom:/"
    assetFileMap += "if(path == \"" + outNameNoPrefix + "\")return " + std::to_string(assetList.size()) + ";\n";
  }

  uint32_t flags = 0;
  if(entry.type == AT::FONT) {
    flags |= 0x01; // KEEP_LOADED
  }

  assetList.push_back({entry.romPath, stringOffset, (uint32_t)entry.type, flags});
  stringOffset += entry.romPath.size() + 1;
}

bool Build::regenerateAssetTable(Project::Project &project)
{
  // Mirror the iteration order and skip rules of buildProject's main asset
  // loop so the indices we emit match what the next full build will produce.
  // Only file-path-addressable entries from the asset manager show up here;
  // synthesized entries that buildProject appends later (e.g. t3dm collision
  // sub-assets) come after these, so 0..N-1 stay stable.
  std::string assetFileMap{};
  size_t idx = 0;
  for (auto &typed : project.getAssets().getEntries()) {
    for (auto &entry : typed) {
      if (entry.conf.exclude
        || entry.type == AT::UNKNOWN
        || entry.type == AT::CODE_OBJ
        || entry.type == AT::CODE_GLOBAL
        || entry.type == AT::RESOURCE_TYPE
      ) continue;

      if (entry.romPath.size() > 5) {
        auto outNameNoPrefix = entry.romPath.substr(5); // strip "rom:/"
        assetFileMap += "if(path == \"" + outNameNoPrefix + "\")return "
          + std::to_string(idx) + ";\n";
      }
      ++idx;
    }
  }

  auto assetTableCode = Utils::replaceAll(
    Utils::FS::loadTextFile("data/scripts/assetTable.h"),
    "{{ASSET_MAP}}", assetFileMap
  );

  auto outPath = project.getPath() + "/src/p64/assetTable.h";
  // Skip the write when the on-disk header already matches. Avoids touching
  // the file's mtime on every watcher tick (which would re-trigger downstream
  // file watchers and add VCS churn) and keeps the no-op case truly cheap.
  if (fs::exists(outPath)) {
    auto existing = Utils::FS::loadTextFile(outPath);
    if (existing == assetTableCode) {
      return false;
    }
  }
  Utils::FS::saveTextFile(outPath, assetTableCode);
  return true;
}

bool Build::buildProject(const std::string &configPath)
{
  Project::Project project{configPath};
  auto path = project.getPath();
  // Reset structured graph diagnostics. The Compile Errors panel reads this
  // list and auto-focuses when its revision counter changes, so clearing here
  // also closes the panel from any previous build.
  ctx.compileErrors.clear();
  Utils::Logger::log("Building project...");

  if(project.conf.pathN64Inst.empty())
  {
    // read N64_INST from environment
    char* n64InstEnv = getenv("N64_INST");
    if(n64InstEnv != nullptr) {
      project.conf.pathN64Inst = n64InstEnv;
    }
  // On Windows, fall back to pyrite64-sdk (autoinstaller location) if not explicitly set by the user. 
  #if defined(_WIN32)
    else {
      project.conf.pathN64Inst = "/pyrite64-sdk";
    }
  #endif
  } else {
  #if defined(_WIN32)
    //_putenv_s("N64_INST", project.conf.pathN64Inst.c_str());
  #else
    setenv("N64_INST", project.conf.pathN64Inst.c_str(), 0);
  #endif
  }

  #if defined(_WIN32)
    _putenv_s("N64_INST", project.conf.pathN64Inst.c_str());
    _putenv_s("MSYSTEM", "MINGW64");
    _putenv_s("PATH", "C:\\msys64\\mingw64\\bin;C:\\msys64\\usr\\bin");
  #endif

  auto fsPath = fs::absolute(fs::path{path} / "filesystem");
  auto fsDataPath = fs::absolute(fs::path{path} / "filesystem" / "p64");
  if (!fs::exists(fsDataPath)) {
    fs::create_directories(fsDataPath);
  }

  SceneCtx sceneCtx{};
  sceneCtx.toolchain.scan();
  sceneCtx.project = &project;

  // Global project config
  sceneCtx.files.push_back("filesystem/p64/conf");

  // Asset-Manager
  for (auto &typed : project.getAssets().getEntries()) {
    for (auto &entry : typed) {
      if (entry.conf.exclude || entry.type == Project::FileType::UNKNOWN
        || entry.type == Project::FileType::CODE_OBJ
        || entry.type == Project::FileType::CODE_GLOBAL
        || entry.type == Project::FileType::RESOURCE_TYPE
      ) continue;
      sceneCtx.addAsset(entry);
    }
  }

  // check if files got added/removed, in which case trigger an asset rebuild.
  // This is needed as some assets reference others via indices.
  {
    auto fileNames = sceneCtx.files;
    std::sort(fileNames.begin(), fileNames.end());
    auto fileStr = Utils::join(fileNames, " ");

    auto oldFileStr = Utils::FS::loadTextFile(fsDataPath / "fileList.txt");
    if(fileStr != oldFileStr)
    {
      Utils::Logger::log("Asset list changed, clean asset dependencies");
      Utils::FS::delTypeRecursive(fsPath, ".t3dm");
      Utils::FS::delTypeRecursive(fsPath, ".pf");
      fs::remove_all(fsDataPath);
      fs::create_directories(fsDataPath);

      cleanProject(project, {
        .code = true,
        .assets = false,
        .engine = false,
      });

      Utils::FS::saveTextFile(fsDataPath / "fileList.txt", fileStr);
    }
  }

  // User scripts
  auto userDirs = Utils::FS::scanDirs(path + "/src/user");
  std::string userCodeRules = "";
  for (const auto &dir : userDirs) {
    userCodeRules += "src += $(wildcard src/user/" + dir + "/*.cpp)\n";
  }

  if(!buildNodeGraphAssets(project, sceneCtx)) {
    Utils::Logger::log(std::string("Graph-Asset build failed!", Utils::Logger::LEVEL_ERROR));
    return false;
  }

  if(!buildResourceAssets(project, sceneCtx)) {
    Utils::Logger::log("Resource-Asset build failed!", Utils::Logger::LEVEL_ERROR);
    return false;
  }

  // Scripts
  buildGlobalScripts(project, sceneCtx);
  buildScripts(project, sceneCtx);
  // Must run after sceneCtx.assetList is fully populated (above) so the
  // emitted typeForAssetIdx[] aligns with the rom asset table.
  buildResourceTable(project, sceneCtx);

  // Scenes
  project.getScenes().reload();
  const auto &scenes = project.getScenes().getEntries();

  std::string sceneMapStr{};
  std::string sceneNameStr{};
  for (const auto &scene : scenes) {
    sceneMapStr += "if(path == \"" + scene.name + "\")return " + std::to_string(scene.id) + ";\n";
    sceneNameStr += "\"" + scene.name + "\",\n";
    try
    {
      buildScene(project, scene, sceneCtx);
    } catch(const std::exception &e)
    {
      Utils::Logger::log(std::string("Scene build failed: ") + e.what(), Utils::Logger::LEVEL_ERROR);
      return false;
    }
  }

  auto sceneTableHeader = Utils::replaceAll(Utils::FS::loadTextFile("data/scripts/sceneTable.h"), {
    {"{{SCENE_MAP}}", sceneMapStr},
    {"{{SCENE_COUNT}}", std::to_string(scenes.size())}
  });
  Utils::FS::saveTextFile(project.getPath() + "/src/p64/sceneTable.h", sceneTableHeader);

  Utils::FS::saveTextFile(project.getPath() + "/src/p64/sceneTable.cpp",
    "#include \"sceneTable.h\"\n"
    "\n"
    "namespace P64::SceneManager {\n"
    "  const char* SCENE_NAMES["+std::to_string(scenes.size())+"] = {\n" + sceneNameStr + "};\n"
    "}\n"
  );


  for(auto &builder : assetBuilders)
  {
    if(!builder.func(project, sceneCtx)) {
      Utils::Logger::log(std::string(builder.name) + " Asset build failed!", Utils::Logger::LEVEL_ERROR);
      return false;
    }
  }

  auto assetTableCode = Utils::replaceAll(
    Utils::FS::loadTextFile("data/scripts/assetTable.h"),
    "{{ASSET_MAP}}", sceneCtx.assetFileMap
  );
  Utils::FS::saveTextFile(project.getPath() + "/src/p64/assetTable.h", assetTableCode);

  // Asset table
  Utils::BinaryFile fileList{};
  fileList.write<uint32_t>(sceneCtx.assetList.size());
  uint32_t baseOffset = (sceneCtx.assetList.size() * sizeof(uint32_t)*2) + sizeof(uint32_t);
  for (auto &entry : sceneCtx.assetList) {
    fileList.write(baseOffset + entry.stringOffset);
    uint32_t ptr = entry.type << (32-4);
    ptr |= entry.flags << (32-8);
    fileList.write(ptr);
  }
  for (auto &entry : sceneCtx.assetList) {
    fileList.writeChars(entry.path.c_str(), entry.path.size()+1);
  }
  fileList.writeToFile(fsDataPath / "a");

  // kep order stable to detect makefile changes
  auto filesSorted = sceneCtx.files;
  std::sort(filesSorted.begin(), filesSorted.end());

  // Resolve ROM header title: explicit override or fall back to project name.
  // Libdragon's n64tool truncates to 20 chars, so do the same here so the
  // generated Makefile reflects what will actually land in the ROM header.
  std::string romTitle = project.conf.romTitle.empty() ? project.conf.name : project.conf.romTitle;
  if (romTitle.size() > 20) romTitle.resize(20);

  // Makefile
  auto makefile = Utils::replaceAll(
    Utils::FS::loadTextFile("data/build/baseMakefile.mk"),
    {
      {"{{N64_INST}}",          project.conf.pathN64Inst},
      {"{{ROM_NAME}}",          project.conf.romName},
      {"{{PROJECT_NAME}}",      project.conf.name},
      {"{{ROM_TITLE}}",         romTitle},
      {"{{ROM_SAVETYPE}}",      Project::saveTypeToMakefileString(project.conf.saveType)},
      {"{{ROM_REGIONFREE}}",    project.conf.regionFree ? "true" : "false"},
      {"{{ROM_RTC}}",           project.conf.rtcSupport ? "true" : "false"},
      {"{{ASSET_LIST}}",        Utils::join(filesSorted, " ")},
      {"{{USER_CODE_DIRS}}",    userCodeRules},
      {"{{P64_SELF_PATH}}",     Utils::Proc::getSelfPath().string()},
      {"{{PROJECT_SELF_PATH}}", fs::absolute(configPath).string()},
    }
  );

  auto mkPath = fs::absolute(path) / "Makefile";
  Utils::FS::saveTextFile(mkPath, makefile);

  {
    Utils::BinaryFile f{};
    f.write<uint32_t>(project.conf.sceneIdOnBoot);
    f.write<uint32_t>(project.conf.sceneIdOnReset);
    for(uint32_t i=0; i<sceneCtx.autoLoadFontUUIDs.size(); ++i) {
      auto uuid = sceneCtx.autoLoadFontUUIDs[i];
      f.write<uint16_t>(uuid == 0 ? 0xFFFF : sceneCtx.assetUUIDToIdx[uuid]);
    }
    f.writeToFile(fsDataPath / "conf");
  }

  // Build
  bool success = sceneCtx.toolchain.runCmdSyncLogged("make -C \"" + path + "\" -j8");

  // Surface a one-line summary if the graph validator collected anything —
  // the Compile Errors panel has the structured details, this is just the
  // glanceable count for the Log stream.
  size_t graphErrs  = ctx.compileErrors.errorCount();
  size_t graphWarns = ctx.compileErrors.warningCount();
  if(graphErrs || graphWarns) {
    Utils::Logger::log(
      "Graph validation: " + std::to_string(graphErrs) + " error(s), " +
      std::to_string(graphWarns) + " warning(s) — see Compile Errors panel.",
      graphErrs ? Utils::Logger::LEVEL_ERROR : Utils::Logger::LEVEL_WARN);
  }

  if(success) {
    Utils::Logger::log("Build done!");
  } else {
    Utils::Logger::log("Build failed!", Utils::Logger::LEVEL_ERROR);
  }
  return success;
}

bool Build::cleanProject(const Project::Project &project, const CleanArgs &args)
{
  Utils::Logger::log("Clean Project: " + project.getPath());
  fs::path projPath{project.getPath()};

  fs::remove(projPath / (project.conf.romName + ".z64"));

  if(args.assets) {
    fs::remove_all(projPath / "filesystem");
  }
  if(args.code) {
    fs::remove_all(projPath / "build");
  }
  if(args.engine) {
    fs::remove_all(projPath / "engine" / "build");
  }
  if(args.engineSrc) {
    fs::remove_all(projPath / "engine");
  }

  return true;
}


bool Build::assetBuildNeeded(const Project::AssetManagerEntry &asset, const fs::path &outPath)
{
  auto ageSrc = Utils::FS::getFileAge(asset.path);
  auto ageDst = Utils::FS::getFileAge(outPath);
  if(ageSrc < ageDst) {
    //Utils::Logger::log("Skipping Asset (up to date): " + asset.outPath);
    return false;
  }
  Utils::Logger::log("Building Asset: " + asset.path);
  return true;
}
