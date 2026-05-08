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

bool Editor::ProjectSettings::draw()
{
  ImGui::BeginChild("TOP", ImVec2(0, -26_px));

  if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImTable::start("General");
    ImTable::add("Name", ctx.project->conf.name);
    ImTable::add("ROM-Name", ctx.project->conf.romName);
    ImTable::add("Author", ctx.project->conf.author);
    ImTable::add("Version", ctx.project->conf.version);

    // Multiline description: render the row manually so we can use InputTextMultiline
    // (ImTable::add with std::string only exposes single-line InputText).
    ImTable::add("Description");
    ImGui::PushID("description");
    ImGui::InputTextMultiline("##", &ctx.project->conf.description, ImVec2(-FLT_MIN, 60_px));
    ImGui::PopID();

    // Game image: pick from existing IMAGE assets (drag-drop also works since
    // addAssetVecComboBox accepts ASSET payloads). Stored as a UUID so the link
    // survives renames; resolved against AssetManager at use sites (e.g. launcher).
    auto &assets = ctx.project->getAssets();
    const auto &imageList = assets.getTypeEntries(Project::FileType::IMAGE);
    ImTable::addAssetVecComboBox("Game Image", imageList, ctx.project->conf.gameImageUUID);

    // Inline preview of the picked image so the choice is verifiable here.
    if (auto *entry = assets.getEntryByUUID(ctx.project->conf.gameImageUUID);
        entry && entry->type == Project::FileType::IMAGE && entry->texture)
    {
      ImTable::add("");
      const float maxW = 128_px;
      const float texW = (float)entry->texture->getWidth();
      const float texH = (float)entry->texture->getHeight();
      const float scale = (texW > 0.0f) ? std::min(1.0f, maxW / texW) : 1.0f;
      ImGui::Image((ImTextureID)entry->texture->getGPUTex(), ImVec2(texW * scale, texH * scale));
    }

    ImTable::end();
  }

  if (ImGui::CollapsingHeader("Default Scenes", ImGuiTreeNodeFlags_DefaultOpen)) {
    // Boot/Reset selection used to live next to the assets browser; centralized
    // here so it sits with the other project-wide config and the assets browser
    // can devote its space to a Unreal-style content view.
    ImTable::start("Default Scenes");

    const auto &scenes = ctx.project->getScenes().getEntries();
    if (scenes.empty()) {
      ImTable::add("");
      ImGui::TextDisabled("(no scenes yet)");
    } else {
      ImTable::add("On Boot");
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::VectorComboBox("##Boot", scenes, ctx.project->conf.sceneIdOnBoot);

      ImTable::add("On Reset");
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::VectorComboBox("##Reset", scenes, ctx.project->conf.sceneIdOnReset);
    }
    ImTable::end();
  }

  if (ImGui::CollapsingHeader("ROM Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
    // Cart Size is advisory: it colors the ROM Memory Dashboard's budget bar
    // but is not enforced by the build pipeline. Kept here so the dashboard
    // stays a read-only view of project-level configuration.
    ImTable::start("ROM Layout");

    std::vector<ImTable::ComboEntry> cartSizes{};
    for (int i = 0; i < Project::CART_SIZE_COUNT; ++i) {
      cartSizes.push_back({(uint32_t)i, Project::CART_LABELS[i]});
    }
    ImTable::addVecComboBox("Cart Size", cartSizes, ctx.project->conf.cartSize);

    ImTable::add("");
    ImGui::TextDisabled("Used by the ROM Memory Dashboard's budget bar.");

    ImTable::end();
  }

  if (ImGui::CollapsingHeader("ROM Header", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImTable::start("ROM Header");

    // Title is what shows up in flashcart menus / homebrew launchers; libdragon
    // truncates to 20 chars at write time, so warn the user past that length.
    ImTable::add("Title", ctx.project->conf.romTitle);
    if (ctx.project->conf.romTitle.size() > 20) {
      ImTable::add("");
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
        "Title exceeds 20 chars; will be truncated.");
    } else if (ctx.project->conf.romTitle.empty()) {
      ImTable::add("");
      ImGui::TextDisabled("(empty: falls back to project name, max 20 chars)");
    }

    std::vector<ImTable::ComboEntry> saveTypes{
      {(uint32_t)Project::SaveType::None,      "None"},
      {(uint32_t)Project::SaveType::EEPROM4K,  "EEPROM 4K"},
      {(uint32_t)Project::SaveType::EEPROM16K, "EEPROM 16K"},
      {(uint32_t)Project::SaveType::SRAM256K,  "SRAM 256Kbit"},
      {(uint32_t)Project::SaveType::SRAM768K,  "SRAM 768Kbit"},
      {(uint32_t)Project::SaveType::SRAM1M,    "SRAM 1Mbit"},
      {(uint32_t)Project::SaveType::FlashRAM,  "FlashRAM"},
    };
    ImTable::addVecComboBox("Save Type", saveTypes, ctx.project->conf.saveType);

    ImTable::add("Region-Free", ctx.project->conf.regionFree);
    ImTable::add("RTC Support", ctx.project->conf.rtcSupport);
    ImTable::end();
  }

  if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImTable::start("Collision");

    ImTable::add("Layer Names");
    for(int i=0; i<8; ++i) {
      ImTable::add("Layer " + std::to_string(i));
      ImGui::InputText(("##" + std::to_string(i)).c_str(), &ctx.project->conf.collLayerNames[i]);
    }
    ImTable::end();
  }

  if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImTable::start("Environment");
    ImTable::addPath("Emulator", ctx.project->conf.pathEmu);
    ImTable::addPath("N64_INST", ctx.project->conf.pathN64Inst, true, "$N64_INST");
    ImTable::end();
  }

  ImGui::EndChild();

  ImGui::BeginChild("BOTTOM", ImVec2(0, 24_px));
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 64_px);

    bool res = ImGui::Button("Save", ImVec2(60_px, 0));
  ImGui::EndChild();

  if (res) {
    ctx.project->save();
  }
  return res;
}
