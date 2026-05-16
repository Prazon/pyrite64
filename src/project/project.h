/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "assetManager.h"
#include "scene/sceneManager.h"

namespace Project
{
  // Save type written into the N64 ROM header by libdragon's n64tool.
  // Values match the strings n64.mk's N64_ROM_SAVETYPE accepts; 0 = "none".
  enum class SaveType : uint32_t {
    None      = 0,
    EEPROM4K  = 1,
    EEPROM16K = 2,
    SRAM256K  = 3,
    SRAM768K  = 4,
    SRAM1M    = 5,
    FlashRAM  = 6,
  };

  // Map a SaveType enum to the literal string libdragon expects in the Makefile.
  const char *saveTypeToMakefileString(uint32_t saveType);

  // Cart sizes the project can target. Today this is advisory — it colors the
  // ROM Memory Dashboard's budget bar but does not enforce a build cap.
  inline constexpr int CART_SIZE_COUNT = 4;
  inline constexpr uint64_t CART_SIZES[CART_SIZE_COUNT] = {
     8ull * 1024 * 1024,
    16ull * 1024 * 1024,
    32ull * 1024 * 1024,
    64ull * 1024 * 1024,
  };
  inline constexpr const char *CART_LABELS[CART_SIZE_COUNT] = {
    "8 MB", "16 MB", "32 MB", "64 MB"
  };

  struct ProjectConf
  {
    std::string name{};
    std::string romName{};
    std::string pathEmu{};
    std::string pathN64Inst{};
    std::string editorVersion{};

    // ROM header metadata (baked into the .z64 by libdragon)
    std::string romTitle{};       // max 20 chars; falls back to `name` when empty
    uint32_t saveType{0};         // SaveType enum
    bool regionFree{false};       // N64_ROM_REGIONFREE
    bool rtcSupport{false};       // N64_ROM_RTC

    // Project metadata (informational; stored in .p64proj only)
    std::string author{};
    std::string version{};
    std::string description{};
    uint64_t gameImageUUID{0};   // UUID of an Image asset used as box-art / banner

    uint32_t sceneIdOnBoot{1};
    uint32_t sceneIdOnReset{1};
    uint32_t sceneIdLastOpened{1};

    // Index into CART_SIZES (default 3 = 64 MB). Drives the ROM dashboard's
    // budget bar; not currently enforced by the build pipeline.
    uint32_t cartSize{3};

    std::array<std::string, 8> collLayerNames{};

    std::string serialize() const;
  };

  class Project
  {
    private:
      std::string path;
      std::string pathConfig;
      bool dirty{false};
      std::string savedState{};

      AssetManager assets{this};
      SceneManager scenes{this};

      // Watch state for files outside the assetManager's scope:
      // every data/scenes/<id>/scene.json plus the .p64proj. Keyed by
      // absolute path, value is mtime from Utils::FS::getFileAge.
      std::unordered_map<std::string, uint64_t> externalWatch{};
      std::chrono::steady_clock::time_point externalLastCheck{};
      bool externalInitialized{false};
      bool externalForceNext{false};

      void deserialize(const nlohmann::json &doc);

      // Returns true iff the project-config side is dirty (ignores assets,
      // which have their own watch + dirty tracking).
      [[nodiscard]] bool isConfigDirty() const { return dirty || conf.serialize() != savedState; }

    public:
      ProjectConf conf{};

      Project(const std::string &p64projPath);

      void save();
      void saveConfig();
      // Public view of config-only dirtiness (used by the Project Settings
      // window to show an unsaved-changes marker).
      [[nodiscard]] bool hasUnsavedConfig() const { return isConfigDirty(); }
      void markDirty() { dirty = true; }
      void markSaved() { dirty = false; savedState = conf.serialize(); }
      [[nodiscard]] bool isDirty() const { return dirty || conf.serialize() != savedState || assets.isDirty(); }

      AssetManager& getAssets() { return assets; }
      SceneManager& getScenes() { return scenes; }
      [[nodiscard]] const std::string &getPath() const { return path; }
      [[nodiscard]] const std::string &getConfigPath() const { return pathConfig; }

      // Detects external edits to scene.json files and the .p64proj.
      // Silent reload when in-memory state is clean; on conflict opens a
      // Reload / Keep mine / Cancel modal. Has an internal 2 s throttle
      // unless `forceNow` is true (used after window focus regained).
      void pollExternalChanges(bool forceNow = false);

      // Request that the next pollExternalChanges() bypasses the throttle.
      // Cheaper than calling pollExternalChanges(true) directly because the
      // actual poll still happens inside the main loop's existing slot.
      void requestExternalPoll() { externalForceNext = true; }

      // Refresh the watch snapshot for a path we just wrote ourselves, so
      // the next poll does not mistake our write for an external edit.
      void noteSelfWrite(const std::string &absPath);

      // Re-parse the .p64proj from disk into conf and reset savedState.
      // Does not touch assets/scenes (those have their own paths).
      void reloadConfigFromDisk();

  };
}