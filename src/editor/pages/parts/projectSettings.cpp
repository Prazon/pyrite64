/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "projectSettings.h"

#include <algorithm>
#include <filesystem>
#include "imgui.h"
#include "../../../context.h"
#include "../../../project/assetManager.h"
#include "../../../project/project.h"
#include "../../../renderer/texture.h"
#include "../../../utils/logger.h"
#include "../../../project/romMeta.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../imgui/helper.h"
#include "IconsMaterialDesignIcons.h"

namespace
{
  using namespace Project;

  // True if an image asset reference cannot be embedded as-is (missing, wrong format,
  // or larger than the 320x240 / 240x320 limit enforced by n64metadata).
  bool imageRefInvalid(uint64_t uuid, std::string &reason)
  {
    if(uuid == 0) return false;
    auto *e = ctx.project->getAssets().getEntryByUUID(uuid);
    if(!e || e->type != FileType::IMAGE) { reason = "asset missing"; return true; }

    std::string ext = std::filesystem::path(e->path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if(ext != ".png" && ext != ".jpg" && ext != ".jpeg") { reason = "not PNG/JPG"; return true; }

    int w = e->texture ? e->texture->getWidth() : 0;
    int h = e->texture ? e->texture->getHeight() : 0;
    bool okSize = (w <= 320 && h <= 240) || (w <= 240 && h <= 320);
    if(w > 0 && !okSize) {
      reason = std::to_string(w) + "x" + std::to_string(h) + " exceeds 320x240";
      return true;
    }
    return false;
  }

  // Picks an IMAGE asset for `uuid`, with a clear button and an inline warning when invalid.
  // Returns true if the clear (X) button was pressed this frame, in addition to setting uuid=0.
  bool imagePicker(const std::string &label, uint64_t &uuid)
  {
    ImTable::add(label);
    ImGui::PushID(label.c_str());

    const auto &imgs = ctx.project->getAssets().getTypeEntries(FileType::IMAGE);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28_px);
    ImTable::addAssetVecComboBox("", imgs, uuid, true);
    ImGui::SameLine();
    bool cleared = ImGui::Button(ICON_MDI_CLOSE);
    if(cleared) uuid = 0;

    std::string reason;
    if(imageRefInvalid(uuid, reason)) {
      ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, ICON_MDI_ALERT " %s", reason.c_str());
    }
    ImGui::PopID();
    return cleared;
  }

  void drawMetaLang(MetaLang &lang)
  {
    ImTable::start("MetaFields");
    ImTable::add("Name", lang.name);
    ImTable::add("Author", lang.author);
    ImTable::add("Release Date", lang.releaseDate); // YYYY-MM-DD
    ImTable::add("OSI License", lang.osiLicense);    // SPDX code, e.g. MIT
    ImTable::add("Website", lang.website);
    ImTable::add("Age Rating", lang.ageRating);      // 0-18
    ImTable::add("Short Desc", lang.shortDesc);

    ImTable::add("Long Desc");
    ImGui::InputTextMultiline("##longdesc", &lang.longDesc, ImVec2(-FLT_MIN, 80_px));

    // Screenshots: variable-length list of image refs, the X removes the whole entry.
    int removeShot = -1;
    for(size_t i = 0; i < lang.screenshots.size(); ++i) {
      ImGui::PushID((int)i);
      if(imagePicker("Screenshot " + std::to_string(i + 1), lang.screenshots[i])) removeShot = (int)i;
      ImGui::PopID();
    }
    if(removeShot >= 0) lang.screenshots.erase(lang.screenshots.begin() + removeShot);
    ImTable::add("");
    if(ImGui::Button(ICON_MDI_PLUS " Screenshot")) lang.screenshots.push_back(0);

    ImTable::end();

    if(ImGui::CollapsingSubHeader("Box Art")) {
      ImTable::start("BoxArt");
      imagePicker("Front", lang.boxFront);
      imagePicker("Back", lang.boxBack);
      imagePicker("Top", lang.boxTop);
      imagePicker("Bottom", lang.boxBottom);
      imagePicker("Left", lang.boxLeft);
      imagePicker("Right", lang.boxRight);
      ImTable::end();
    }
    if(ImGui::CollapsingSubHeader("Cart Art")) {
      ImTable::start("CartArt");
      imagePicker("Front", lang.cartFront);
      imagePicker("Back", lang.cartBack);
      ImTable::end();
    }
  }

  void drawMetadata()
  {
    auto &m = ctx.project->conf.metadata;
    ImTable::start("MetadataTop");
    ImTable::addCheckBox("Embed Metadata", m.enabled);
    ImTable::end();
    if(!m.enabled) return;

    if(ImGui::BeginTabBar("MetaLangs")) {
      int removeLang = -1;
      for(size_t i = 0; i < m.langs.size(); ++i) {
        auto &lang = m.langs[i];
        std::string tabName = lang.lang.empty() ? "Default" : lang.lang;
        if(ImGui::BeginTabItem((tabName + "##" + std::to_string(i)).c_str())) {
          if(i > 0) {
            if(ImGui::SmallButton(ICON_MDI_DELETE " Remove Language")) removeLang = (int)i;
          }
          drawMetaLang(lang);
          ImGui::EndTabItem();
        }
      }

      // "+" tab opens a popup to enter a new language code.
      if(ImGui::TabItemButton(ICON_MDI_PLUS, ImGuiTabItemFlags_Trailing)) {
        ImGui::OpenPopup("AddLang");
      }
      if(ImGui::BeginPopup("AddLang")) {
        static std::string code;
        ImGui::TextUnformatted("Language code (e.g. de, ja):");
        ImGui::InputText("##code", &code);
        if(ImGui::Button("Add") && !code.empty()) {
          MetaLang l{};
          l.lang = code;
          m.langs.push_back(l);
          code.clear();
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      if(removeLang > 0) m.langs.erase(m.langs.begin() + removeLang);
      ImGui::EndTabBar();
    }
  }

  // Validate every language's image refs (not just the visible tab) so Save can be blocked.
  bool metadataHasError()
  {
    if(!ctx.project->conf.metadata.enabled) return false;
    std::string reason;
    auto chk = [&](uint64_t u) { return imageRefInvalid(u, reason); };
    for(const auto &l : ctx.project->conf.metadata.langs) {
      if(chk(l.boxFront) || chk(l.boxBack) || chk(l.boxTop) || chk(l.boxBottom) || chk(l.boxLeft) || chk(l.boxRight)) return true;
      if(chk(l.cartFront) || chk(l.cartBack)) return true;
      for(auto u : l.screenshots) if(chk(u)) return true;
    }
    return false;
  }
}

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
    ImTable::addPref("Debug Menu (L + D-Up)", c.debugMenu, CONF_DEF.debugMenu,
      "Enable the in-game debug menu hotkey (L + D-Up).");
    if (ImTable::rowVisible("Debug Menu (L + D-Up)") && !c.debugMenu) {
      ImTable::add("");
      ImGui::TextWrapped(ICON_MDI_INFORMATION_OUTLINE
        " The in-game debug menu hotkey (L + D-Up) is disabled.\n"
        "Call P64::Debug::Overlay::toggle() from your own code to open it.");
    }

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
    auto &h = ctx.project->conf.romHeader;
    const auto &hDef = CONF_DEF.romHeader;
    ImTable::start("ROM Header");
    ImTable::addPrefCombo("Category", h.category, hDef.category,
      RomMeta::labels(RomMeta::CATEGORY),
      "ROM header category code written by n64tool.");
    ImTable::addPrefCombo("Region", h.region, hDef.region,
      RomMeta::labels(RomMeta::REGION),
      "ROM header region code written by n64tool.");
    ImTable::addPrefCombo("Save Type", h.saveType, hDef.saveType,
      RomMeta::labels(RomMeta::SAVETYPE),
      "Save hardware advertised to flashcarts (N64_ROM_SAVETYPE).");
    ImTable::addPref("Region Free", h.regionFree, hDef.regionFree,
      "Set the N64_ROM_REGIONFREE header flag.");
    ImTable::addPref("Real-Time Clock", h.rtc, hDef.rtc,
      "Set the N64_ROM_RTC header flag.");
    auto ctrlLabels = RomMeta::labels(RomMeta::CONTROLLER);
    for (int i = 0; i < 4; ++i) {
      ImTable::addPrefCombo("Controller " + std::to_string(i + 1),
        h.controllers[i], hDef.controllers[i], ctrlLabels,
        "Controller / accessory advertised for this port (ed64romconfig).");
    }
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

  void drawVisibility()
  {
    auto &c = ctx.project->conf;
    ImTable::start("Visibility");
    for (int i = 0; i < 8; ++i) {
      std::string lbl = "Layer " + std::to_string(i);
      if (ImTable::rowVisible(lbl)) {
        ImTable::add(lbl);
        ImGui::InputText(("##vis" + std::to_string(i)).c_str(), &c.visLayerNames[i]);
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
    { "metadata", "Metadata",       ICON_MDI_CARD_TEXT_OUTLINE, "Project", drawMetadata },
    { "collision","Collision",      ICON_MDI_VECTOR_SQUARE,"Project", drawCollision    },
    { "visibility","Visibility",    ICON_MDI_EYE,         "Project", drawVisibility    },
    { "env",      "Environment",    ICON_MDI_FOLDER_COG,  "Project", drawEnvironment   },
  };

  ImGui::BeginChild("body", ImVec2(0, -28_px));
  drawSettingsShell("projconf", cats, shellState);
  ImGui::EndChild();

  bool hasError = metadataHasError();

  bool res = false;
  if (hasError) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, ICON_MDI_ALERT " Fix invalid metadata images before saving");
    ImGui::SameLine();
  } else if (ctx.project->hasUnsavedConfig()) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), ICON_MDI_CIRCLE_MEDIUM " Unsaved changes");
    ImGui::SameLine();
  }
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 64_px);
  if (hasError) ImGui::BeginDisabled();
  res = ImGui::Button("Save", ImVec2(60_px, 0));
  if (hasError) ImGui::EndDisabled();

  if (res) {
    ctx.project->save();
  }
  return res;
}
