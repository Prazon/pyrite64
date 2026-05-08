/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <string>

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

      void deserialize(const nlohmann::json &doc);

    public:
      ProjectConf conf{};

      Project(const std::string &p64projPath);

      void save();
      void saveConfig();
      void markDirty() { dirty = true; }
      void markSaved() { dirty = false; savedState = conf.serialize(); }
      [[nodiscard]] bool isDirty() const { return dirty || conf.serialize() != savedState || assets.isDirty(); }

      AssetManager& getAssets() { return assets; }
      SceneManager& getScenes() { return scenes; }
      [[nodiscard]] const std::string &getPath() const { return path; }
      [[nodiscard]] const std::string &getConfigPath() const { return pathConfig; }

  };
}