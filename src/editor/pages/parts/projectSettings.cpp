/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "projectSettings.h"

#include "imgui.h"
#include "../../../context.h"
#include "../../../project/assetManager.h"
#include "../../../project/project.h"
#include "../../../renderer/texture.h"
#include "../../../utils/logger.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../imgui/helper.h"

namespace
{
  const Project::ProjectConf CONF_DEF{};

  void drawGeneral()
  {
    auto &c = ctx.project->conf;
    ImTable::start("General");
    ImTable::addPref("Name", c.name, CONF_DEF.name,
      "Display name of the project.");
    ImTable::addPref("ROM-Name", c.romName, CONF_DEF.romName,
      "Base filename for the built .z64.");
    ImTable::addPref("Author", c.author, CONF_DEF.author,
      "Informational; stored in the .p64proj only.");
    ImTable::addPref("Version", c.version, CONF_DEF.version,
      "Informational; stored in the .p64proj only.");

    if (ImTable::prefRow("Description", "Free-form project notes.", false)) {
      ImGui::PushID("description");
      ImGui::InputTextMultiline("##", &c.description, ImVec2(-FLT_MIN, 60_px));
      ImGui::PopID();
    }

    if (ImTable::rowVisible("Game Image")) {
      auto &assets = ctx.project->getAssets();
      const auto &imageList = assets.getTypeEntries(Project::FileType::IMAGE);
      ImTable::addAssetVecComboBox("Game Image", imageList, c.gameImageUUID);
      if (auto *entry = assets.getEntryByUUID(c.gameImageUUID);
          entry && entry->type == Project::FileType::IMAGE && entry->texture) {
        ImTable::add("");
        const float maxW = 128_px;
        const float texW = (float)entry->texture->getWidth();
        const float texH = (float)entry->texture->getHeight();
        const float scale = (texW > 0.0f) ? std::min(1.0f, maxW / texW) : 1.0f;
        ImGui::Image((ImTextureID)entry->texture->getGPUTex(), ImVec2(texW * scale, texH * scale));
      }
    }
    ImTable::end();
  }

  void drawDefaultScenes()
  {
    auto &c = ctx.project->conf;
    ImTable::start("Default Scenes");
    const auto &scenes = ctx.project->getScenes().getEntries();
    if (scenes.empty()) {
      if (ImTable::rowVisible("On Boot")) {
        ImTable::add("");
        ImGui::TextDisabled("(no scenes yet)");
      }
    } else {
      if (ImTable::rowVisible("On Boot")) {
        ImTable::add("On Boot");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::VectorComboBox("##Boot", scenes, c.sceneIdOnBoot);
      }
      if (ImTable::rowVisible("On Reset")) {
        ImTable::add("On Reset");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::VectorComboBox("##Reset", scenes, c.sceneIdOnReset);
      }
    }
    ImTable::end();
  }

  void drawRomLayout()
  {
    auto &c = ctx.project->conf;
    ImTable::start("ROM Layout");
    if (ImTable::rowVisible("Cart Size")) {
      std::vector<ImTable::ComboEntry> cartSizes{};
      for (int i = 0; i < Project::CART_SIZE_COUNT; ++i) {
        cartSizes.push_back({(uint32_t)i, Project::CART_LABELS[i]});
      }
      ImTable::addVecComboBox("Cart Size", cartSizes, c.cartSize);
      ImTable::add("");
      ImGui::TextDisabled("Used by the ROM Memory Dashboard's budget bar.");
    }
    ImTable::end();
  }

  void drawRomHeader()
  {
    auto &c = ctx.project->conf;
    ImTable::start("ROM Header");
    ImTable::addPref("Title", c.romTitle, CONF_DEF.romTitle,
      "Shown in flashcart menus. Truncated to 20 chars at build time.");
    if (ImTable::rowVisible("Title")) {
      if (c.romTitle.size() > 20) {
        ImTable::add("");
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
          "Title exceeds 20 chars; will be truncated.");
      } else if (c.romTitle.empty()) {
        ImTable::add("");
        ImGui::TextDisabled("(empty: falls back to project name, max 20 chars)");
      }
    }

    if (ImTable::rowVisible("Save Type")) {
      std::vector<ImTable::ComboEntry> saveTypes{
        {(uint32_t)Project::SaveType::None,      "None"},
        {(uint32_t)Project::SaveType::EEPROM4K,  "EEPROM 4K"},
        {(uint32_t)Project::SaveType::EEPROM16K, "EEPROM 16K"},
        {(uint32_t)Project::SaveType::SRAM256K,  "SRAM 256Kbit"},
        {(uint32_t)Project::SaveType::SRAM768K,  "SRAM 768Kbit"},
        {(uint32_t)Project::SaveType::SRAM1M,    "SRAM 1Mbit"},
        {(uint32_t)Project::SaveType::FlashRAM,  "FlashRAM"},
      };
      ImTable::addVecComboBox("Save Type", saveTypes, c.saveType);
    }
    ImTable::addPref("Region-Free", c.regionFree, CONF_DEF.regionFree,
      "Set the N64_ROM_REGIONFREE header flag.");
    ImTable::addPref("RTC Support", c.rtcSupport, CONF_DEF.rtcSupport,
      "Set the N64_ROM_RTC header flag.");
    ImTable::end();
  }

  void drawCollision()
  {
    auto &c = ctx.project->conf;
    ImTable::start("Collision");
    for (int i = 0; i < 8; ++i) {
      std::string lbl = "Layer " + std::to_string(i);
      if (ImTable::rowVisible(lbl)) {
        ImTable::add(lbl);
        ImGui::InputText(("##" + std::to_string(i)).c_str(), &c.collLayerNames[i]);
      }
    }
    ImTable::end();
  }

  void drawEnvironment()
  {
    auto &c = ctx.project->conf;
    ImTable::start("Environment");
    if (ImTable::rowVisible("Emulator")) ImTable::addPath("Emulator", c.pathEmu);
    if (ImTable::rowVisible("N64_INST")) ImTable::addPath("N64_INST", c.pathN64Inst, true, "$N64_INST");
    ImTable::end();
  }
}

bool Editor::ProjectSettings::draw()
{
  static const std::vector<SettingsCategory> cats = {
    { "general",  "General",        ICON_MDI_COG,         "Project", drawGeneral       },
    { "scenes",   "Default Scenes", ICON_MDI_MOVIE_OUTLINE,"Project", drawDefaultScenes },
    { "layout",   "ROM Layout",     ICON_MDI_MEMORY,      "Project", drawRomLayout     },
    { "header",   "ROM Header",     ICON_MDI_CHIP,        "Project", drawRomHeader     },
    { "collision","Collision",      ICON_MDI_VECTOR_SQUARE,"Project", drawCollision    },
    { "env",      "Environment",    ICON_MDI_FOLDER_COG,  "Project", drawEnvironment   },
  };

  ImGui::BeginChild("body", ImVec2(0, -28_px));
  drawSettingsShell("projconf", cats, shellState);
  ImGui::EndChild();

  bool res = false;
  if (ctx.project->hasUnsavedConfig()) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), ICON_MDI_CIRCLE_MEDIUM " Unsaved changes");
    ImGui::SameLine();
  }
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 64_px);
  res = ImGui::Button("Save", ImVec2(60_px, 0));

  if (res) {
    ctx.project->save();
  }
  return res;
}
