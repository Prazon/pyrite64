/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "assetsBrowser.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "../../imgui/helper.h"
#include "../../imgui/notification.h"
#include "../../../context.h"
#include "../editorScene.h"
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <SDL3/SDL.h>
#include <string>
#include "../../../utils/logger.h"
#include "../../../utils/proc.h"
#include "../../../utils/fs.h"
#include "../../../utils/filePicker.h"
#include "../../../utils/hash.h"
#include "../../../project/scene/prefab.h"
#include "../../../project/prefabFunctions.h"

using FileType = Project::FileType;
namespace fs = std::filesystem;

namespace
{
  using ChipKind = Editor::AssetsBrowser::ChipKind;

  // Two physical roots are mirrored as a single virtual "Content" tree. Code
  // routes into src/user, everything else into assets/. Per CLAUDE.md this
  // split is enforced by libdragon's source-vs-data build pipeline, so the
  // mirror is the closest we can get to a unified root without forking
  // n64.mk.
  struct ChipDef {
    const char* name;
    const char* icon;
    FileType    type;          // FileType::UNKNOWN for chips with no AssetManager type (Scenes)
    bool        underSrcUser;  // true → src/user/, false → assets/
  };

  // Order matches Editor::AssetsBrowser::ChipKind. The table is addressable
  // by the same enum the header defines so callers can index chips[CHIP_X]
  // and CHIP_DEFS[CHIP_X] without drift.
  constexpr std::array<ChipDef, ChipKind::CHIP_COUNT> CHIP_DEFS = {
    ChipDef{ "Scenes",      ICON_MDI_EARTH_BOX,                FileType::UNKNOWN,    false },
    ChipDef{ "Prefabs",     ICON_MDI_PACKAGE_VARIANT_CLOSED,   FileType::PREFAB,     false },
    ChipDef{ "Images",      ICON_MDI_FILE_IMAGE_OUTLINE,       FileType::IMAGE,      false },
    ChipDef{ "Models",      ICON_MDI_CUBE_OUTLINE,             FileType::MODEL_3D,   false },
    ChipDef{ "Audio",       ICON_MDI_MUSIC,                    FileType::AUDIO,      false },
    ChipDef{ "Music (XM)",  ICON_MDI_PIANO,                    FileType::MUSIC_XM,   false },
    ChipDef{ "Fonts",       ICON_MDI_FORMAT_FONT,              FileType::FONT,       false },
    ChipDef{ "Scripts",     ICON_MDI_LANGUAGE_CPP,             FileType::CODE_OBJ,   true  },
    ChipDef{ "Globals",     ICON_MDI_SCRIPT_OUTLINE,           FileType::CODE_GLOBAL,true  },
    ChipDef{ "Node Graphs", ICON_MDI_GRAPH_OUTLINE,            FileType::NODE_GRAPH, false },
    ChipDef{ "Res. Types",  ICON_MDI_DATABASE_OUTLINE,         FileType::RESOURCE_TYPE,     true  },
    ChipDef{ "Resources",   ICON_MDI_DATABASE_EDIT_OUTLINE,    FileType::RESOURCE_INSTANCE, false },
    ChipDef{ "Materials",   ICON_MDI_PALETTE_SWATCH,           FileType::MATERIAL,          false },
  };

  std::string normalizeDir(std::string dir)
  {
    for (auto &c : dir) {
      if (c == '\\') c = '/';
    }
    while (!dir.empty() && dir.front() == '/') dir.erase(dir.begin());
    while (!dir.empty() && dir.back() == '/') dir.pop_back();
    return dir;
  }

  std::string joinDir(const std::string &left, const std::string &right)
  {
    if (left.empty()) return right;
    if (right.empty()) return left;
    return left + "/" + right;
  }

  // Resolve the absolute physical path for one half of the virtual tree.
  fs::path physicalRoot(bool srcUser)
  {
    fs::path base = fs::path(ctx.project->getPath()) / (srcUser ? "src/user" : "assets");
    std::error_code ec;
    auto abs = fs::absolute(base, ec);
    return ec ? base : abs;
  }

  // Per-tab filter state held outside the AssetsBrowser instance because the
  // popup dispatch decouples the menu click from the popup body that consumes
  // these. Deliberately not class members — the script popup is a singleton
  // anyway.
  std::string scriptName{};
  int scriptType{0};
  std::string newScriptDir{};

  // "Create Resource Instance" popup state — same singleton-popup rationale
  // as the script popup above.
  std::string newResourceName{};
  std::string newResourceDir{};
  uint64_t newResourceTypeUUID{0};
}

void Editor::AssetsBrowser::draw() {
  if (!ctx.project) return;
  auto &scenes   = ctx.project->getScenes().getEntries();
  auto &assetMgr = ctx.project->getAssets();

  const bool splitMode = (ctx.prefs.contentBrowserMode == Editor::ContentBrowserMode::Split);

  // In Split mode each tab keeps its own nav state, so currentDir aliases
  // tabDirs[activeTab] for the duration of the frame and is written back
  // before exit. The unified-mode currentDir is preserved across mode flips.
  if (splitMode) currentDir = tabDirs[activeTab];
  currentDir = normalizeDir(currentDir);

  // Tab-scoped chip mask. In Unified mode we read the user's persistent
  // chip toggles; in Split mode the active tab implies which chips matter
  // and chips[] is hidden. Computed locally so toggling between modes
  // doesn't clobber unified-mode chip state.
  std::array<bool, ChipKind::CHIP_COUNT> activeChips{};
  if (splitMode) {
    activeChips.fill(false);
    switch (activeTab) {
      case TAB_SCENES:
        activeChips[CHIP_SCENES] = true;
        break;
      case TAB_ASSETS:
        activeChips[CHIP_PREFABS]            = true;
        activeChips[CHIP_IMAGES]             = true;
        activeChips[CHIP_MODELS]             = true;
        activeChips[CHIP_AUDIO]              = true;
        activeChips[CHIP_MUSIC_XM]           = true;
        activeChips[CHIP_FONTS]              = true;
        activeChips[CHIP_RESOURCE_INSTANCE]  = true;
        activeChips[CHIP_MATERIAL]           = true;
        break;
      case TAB_SCRIPTS:
        activeChips[CHIP_CODE_OBJ]      = true;
        activeChips[CHIP_CODE_GLOBAL]   = true;
        activeChips[CHIP_NODE_GRAPH]    = true;
        activeChips[CHIP_RESOURCE_TYPE] = true;
        break;
      case TAB_PREFABS:
        activeChips[CHIP_PREFABS] = true;
        break;
    }
  } else {
    activeChips = chips;
  }

  fs::path assetsRootAbs  = physicalRoot(false);
  fs::path scriptsRootAbs = physicalRoot(true);

  // Both physical roots' current-dir paths. Either may not exist on disk
  // yet (legacy projects that only ever wrote to one root). All listing
  // tolerates that.
  fs::path assetsCurAbs  = assetsRootAbs  / currentDir;
  fs::path scriptsCurAbs = scriptsRootAbs / currentDir;

  // ── LEFT: filter chip rail (Unified mode only) ───────────────────────
  // Resizable via the splitter button between LEFT and RIGHT below; the
  // pattern mirrors PrefabEditor's drawSplitter (assets/prefabEditor.cpp).
  // Hidden entirely in Split mode — the tab strip drives content scoping.
  constexpr float SPLITTER_W   = 4.0f;
  constexpr float CHIP_MIN_W   = 60.0f;
  constexpr float CHIP_MAX_PAD = 200.0f; // leave at least this much for the grid
  float fullAvail = ImGui::GetContentRegionAvail().x;
  float availWidth;

  if (!splitMode) {
    chipPanelWidth = ImClamp(chipPanelWidth, CHIP_MIN_W,
                             std::max(CHIP_MIN_W, fullAvail - CHIP_MAX_PAD - SPLITTER_W));

    ImGui::BeginChild("LEFT", ImVec2(chipPanelWidth, 0), ImGuiChildFlags_Borders);
    for (int i = 0; i < ChipKind::CHIP_COUNT; ++i) {
      const auto &def = CHIP_DEFS[i];
      bool on = chips[i];
      std::string label = std::string(def.icon) + "  " + def.name;
      // Selectable's "selected" state stands in for "filter on". Right-click
      // pops the solo / show-all menu.
      if (ImGui::Selectable((label + "##chip").c_str(), on)) {
        chips[i] = !on;
      }
      if (ImGui::BeginPopupContextItem(("chipctx" + std::to_string(i)).c_str())) {
        if (ImGui::MenuItem("Solo")) {
          for (int j = 0; j < ChipKind::CHIP_COUNT; ++j) chips[j] = (j == i);
        }
        if (ImGui::MenuItem("Show All")) {
          chips.fill(true);
        }
        ImGui::EndPopup();
      }
    }
    ImGui::EndChild();

    // Vertical splitter — drag to resize the chip rail. Styled like
    // ImGuiCol_Separator so it visually reads as a divider, not a button.
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImGui::GetStyleColorVec4(ImGuiCol_Separator));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
    ImGui::Button("##chipSplitter", ImVec2(SPLITTER_W, -1));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActive()) {
      chipPanelWidth = ImClamp(chipPanelWidth + ImGui::GetIO().MouseDelta.x,
                               CHIP_MIN_W,
                               std::max(CHIP_MIN_W, fullAvail - CHIP_MAX_PAD - SPLITTER_W));
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    availWidth = ImGui::GetContentRegionAvail().x - 24_px - chipPanelWidth - SPLITTER_W;
    ImGui::SameLine();
  } else {
    availWidth = fullAvail - 24_px;
  }

  // ── RIGHT: optional tab bar + breadcrumb + grid ──────────────────────
  ImGui::BeginChild("RIGHT");

  // Split-mode tab strip. Each tab pulls/pushes its own currentDir from
  // tabDirs so per-tab navigation is sticky.
  if (splitMode) {
    tabDirs[activeTab] = currentDir;
    if (ImGui::BeginTabBar("##cbTabs", ImGuiTabBarFlags_None)) {
      auto tabItem = [&](const char* label, int idx) {
        if (ImGui::BeginTabItem(label)) {
          if (activeTab != idx) {
            activeTab = idx;
            currentDir = normalizeDir(tabDirs[idx]);
          }
          ImGui::EndTabItem();
        }
      };
      tabItem(ICON_MDI_EARTH_BOX " Scenes",                 TAB_SCENES);
      tabItem(ICON_MDI_FILE_IMAGE_OUTLINE " Assets",        TAB_ASSETS);
      tabItem(ICON_MDI_LANGUAGE_CPP " Scripts",             TAB_SCRIPTS);
      tabItem(ICON_MDI_PACKAGE_VARIANT_CLOSED " Prefabs",   TAB_PREFABS);
      ImGui::EndTabBar();
    }
  }

  // Breadcrumb + search (Content / sub / dir)
  {
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetColorU32(ImGuiCol_ButtonHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImGui::GetColorU32(ImGuiCol_WindowBg));

    ImGui::BeginChild("PATH", ImVec2(0, 21_px), 0,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove
    );
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4_px, 3_px));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(4_px, 4_px));

    if (ImGui::Button(ICON_MDI_FOLDER " Content")) {
      currentDir.clear();
    }

    std::vector<std::string> crumbParts{};
    if (!currentDir.empty()) {
      size_t start = 0;
      while (start < currentDir.size()) {
        size_t sep = currentDir.find('/', start);
        if (sep == std::string::npos) sep = currentDir.size();
        crumbParts.push_back(currentDir.substr(start, sep - start));
        start = sep + 1;
      }
    }
    std::string accum{};
    for (const auto &part : crumbParts) {
      ImGui::SameLine();
      ImGui::TextUnformatted("/");
      ImGui::SameLine();
      accum = joinDir(accum, part);
      if (ImGui::Button(part.c_str())) {
        currentDir = accum;
      }
    }
    ImGui::PopStyleVar(2);

    ImGui::SameLine();
    // Reserve trailing space for: search box + gap + kebab button + edge gap.
    constexpr float SEARCH_W = 160.0f;
    constexpr float KEBAB_W  = 22.0f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX()
                         - SEARCH_W - 4_px - KEBAB_W - 2_px);
    ImGui::SetNextItemWidth(SEARCH_W);
    ImGui::InputTextWithHint("##search", "Filter...", &searchFilter);

    ImGui::SameLine();
    if (ImGui::Button(ICON_MDI_DOTS_VERTICAL "##cbSettings", ImVec2(KEBAB_W, 0))) {
      ImGui::OpenPopup("ContentBrowserSettings");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("View options");

    if (ImGui::BeginPopup("ContentBrowserSettings")) {
      ImGui::TextDisabled("View Mode");
      ImGui::Separator();
      auto modeRadio = [&](const char* label, Editor::ContentBrowserMode m) {
        bool selected = (ctx.prefs.contentBrowserMode == m);
        if (ImGui::MenuItem(label, nullptr, selected)) {
          if (!selected) {
            ctx.prefs.contentBrowserMode = m;
            ctx.prefs.save();
          }
        }
      };
      modeRadio("Unified",     Editor::ContentBrowserMode::Unified);
      modeRadio("Split (Tabs)", Editor::ContentBrowserMode::Split);
      ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(3);
  }

  ImGui::BeginChild("ASSETS");

  float imageSize  = 64_px;
  float itemWidth  = imageSize + 18_px;
  float currentWid = 0.0f;
  ImVec2 textBtnSize{imageSize + 12_px, imageSize + 8_px};

  float cursorStartX = ImGui::GetCursorPosX();
  float cursorY      = ImGui::GetCursorPosY();

  auto checkLineBreak = [&]() {
    if ((currentWid + itemWidth*2) > availWidth) {
      currentWid = 0.0f;
      cursorY += imageSize + 28_px;
      ImGui::SetCursorPos({cursorStartX, cursorY});
    } else {
      if (currentWid != 0) ImGui::SameLine();
    }
    currentWid += itemWidth;
  };

  auto drawRename = [&](const std::string &label, const ImVec2 &startPos) {
    ImVec2 rectMin{startPos.x,                  startPos.y + imageSize + 8};
    ImVec2 rectMax{startPos.x + imageSize+14_px, startPos.y + imageSize + 8_px + 16_px};

    ImVec2 originalCursor = ImGui::GetCursorPos();
    ImGui::SetCursorScreenPos(rectMin);
    ImGui::SetNextItemWidth(rectMax.x - rectMin.x);
    if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2_px, 0));
    if (ImGui::InputText("##renameInput", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
      fs::path oldPath = renamePath;
      bool isDir = fs::is_directory(oldPath);

      if (isDir) {
        // Folder rename: cross-root mirror. Apply to whichever physical
        // sides exist, then rewrite scene relPaths under the old prefix.
        std::string newName = renameBuffer;
        if (!newName.empty() && newName != oldPath.filename().string()) {
          fs::path parent = oldPath.parent_path();
          fs::path newAbs = parent / newName;

          // Determine which physical root this anchor is in so we can
          // rename the mirror on the other side too.
          std::error_code ec;
          fs::path otherOld, otherNew;
          {
            auto lex = oldPath.lexically_relative(assetsRootAbs);
            if (!lex.empty() && !lex.string().starts_with("..")) {
              otherOld = scriptsRootAbs / lex;
              otherNew = scriptsRootAbs / lex.parent_path() / newName;
            } else {
              auto lex2 = oldPath.lexically_relative(scriptsRootAbs);
              if (!lex2.empty() && !lex2.string().starts_with("..")) {
                otherOld = assetsRootAbs / lex2;
                otherNew = assetsRootAbs / lex2.parent_path() / newName;
              }
            }
          }

          fs::rename(oldPath, newAbs, ec);
          if (ec) Utils::Logger::log("Rename failed: " + ec.message(), Utils::Logger::LEVEL_ERROR);

          if (!otherOld.empty() && fs::exists(otherOld)) {
            std::error_code ec2;
            fs::rename(otherOld, otherNew, ec2);
            if (ec2) Utils::Logger::log("Mirror rename failed: " + ec2.message(), Utils::Logger::LEVEL_ERROR);
          }

          // Compute the virtual prefix swap and rewrite scene relPaths.
          std::string oldVirt = joinDir(currentDir, oldPath.filename().string());
          std::string newVirt = joinDir(currentDir, newName);
          ctx.project->getScenes().renameSceneFolder(oldVirt, newVirt);
        }
      } else {
        // File rename: preserve the existing extension and any sidecar
        // .conf the asset pipeline owns.
        std::string newFileName = std::string(renameBuffer) + oldPath.extension().string();
        fs::path newPath = oldPath.parent_path() / newFileName;

        std::error_code ec;
        if (oldPath != newPath) {
          if (fs::exists(newPath)) {
            Utils::Logger::log("A file with that name already exists.", Utils::Logger::LEVEL_ERROR);
          } else {
            fs::rename(oldPath, newPath, ec);
            if (ec) Utils::Logger::log("Rename failed: " + ec.message(), Utils::Logger::LEVEL_ERROR);
            else {
              fs::path oldConf = oldPath.string() + ".conf";
              fs::path newConf = newPath.string() + ".conf";
              if (fs::exists(oldConf)) {
                fs::rename(oldConf, newConf, ec);
                if (ec) Utils::Logger::log("Failed to move .conf: " + ec.message(), Utils::Logger::LEVEL_ERROR);
              }
            }
          }
        }
      }
      renamePath.clear();
    }
    ImGui::PopStyleVar();

    if ((!ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      renamePath.clear();
    }

    ImGui::SetCursorPos(originalCursor);
  };

  auto drawLabel = [&](const std::string &label, const ImVec2 &startPos) {
    auto size = ImGui::CalcTextSize(label.c_str());
    ImVec2 rextMin{startPos.x,                   startPos.y + imageSize + 8_px};
    ImVec2 rextMax{startPos.x + imageSize+14_px, startPos.y + imageSize + 8_px + 16_px};

    if((size.x+3_px) > (rextMax.x - rextMin.x))
    {
      ImGui::RenderTextEllipsis(
        ImGui::GetWindowDrawList(), rextMin, rextMax, 0,
        label.c_str(), label.c_str() + label.size(),
        nullptr
      );
    } else {
      ImGui::GetWindowDrawList()->AddText(
        {rextMin.x + ((rextMax.x - rextMin.x) - size.x) * 0.5f,
         rextMin.y + ((rextMax.y - rextMin.y) - size.y) * 0.5f},
        ImGui::GetColorU32(ImGuiCol_Text),
        label.c_str()
      );
    }
  };

  auto drawGridButton = [&](const std::string &id, ImTextureRef icon, const char* iconTxt,
    const std::string &label, bool selected, float alpha) {
    bool clicked = false;
    if(selected) {
      ImGui::PushStyleColor(ImGuiCol_Button,        {0.5f,0.5f,0.7f,1});
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.5f,0.5f,0.7f,0.8f});
    }

    ImGui::PushID(id.c_str());
    auto sPos = ImGui::GetCursorScreenPos();
    bool isRenaming = id == renamePath;
    if (isRenaming) drawRename(label, sPos);
    else drawLabel(label, sPos);

    if(icon._TexID)
    {
      clicked = ImGui::ImageButton("##img", icon,
        {imageSize, imageSize}, {0,0}, {1,1}, {0,0,0,0},
        {1,1,1, alpha}
      );
    } else {
      ImGui::PushFont(nullptr, 40_px);
      clicked = ImGui::Button(iconTxt, textBtnSize);
      ImGui::PopFont();
    }

    ImGui::PopID();

    if(selected) ImGui::PopStyleColor(2);
    return clicked && !isRenaming;
  };

  // ── Collect items at currentDir ──────────────────────────────────────
  // Folders (union of both physical roots), filtered to direct children.
  std::vector<std::string> folders{};
  std::unordered_set<std::string> folderSet{};
  std::unordered_map<std::string, bool> folderHasContent{};

  auto addFoldersFromRoot = [&](const fs::path &absRoot) {
    if (absRoot.empty()) return;
    std::error_code ec;
    for (auto it = fs::directory_iterator(absRoot, ec);
         !ec && it != fs::directory_iterator();
         it.increment(ec))
    {
      const auto &entry = *it;
      if (!entry.is_directory()) continue;
      auto name = entry.path().filename().string();
      // Hide the generated outputs directory at the Content root.
      if (currentDir.empty() && name == "p64") continue;
      if (folderSet.insert(name).second) folders.push_back(name);
    }
  };
  addFoldersFromRoot(assetsCurAbs);
  addFoldersFromRoot(scriptsCurAbs);

  // Files at currentDir (one pass per enabled chip's FileType). Also flag
  // folders that contain matching content so the icon is filled vs outline.
  std::vector<const Project::AssetManagerEntry*> assetItems{};
  for (int chipIdx = 0; chipIdx < ChipKind::CHIP_COUNT; ++chipIdx) {
    if (!activeChips[chipIdx]) continue;
    const auto &def = CHIP_DEFS[chipIdx];
    if (def.type == FileType::UNKNOWN) continue; // Scenes handled separately

    fs::path rootAbs = physicalRoot(def.underSrcUser);
    for (const auto &asset : assetMgr.getTypeEntries(def.type)) {
      std::error_code ec;
      auto absPath = fs::absolute(fs::path(asset.path), ec);
      if (ec) absPath = fs::path(asset.path);
      auto rel = absPath.lexically_relative(rootAbs).generic_string();
      if (rel == ".") continue;
      if (rel.starts_with("..")) continue;

      if (!currentDir.empty()) {
        auto prefix = currentDir + "/";
        if (!rel.starts_with(prefix)) continue;
        rel = rel.substr(prefix.size());
      }

      auto slashPos = rel.find('/');
      if (slashPos != std::string::npos) {
        // The asset is inside a subfolder; mark that folder as filled.
        folderHasContent[rel.substr(0, slashPos)] = true;
      } else {
        assetItems.push_back(&asset);
      }
    }
  }

  std::sort(folders.begin(), folders.end());
  std::sort(assetItems.begin(), assetItems.end(),
    [](const auto *a, const auto *b) { return a->name < b->name; });

  // Scenes at currentDir (chip-gated). ID-keyed flat pool — relPath places
  // them virtually without changing on-disk layout (data/scenes/<id>/).
  std::vector<const Project::SceneEntry*> sceneItems{};
  if (activeChips[ChipKind::CHIP_SCENES]) {
    for (const auto &sc : scenes) {
      if (sc.relPath != currentDir) continue;
      sceneItems.push_back(&sc);
    }
  }

  // ── Render: scenes → folders → files ─────────────────────────────────
  static int  ctxSceneId = -1;
  // Track whether we accepted a SCENE drag-drop this frame so we can apply
  // it after the loop without invalidating iteration.
  pendingSceneMoveId = 0;
  pendingSceneMoveTarget.clear();

  if (activeChips[ChipKind::CHIP_SCENES])
  {
    for (const auto *scPtr : sceneItems)
    {
      const auto &scene = *scPtr;
      auto activeScene = ctx.project->getScenes().getLoadedScene();

      bool isLoaded = activeScene && (activeScene->getId() == scene.id);
      bool isUserSel = (selectedSceneId == scene.id);
      bool isHighlighted = isLoaded || isUserSel;
      const auto &liveName = isLoaded ? activeScene->getName() : scene.name;
      const auto &displayName = liveName.empty() ? "(unnamed)" : liveName;

      if (!searchFilter.empty() && displayName.find(searchFilter) == std::string::npos) continue;

      checkLineBreak();

      if (isHighlighted) {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.5f,0.5f,0.7f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.5f,0.5f,0.7f,0.8f});
      }

      ImGui::PushID(scene.id);
      ImVec2 lblPos = ImGui::GetCursorScreenPos();
      drawLabel(displayName, lblPos);

      ImGui::PushFont(nullptr, 40_px);
      bool pressed = ImGui::Button(ICON_MDI_EARTH_BOX, textBtnSize);
      ImGui::PopFont();
      bool isDblClick = ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered();
      ImGui::PopID();

      if (pressed) {
        // Single-click: select only. Clears asset/folder selection so the
        // browser shows one focused item at a time.
        selectedSceneId = scene.id;
        selectedFolder.clear();
        ctx.selAssetUUID = 0;
      }
      if (isDblClick) {
        ctx.project->getScenes().loadScene(scene.id);
        ctx.project->conf.sceneIdLastOpened = scene.id;
        ctx.project->saveConfig();
      }

      if (isHighlighted) ImGui::PopStyleColor(2);

      if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("SCENE", &scene.id, sizeof(scene.id));
        ImGui::TextUnformatted(displayName.c_str());
        ImGui::EndDragDropSource();
      }

      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Scene: %s\nID: %d\n\nDrag onto a folder to move\nRight-click for options",
                          displayName.c_str(), scene.id);
      }

      if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        ctxSceneId = scene.id;
        ImGui::OpenPopup("SceneCtxMenu");
      }
    }

    if (ImGui::BeginPopup("SceneCtxMenu")) {
      bool canDelete = scenes.size() > 1;

      if (ImGui::MenuItem(ICON_MDI_CONTENT_COPY " Duplicate")) {
        ctx.project->getScenes().duplicate(ctxSceneId);
      }
      if (ImGui::MenuItem(ICON_MDI_FOLDER_OPEN " Move to root")) {
        ctx.project->getScenes().setSceneRelPath(ctxSceneId, "");
      }
      if (!canDelete) ImGui::BeginDisabled();
      if (ImGui::MenuItem(ICON_MDI_TRASH_CAN_OUTLINE " Delete")) {
        ctx.project->getScenes().remove(ctxSceneId);
        const auto &after = ctx.project->getScenes().getEntries();
        ctx.project->conf.sceneIdLastOpened = after.empty() ? 0 : after.front().id;
        ctx.project->saveConfig();
      }
      if (!canDelete) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
        }
        ImGui::EndDisabled();
      }
      ImGui::EndPopup();
    }
  }

  // Folders
  for (const auto &folder : folders) {
    if (!searchFilter.empty() && folder.find(searchFilter) == std::string::npos) continue;

    checkLineBreak();
    // Build a unique virtual id so PushID/popups don't collide with files of
    // the same name. The id also doubles as the folder's virtual rel path.
    std::string virtChild = joinDir(currentDir, folder);
    std::string folderId  = "folder://" + virtChild;

    bool filled = folderHasContent[folder];
    const char* folderIcon = filled ? ICON_MDI_FOLDER : ICON_MDI_FOLDER_OUTLINE;
    bool isFolderSel = (selectedFolder == virtChild);

    bool clicked = drawGridButton(folderId, ImTextureRef(nullptr), folderIcon, folder, isFolderSel, 1.0f);
    bool isDblClick = ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered();

    // Drag-drop target: a scene dropped here moves to this folder.
    if (ImGui::BeginDragDropTarget()) {
      if (const auto *payload = ImGui::AcceptDragDropPayload("SCENE")) {
        int sid = *static_cast<const int*>(payload->Data);
        pendingSceneMoveId = sid;
        pendingSceneMoveTarget = virtChild;
      }
      ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem(folderId.c_str())) {
      // Resolve which physical sides this virtual folder lives on.
      fs::path assetSide  = assetsRootAbs  / virtChild;
      fs::path scriptSide = scriptsRootAbs / virtChild;
      bool hasAssetSide  = fs::exists(assetSide);
      bool hasScriptSide = fs::exists(scriptSide);

      if (!hasAssetSide) ImGui::BeginDisabled();
      if (ImGui::MenuItem(ICON_MDI_FOLDER_OPEN " Show Assets folder")) {
        Utils::Proc::openInFileBrowser(assetSide.string());
      }
      if (!hasAssetSide) ImGui::EndDisabled();

      if (!hasScriptSide) ImGui::BeginDisabled();
      if (ImGui::MenuItem(ICON_MDI_FOLDER_OPEN " Show Scripts folder")) {
        Utils::Proc::openInFileBrowser(scriptSide.string());
      }
      if (!hasScriptSide) ImGui::EndDisabled();

      ImGui::Separator();
      if (ImGui::MenuItem(ICON_MDI_RENAME " Rename")) {
        // Pick whichever side actually exists as the rename anchor; the
        // mirror is renamed in lockstep inside drawRename().
        renamePath = (hasAssetSide ? assetSide : scriptSide).string();
        std::string stem = folder;
        strncpy(renameBuffer, stem.c_str(), sizeof(renameBuffer) - 1);
        renameBuffer[sizeof(renameBuffer) - 1] = '\0';
      }
      if (ImGui::MenuItem(ICON_MDI_DELETE " Delete")) {
        deleteFolderPath = virtChild;
      }
      ImGui::EndPopup();
    }

    if (clicked) {
      // Single-click: highlight only. Double-click navigates in.
      selectedFolder = virtChild;
      selectedSceneId = 0;
      ctx.selAssetUUID = 0;
    }
    if (isDblClick) {
      currentDir = virtChild;
      selectedFolder.clear();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Folder: %s\n(double-click to open)", virtChild.c_str());
    }
  }

  // Files
  for (const auto *assetPtr : assetItems)
  {
    const auto &asset = *assetPtr;
    if (!searchFilter.empty() && asset.name.find(searchFilter) == std::string::npos) continue;

    checkLineBreak();

    auto icon = ImTextureRef(nullptr);
    const char* iconTxt = ICON_MDI_FILE_OUTLINE;
    if (asset.texture) {
      icon = ImTextureRef(asset.texture->getGPUTex());
    } else {
      switch (asset.type) {
        case FileType::MODEL_3D:    iconTxt = ICON_MDI_CUBE_OUTLINE;             break;
        case FileType::AUDIO:       iconTxt = ICON_MDI_MUSIC;                    break;
        case FileType::MUSIC_XM:    iconTxt = ICON_MDI_PIANO;                    break;
        case FileType::FONT:        iconTxt = ICON_MDI_FORMAT_FONT;              break;
        case FileType::PREFAB:      iconTxt = ICON_MDI_PACKAGE_VARIANT_CLOSED;   break;
        case FileType::CODE_OBJ:    iconTxt = ICON_MDI_LANGUAGE_CPP;             break;
        case FileType::CODE_GLOBAL: iconTxt = ICON_MDI_SCRIPT_OUTLINE;           break;
        case FileType::NODE_GRAPH:  iconTxt = ICON_MDI_GRAPH_OUTLINE;            break;
        case FileType::MATERIAL:    iconTxt = ICON_MDI_PALETTE_SWATCH;           break;
        default: break;
      }
    }

    bool isSelected = (ctx.selAssetUUID == asset.getUUID());
    bool clicked = drawGridButton(
      asset.path,
      icon,
      iconTxt,
      asset.name,
      isSelected,
      asset.conf.exclude ? 0.25f : 1.0f
    );
    bool isDblClick = ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered();

    if (clicked) {
      ctx.selAssetUUID = asset.getUUID();
      selectedFolder.clear();
      selectedSceneId = 0;
      ImGui::makeTabVisible("Asset");
    }
    if (isDblClick) {
      bool handled = false;
      if (ctx.editorScene) {
        if (asset.type == FileType::IMAGE) {
          ctx.editorScene->openImageEditor(asset.getUUID());
          handled = true;
        } else if (asset.type == FileType::MODEL_3D) {
          ctx.editorScene->openModelEditor(asset.getUUID());
          handled = true;
        } else if (asset.type == FileType::CODE_OBJ
                || asset.type == FileType::CODE_GLOBAL) {
          ctx.editorScene->openCodeEditor(asset.getUUID());
          handled = true;
        } else if (asset.type == FileType::PREFAB) {
          ctx.editorScene->openPrefabEditor(asset.getUUID());
          handled = true;
        } else if (asset.type == FileType::MATERIAL) {
          ctx.editorScene->openMaterialEditor(asset.getUUID());
          handled = true;
        }
      }
      if (!handled && !Utils::Proc::openFile(asset.path)) {
        Editor::Noti::add(Editor::Noti::Type::ERROR,
          "Failed to open File. This may be due to WSL path conversion failure.");
      }
    }

    if (ImGui::BeginDragDropSource()) {
      if (icon._TexID) {
        ImGui::ImageButton(asset.name.c_str(), icon, {imageSize*0.75f, imageSize*0.75f});
      } else {
        ImGui::Button(iconTxt, textBtnSize);
      }
      ImGui::SetDragDropPayload("ASSET", &asset.conf.uuid, sizeof(asset.conf.uuid));
      ImGui::EndDragDropSource();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      auto tooltipPath = currentDir.empty() ? asset.name : (currentDir + "/" + asset.name);
      ImGui::SetTooltip("File: %s", tooltipPath.c_str());
    }

    if (ImGui::BeginPopupContextItem(asset.path.c_str())) {
      if (asset.type == FileType::PREFAB && asset.prefab) {
        if (ImGui::MenuItem(ICON_MDI_PACKAGE_VARIANT_CLOSED_PLUS " Create Variant")) {
          fs::path srcPath{asset.path};
          fs::path dir = srcPath.parent_path();
          std::string baseStem = srcPath.stem().string() + "_Variant";
          fs::path outPath;
          for (int i = 0; i < 1000; ++i) {
            std::string stem = (i == 0) ? baseStem : (baseStem + "_" + std::to_string(i + 1));
            fs::path candidate = dir / (stem + ".prefab");
            std::error_code ec;
            if (!fs::exists(candidate, ec)) { outPath = candidate; break; }
          }
          if (outPath.empty()) outPath = dir / (baseStem + ".prefab");

          Project::Prefab variant{};
          variant.uuid.value = Utils::Hash::randomU64();
          variant.uuidParentPrefab.value = asset.prefab->uuid.value;
          variant.obj = Project::Object{};
          variant.obj.name = outPath.stem().string();
          variant.patchOps = nlohmann::json::array();

          Utils::FS::saveTextFile(outPath.string(), variant.serialize());
          ctx.project->getAssets().reload();

          if (ctx.editorScene) {
            auto* entry = ctx.project->getAssets().getByPath(outPath.string());
            if (entry) ctx.editorScene->openPrefabEditor(entry->getUUID());
          }
        }
      }
      showContextMenu(asset.path);
      ImGui::EndPopup();
    }
  }

  // Apply pending scene drag-drop after the grid pass so we don't mutate
  // SceneManager during iteration.
  if (pendingSceneMoveId != 0) {
    ctx.project->getScenes().setSceneRelPath(pendingSceneMoveId, pendingSceneMoveTarget);
  }

  // ── Confirm dialogs ──────────────────────────────────────────────────
  if (!deletePath.empty()) ImGui::OpenPopup("Confirm Delete");
  if (ImGui::BeginPopupModal("Confirm Delete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("This action cannot be undone!\nAre you sure you want to delete this asset?");
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(120_px, 0))) {
      fs::remove(deletePath);
      deletePath.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::SetItemDefaultFocus();
    if (ImGui::Button("Cancel", ImVec2(120_px, 0))) {
      deletePath.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // Folder delete: warn about nested scenes (which sit outside the two
  // physical roots) so the user knows what's about to vanish.
  if (!deleteFolderPath.empty()) ImGui::OpenPopup("Confirm Folder Delete");
  if (ImGui::BeginPopupModal("Confirm Folder Delete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    auto sceneIds = ctx.project->getScenes().findScenesUnder(deleteFolderPath);
    ImGui::Text("Delete folder \"%s\" and everything inside?", deleteFolderPath.c_str());
    if (!sceneIds.empty()) {
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
        "This will also delete %d scene%s assigned to this folder.",
        (int)sceneIds.size(), sceneIds.size() == 1 ? "" : "s");
    }
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(120_px, 0))) {
      // Remove scenes first (they live outside the physical mirror).
      for (int sid : sceneIds) ctx.project->getScenes().remove(sid);

      // Then nuke whichever physical sides exist.
      fs::path assetSide  = assetsRootAbs  / deleteFolderPath;
      fs::path scriptSide = scriptsRootAbs / deleteFolderPath;
      std::error_code ec;
      if (fs::exists(assetSide))  fs::remove_all(assetSide,  ec);
      if (fs::exists(scriptSide)) fs::remove_all(scriptSide, ec);

      deleteFolderPath.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::SetItemDefaultFocus();
    if (ImGui::Button("Cancel", ImVec2(120_px, 0))) {
      deleteFolderPath.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // ── Empty-area Create / Import context menu ──────────────────────────
  bool wantsNewScript = false;
  bool wantsNewResource = false;

  auto runImportTo = [&](const fs::path &targetAbsDir, const std::vector<Utils::FilePicker::Options::Filter> &filters) {
    fs::create_directories(targetAbsDir);
    Utils::FilePicker::open(
      [targetAbsDirStr = targetAbsDir.string()](const std::string &path) {
        if (path.empty()) return;
        std::error_code ec;
        fs::path src{path};
        fs::path dst = fs::path(targetAbsDirStr) / src.filename();
        if (fs::exists(dst, ec)) {
          Editor::Noti::add(Editor::Noti::Type::ERROR,
            "Asset already exists at destination: " + dst.filename().string());
          return;
        }
        fs::copy_file(src, dst, ec);
        if (ec) {
          Editor::Noti::add(Editor::Noti::Type::ERROR,
            "Import failed: " + ec.message());
        }
      },
      {.title = "Import Asset…", .isDirectory = false, .customFilters = filters}
    );
  };

  auto createBlankPrefab = [&](const fs::path &dir) {
    fs::create_directories(dir);
    auto pickName = [&](){
      for (int i = 0; i < 1000; ++i) {
        std::string base = (i == 0) ? "NewPrefab" : ("NewPrefab_" + std::to_string(i + 1));
        fs::path candidate = dir / (base + ".prefab");
        std::error_code existsEc;
        if (!fs::exists(candidate, existsEc)) {
          return std::pair{base, candidate};
        }
      }
      return std::pair<std::string, fs::path>{"NewPrefab", dir / "NewPrefab.prefab"};
    };
    auto [stem, fullPath] = pickName();

    Project::Prefab prefab{};
    prefab.uuid.value = Utils::Hash::randomU64();
    prefab.obj.name = "Root";
    prefab.obj.scale.value = {1.0f, 1.0f, 1.0f};
    prefab.obj.rot.value = {0, 0, 0, 1};

    Utils::FS::saveTextFile(fullPath.string(), prefab.serialize());

    // Scaffold the per-prefab user source pair alongside the .prefab so the
    // Code panel in the prefab editor has files to list immediately. Uses the
    // same naming the existing P64_NODE scanner expects (filename including
    // the .prefab extension), so addPrefabFunction / scanPrefabFunctions
    // continue to round-trip cleanly.
    Project::ensurePrefabUserSource(
      ctx.project->getPath(), fullPath.filename().string()
    );
    ctx.project->getAssets().reload();

    if (ctx.editorScene) {
      auto* entry = ctx.project->getAssets().getByPath(fullPath.string());
      if (entry) ctx.editorScene->openPrefabEditor(entry->getUUID());
    }
  };

  if (ImGui::BeginPopupContextWindow("AssetsBrowserCtx",
        ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
  {
    // New Folder — eager-mirror across both physical roots so the
    // virtual tree stays parallel.
    if (ImGui::MenuItem(ICON_MDI_FOLDER_PLUS " New Folder")) {
      auto pickFreeName = [&](){
        for (int i = 0; i < 1000; ++i) {
          std::string name = (i == 0) ? "NewFolder" : ("NewFolder_" + std::to_string(i + 1));
          // Reserve "p64" at the Content root.
          if (currentDir.empty() && name == "p64") continue;
          fs::path candA = assetsCurAbs  / name;
          fs::path candS = scriptsCurAbs / name;
          if (!fs::exists(candA) && !fs::exists(candS)) return name;
        }
        return std::string{"NewFolder"};
      };
      std::string name = pickFreeName();
      std::error_code ec;
      fs::create_directories(assetsCurAbs  / name, ec);
      fs::create_directories(scriptsCurAbs / name, ec);
    }

    ImGui::Separator();

    if (ImGui::MenuItem(ICON_MDI_EARTH_BOX_PLUS " New Scene")) {
      ctx.project->getScenes().add();
      // Place the freshly-created scene at the current virtual folder.
      const auto &after = ctx.project->getScenes().getEntries();
      if (!after.empty() && !currentDir.empty()) {
        ctx.project->getScenes().setSceneRelPath(after.back().id, currentDir);
      }
    }

    if (ImGui::MenuItem(ICON_MDI_PACKAGE_VARIANT_CLOSED_PLUS " New Prefab")) {
      createBlankPrefab(assetsCurAbs);
    }

    if (ImGui::MenuItem(ICON_MDI_PALETTE_SWATCH " New Material")) {
      // createMaterial() writes into <project>/assets/ directly (matching
      // the existing createNodeGraph contract — current sub-dir isn't
      // consulted). Search the same root for a free name.
      fs::path matRoot = fs::path(ctx.project->getPath()) / "assets";
      auto findFreeName = [&]() -> std::string {
        for (int i = 1; i < 1000; ++i) {
          std::string n = (i == 1) ? "Material" : ("Material_" + std::to_string(i));
          if (!fs::exists(matRoot / (n + ".p64mat"))) return n;
        }
        return "Material_X";
      };
      uint64_t newUUID = ctx.project->getAssets().createMaterial(findFreeName());
      if (newUUID && ctx.editorScene) {
        ctx.selAssetUUID = newUUID;
        ctx.editorScene->openMaterialEditor(newUUID);
      } else if (!newUUID) {
        Editor::Noti::add(Editor::Noti::Type::ERROR,
          "Failed to create material asset.");
      }
    }

    if (ImGui::BeginMenu(ICON_MDI_FILE_DOCUMENT_PLUS_OUTLINE " New Script")) {
      if (ImGui::MenuItem("Object Script")) {
        newScriptDir = currentDir; scriptName = "New_Script"; scriptType = 0;
        wantsNewScript = true;
      }
      if (ImGui::MenuItem("Global Script")) {
        newScriptDir = currentDir; scriptName = "New_Script"; scriptType = 1;
        wantsNewScript = true;
      }
      if (ImGui::MenuItem("Node Graph")) {
        newScriptDir = currentDir; scriptName = "New_Graph";  scriptType = 2;
        wantsNewScript = true;
      }
      ImGui::EndMenu();
    }

    {
      const auto &resourceTypes = ctx.project->getAssets().getTypeEntries(FileType::RESOURCE_TYPE);
      if (!resourceTypes.empty()) {
        if (ImGui::BeginMenu(ICON_MDI_DATABASE_PLUS " New Resource Instance")) {
          for (const auto &typeEntry : resourceTypes) {
            if (ImGui::MenuItem(typeEntry.name.c_str())) {
              newResourceDir = currentDir;
              newResourceName = "New_" + fs::path(typeEntry.name).stem().string();
              newResourceTypeUUID = typeEntry.getUUID();
              wantsNewResource = true;
            }
          }
          ImGui::EndMenu();
        }
      }
    }

    ImGui::Separator();

    if (ImGui::MenuItem(ICON_MDI_FILE_IMPORT_OUTLINE " Import Asset…")) {
      using F = Utils::FilePicker::Options::Filter;
      runImportTo(assetsCurAbs, {
        F{"Image",       "png"},
        F{"GLTF/GLB",    "glb,gltf"},
        F{"Font (TTF)",  "ttf"},
        F{"Audio (WAV)", "wav"},
        F{"Music (XM)",  "xm"},
        F{"Prefab",      "p64prefab"},
      });
    }
    if (ImGui::MenuItem(ICON_MDI_FILE_IMPORT_OUTLINE " Import Script…")) {
      using F = Utils::FilePicker::Options::Filter;
      runImportTo(scriptsCurAbs, { F{"C++ Source", "cpp"} });
    }

    ImGui::EndPopup();
  }
  if (wantsNewScript) ImGui::OpenPopup("NewScript");
  if (wantsNewResource) ImGui::OpenPopup("NewResource");

  ImGui::Dummy({0, 10_px});

  ImGui::SetNextWindowSize(ImVec2(250_px, 0));
  if (ImGui::BeginPopup("NewScript"))
  {
    ImTable::start("SCRIPT");

    ImTable::add("Type");
    const char* scriptTypes[] = {"Object Script", "Global Script", "Node Graph"};
    ImGui::Combo("##ScriptType", &scriptType, scriptTypes, IM_ARRAYSIZE(scriptTypes));

    ImTable::add("Name");
    ImGui::InputText("##ScriptName", &scriptName);

    ImTable::end();
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 112_px);
    if (ImGui::Button("Create"))
    {
      bool res{false};
      if (scriptType == 0) res = ctx.project->getAssets().createScript(scriptName, false, newScriptDir);
      if (scriptType == 1) res = ctx.project->getAssets().createScript(scriptName, true,  newScriptDir);
      if (scriptType == 2) res = ctx.project->getAssets().createNodeGraph(scriptName) != 0;

      if (res) {
        ImGui::CloseCurrentPopup();
      } else {
        Editor::Noti::add(
          Editor::Noti::Type::ERROR,
          "Failed to create script. File Name may not contain any of [/, \\, :, *, ?, \", <, >, |]."
        );
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowSize(ImVec2(280_px, 0));
  if (ImGui::BeginPopup("NewResource"))
  {
    auto *typeEntry = ctx.project->getAssets().getEntryByUUID(newResourceTypeUUID);

    ImTable::start("RESOURCE");
    ImTable::add("Type");
    ImGui::TextUnformatted(typeEntry ? typeEntry->name.c_str() : "(missing)");
    ImTable::add("Name");
    ImGui::InputText("##ResName", &newResourceName);
    ImTable::end();

    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 112_px);
    if (ImGui::Button("Create"))
    {
      auto uuid = ctx.project->getAssets().createResourceInstance(
        newResourceName, newResourceTypeUUID, newResourceDir
      );
      if (uuid) {
        ctx.selAssetUUID = uuid;
        ImGui::CloseCurrentPopup();
      } else {
        Editor::Noti::add(
          Editor::Noti::Type::ERROR,
          "Failed to create resource instance. Name may not contain any of [/, \\, :, *, ?, \", <, >, |] and the file must not already exist."
        );
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::EndChild();
  ImGui::EndChild();

  // Persist any navigation that happened this frame back into the active
  // tab's dir slot so the next frame and tab switches see it.
  if (splitMode) tabDirs[activeTab] = currentDir;
}

void Editor::AssetsBrowser::showContextMenu(const std::string& path) {
#if defined(_WIN32)
  std::string showPrompt = ICON_MDI_FOLDER_OPEN " Show in Explorer";
#elif defined(__APPLE__)
  std::string showPrompt = ICON_MDI_FOLDER_OPEN " Show in Finder";
#else
  std::string showPrompt = ICON_MDI_FOLDER_OPEN " Show in File Manager";
#endif
  if(ImGui::MenuItem(showPrompt.c_str())) {
    if (!Utils::Proc::openInFileBrowser(path)) {
      Editor::Noti::add(Editor::Noti::Type::ERROR, "Failed to open File Explorer. This may be due to WSL path conversion failure.");
    }
  }

  if(ImGui::MenuItem(ICON_MDI_OPEN_IN_NEW " Open")) {
    if (!Utils::Proc::openFile(path)) {
      Editor::Noti::add(Editor::Noti::Type::ERROR, "Failed to open File. This may be due to WSL path conversion failure.");
    }
  }

  if(ImGui::MenuItem(ICON_MDI_CONTENT_COPY " Copy Path")) {
    SDL_SetClipboardText(path.c_str());
  }

  if(ImGui::MenuItem(ICON_MDI_RENAME " Rename")) {
    renamePath = path;
    std::string stem = fs::path(path).stem().string();
    strncpy(renameBuffer, stem.c_str(), sizeof(renameBuffer) - 1);
    renameBuffer[sizeof(renameBuffer) - 1] = '\0';
  }

  if(ImGui::MenuItem(ICON_MDI_DELETE " Delete")) {
    deletePath = path;
  }
}
