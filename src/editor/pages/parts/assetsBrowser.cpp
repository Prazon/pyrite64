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
#include <functional>
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
    ChipDef{ "Res. Types",  ICON_MDI_DATABASE_OUTLINE,         FileType::RESOURCE_TYPE,     false },
    ChipDef{ "Resources",   ICON_MDI_DATABASE_EDIT_OUTLINE,    FileType::RESOURCE_INSTANCE, false },
    ChipDef{ "Materials",   ICON_MDI_PALETTE_SWATCH,           FileType::MATERIAL,          false },
  };

  // Color the asset card with a thin stripe between icon and label so users can
  // identify the asset kind at a glance without reading the name. Scenes have
  // no FileType slot so they're handled with their own constant below.
  constexpr ImU32 SCENE_TYPE_COLOR = IM_COL32(0xFF, 0x8C, 0x14, 0xFF);

  ImU32 assetTypeColor(FileType t)
  {
    switch (t) {
      case FileType::PREFAB:            return IM_COL32(0x1F, 0x8C, 0xFF, 0xFF); // blue
      case FileType::IMAGE:             return IM_COL32(0xFF, 0x32, 0x32, 0xFF); // red
      case FileType::MODEL_3D:          return IM_COL32(0xC8, 0x3C, 0xFF, 0xFF); // purple
      case FileType::AUDIO:
      case FileType::MUSIC_XM:          return IM_COL32(0xB4, 0x14, 0x32, 0xFF); // maroon
      case FileType::FONT:              return IM_COL32(0xFF, 0xE6, 0x3C, 0xFF); // yellow
      case FileType::MATERIAL:          return IM_COL32(0x32, 0xC8, 0x46, 0xFF); // green
      case FileType::CODE_OBJ:
      case FileType::CODE_GLOBAL:
      case FileType::NODE_GRAPH:        return IM_COL32(0x96, 0x96, 0x96, 0xFF); // grey
      default:                          return IM_COL32(0x6E, 0x6E, 0x6E, 0xFF); // fallback grey
    }
  }

  // Color for a chip in the unified-mode left rail. Mirrors assetTypeColor()
  // but special-cases the Scenes chip, since scenes don't have a FileType slot.
  ImU32 chipColor(int chipIdx)
  {
    if (chipIdx == ChipKind::CHIP_SCENES) return SCENE_TYPE_COLOR;
    return assetTypeColor(CHIP_DEFS[chipIdx].type);
  }

  // Short label rendered as the second line of an asset card (UE5-style). Kept
  // brief so it doesn't wrap inside a card; matches the chip rail vocabulary.
  const char* assetTypeLabel(FileType t)
  {
    switch (t) {
      case FileType::IMAGE:             return "Texture";
      case FileType::AUDIO:             return "Audio";
      case FileType::FONT:              return "Font";
      case FileType::MODEL_3D:          return "Static Mesh";
      case FileType::CODE_OBJ:          return "Object Script";
      case FileType::CODE_GLOBAL:       return "Global Script";
      case FileType::PREFAB:            return "Prefab";
      case FileType::NODE_GRAPH:        return "Node Graph";
      case FileType::MUSIC_XM:          return "Music";
      case FileType::RESOURCE_TYPE:     return "Resource Type";
      case FileType::RESOURCE_INSTANCE: return "Resource";
      case FileType::MATERIAL:          return "Material";
      default:                          return "Asset";
    }
  }

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

  // ── Nav history (Unified mode only) ──────────────────────────────────
  // Lazy-seed and re-sync if currentDir was changed externally (e.g. mode
  // switch). Split mode bypasses the stack entirely since per-tab dirs
  // don't share a single timeline.
  if (!splitMode) {
    if (dirHistory.empty()) {
      dirHistory.push_back(currentDir);
      dirHistoryIdx = 0;
    } else if (dirHistory[dirHistoryIdx] != currentDir) {
      if (dirHistoryIdx + 1 < (int)dirHistory.size())
        dirHistory.resize(dirHistoryIdx + 1);
      dirHistory.push_back(currentDir);
      dirHistoryIdx = (int)dirHistory.size() - 1;
    }
  }

  // Funnel for user-initiated navigation. Anything that wants the back
  // button to remember the previous spot must go through this. Toolbar
  // back/forward bypass it deliberately so they don't push their own
  // targets onto the stack.
  auto navigateTo = [&](const std::string &target) {
    std::string norm = normalizeDir(target);
    if (norm == currentDir) return;
    currentDir = norm;
    if (!splitMode) {
      if (dirHistoryIdx + 1 < (int)dirHistory.size())
        dirHistory.resize(dirHistoryIdx + 1);
      dirHistory.push_back(norm);
      dirHistoryIdx = (int)dirHistory.size() - 1;
    }
  };

  // ── Hoisted create-menu helpers ──────────────────────────────────────
  // Live above the LEFT/RIGHT split so both the toolbar Add button and
  // the empty-area context menu can share one drawCreateMenu() body.

  // Drop the just-created asset into rename mode so the user can either
  // accept the default name (Enter), discard rename (Escape — file keeps the
  // default), or replace the highlighted text by typing. This sidesteps the
  // need for a "new asset name" dialog: the rename overlay already does
  // exactly the right UX.
  auto enterRenameForPath = [&](const std::string &absPath) {
    renamePath = absPath;
    std::string stem = fs::path(absPath).stem().string();
    if (stem.empty()) stem = fs::path(absPath).filename().string();
    std::strncpy(renameBuffer, stem.c_str(), sizeof(renameBuffer) - 1);
    renameBuffer[sizeof(renameBuffer) - 1] = '\0';
  };
  auto enterRenameForUUID = [&](uint64_t uuid) {
    if (!uuid) return;
    ctx.selAssetUUID = uuid;
    auto* entry = ctx.project->getAssets().getEntryByUUID(uuid);
    if (!entry) return;
    enterRenameForPath(entry->path);
  };

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

  auto createBlankPrefab = [&](const fs::path &dir) -> fs::path {
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
    // Code panel in the prefab editor has files to list immediately.
    Project::ensurePrefabUserSource(
      ctx.project->getPath(), fullPath.filename().string()
    );
    ctx.project->getAssets().reload();
    return fullPath;
  };

  auto drawCreateMenu = [&]() {
    if (ImGui::MenuItem(ICON_MDI_FOLDER_PLUS " New Folder")) {
      auto pickFreeName = [&](){
        for (int i = 0; i < 1000; ++i) {
          std::string name = (i == 0) ? "NewFolder" : ("NewFolder_" + std::to_string(i + 1));
          if (currentDir.empty() && name == "p64") continue;
          fs::path candA = assetsCurAbs  / name;
          fs::path candS = scriptsCurAbs / name;
          if (!fs::exists(candA) && !fs::exists(candS)) return name;
        }
        return std::string{"NewFolder"};
      };
      std::string name = pickFreeName();
      std::error_code ec;
      fs::path newDirA = assetsCurAbs  / name;
      fs::create_directories(newDirA, ec);
      fs::create_directories(scriptsCurAbs / name, ec);
      // Drop the new folder card into rename mode. The folder card resolves
      // its id to the assets-side path when both sides exist (which they do
      // after the pair of create_directories calls above), so renamePath
      // matches the card id and the InputText overlay appears next frame.
      enterRenameForPath(newDirA.string());
    }

    ImGui::Separator();

    if (ImGui::MenuItem(ICON_MDI_EARTH_BOX_PLUS " New Scene")) {
      ctx.project->getScenes().add();
      const auto &after = ctx.project->getScenes().getEntries();
      if (!after.empty() && !currentDir.empty()) {
        ctx.project->getScenes().setSceneRelPath(after.back().id, currentDir);
      }
    }

    if (ImGui::MenuItem(ICON_MDI_PACKAGE_VARIANT_CLOSED_PLUS " New Prefab")) {
      fs::path created = createBlankPrefab(assetsCurAbs);
      if (auto* entry = ctx.project->getAssets().getByPath(created.string())) {
        enterRenameForUUID(entry->getUUID());
      }
    }

    if (ImGui::MenuItem(ICON_MDI_DATABASE_EDIT " New Resource Type")) {
      auto findFreeName = [&]() -> std::string {
        for (int i = 1; i < 1000; ++i) {
          std::string n = (i == 1) ? "ResourceType" : ("ResourceType_" + std::to_string(i));
          if (!fs::exists(assetsCurAbs / (n + ".p64restype"))) return n;
        }
        return "ResourceType_X";
      };
      uint64_t newUUID = ctx.project->getAssets().createResourceType(
        findFreeName(), currentDir
      );
      if (newUUID) {
        enterRenameForUUID(newUUID);
      } else {
        Editor::Noti::add(Editor::Noti::Type::ERROR,
          "Failed to create resource type.");
      }
    }

    if (ImGui::MenuItem(ICON_MDI_PALETTE_SWATCH " New Material")) {
      // createMaterial() writes into <project>/assets/ directly (matching
      // createNodeGraph). Search the same root for a free name.
      fs::path matRoot = fs::path(ctx.project->getPath()) / "assets";
      auto findFreeName = [&]() -> std::string {
        for (int i = 1; i < 1000; ++i) {
          std::string n = (i == 1) ? "Material" : ("Material_" + std::to_string(i));
          if (!fs::exists(matRoot / (n + ".p64mat"))) return n;
        }
        return "Material_X";
      };
      uint64_t newUUID = ctx.project->getAssets().createMaterial(findFreeName());
      if (newUUID) {
        enterRenameForUUID(newUUID);
      } else {
        Editor::Noti::add(Editor::Noti::Type::ERROR,
          "Failed to create material asset.");
      }
    }

    auto findFreeScriptName = [&](const std::string &base, const char* ext) {
      for (int i = 0; i < 1000; ++i) {
        std::string n = (i == 0) ? base : (base + "_" + std::to_string(i + 1));
        if (!fs::exists(scriptsCurAbs / (n + ext))) return n;
      }
      return base;
    };
    auto findFreeAssetRootName = [&](const std::string &base, const char* ext) {
      fs::path root = fs::path(ctx.project->getPath()) / "assets";
      for (int i = 0; i < 1000; ++i) {
        std::string n = (i == 0) ? base : (base + "_" + std::to_string(i + 1));
        if (!fs::exists(root / (n + ext))) return n;
      }
      return base;
    };

    if (ImGui::BeginMenu(ICON_MDI_FILE_DOCUMENT_PLUS_OUTLINE " New Script")) {
      auto createScriptKind = [&](bool isGlobal, const std::string &base) {
        std::string name = findFreeScriptName(base, ".cpp");
        if (ctx.project->getAssets().createScript(name, isGlobal, currentDir)) {
          // Look up the new entry by path so we use the AssetManager's
          // normalized string (the filesystem iterator may format it
          // differently to our locally-constructed string, and the rename
          // overlay matches by exact string equality).
          fs::path constructed = scriptsCurAbs / (name + ".cpp");
          if (auto* entry = ctx.project->getAssets().getByPath(constructed.string())) {
            enterRenameForUUID(entry->getUUID());
          }
        } else {
          Editor::Noti::add(Editor::Noti::Type::ERROR, "Failed to create script.");
        }
      };
      if (ImGui::MenuItem("Object Script")) createScriptKind(false, "NewScript");
      if (ImGui::MenuItem("Global Script")) createScriptKind(true,  "NewGlobalScript");
      if (ImGui::MenuItem("Node Graph")) {
        std::string name = findFreeAssetRootName("NewGraph", ".p64graph");
        uint64_t uuid = ctx.project->getAssets().createNodeGraph(name);
        if (uuid) enterRenameForUUID(uuid);
        else Editor::Noti::add(Editor::Noti::Type::ERROR, "Failed to create node graph.");
      }
      ImGui::EndMenu();
    }

    {
      const auto &resourceTypes = ctx.project->getAssets().getTypeEntries(FileType::RESOURCE_TYPE);
      if (!resourceTypes.empty()) {
        if (ImGui::BeginMenu(ICON_MDI_DATABASE_PLUS " New Resource Instance")) {
          for (const auto &typeEntry : resourceTypes) {
            if (ImGui::MenuItem(typeEntry.name.c_str())) {
              std::string baseName = "New" + fs::path(typeEntry.name).stem().string();
              auto findFree = [&]() {
                for (int i = 0; i < 1000; ++i) {
                  std::string n = (i == 0) ? baseName : (baseName + "_" + std::to_string(i + 1));
                  if (!fs::exists(assetsCurAbs / (n + ".p64res"))) return n;
                }
                return baseName;
              };
              uint64_t uuid = ctx.project->getAssets().createResourceInstance(
                findFree(), typeEntry.getUUID(), currentDir);
              if (uuid) enterRenameForUUID(uuid);
              else Editor::Noti::add(Editor::Noti::Type::ERROR,
                "Failed to create resource instance.");
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
  };

  // ── LEFT panes (Unified mode only) ───────────────────────────────────
  // Three-column unified layout: folder tree | chip rail | grid. Each
  // separator is a thin button styled like ImGuiCol_Separator, mirroring
  // PrefabEditor's drawSplitter pattern. Hidden in Split mode — its tab
  // strip drives content scoping there.
  constexpr float SPLITTER_W   = 4.0f;
  constexpr float TREE_MIN_W   = 100.0f;
  constexpr float CHIP_MIN_W   = 60.0f;
  constexpr float CHIP_MAX_PAD = 200.0f; // leave at least this much for the grid
  float fullAvail = ImGui::GetContentRegionAvail().x;
  float availWidth;

  // Reusable vertical splitter — drag to resize the panel to its left.
  auto vSplitter = [&](const char* id, float &widthVar, float minW, float maxW) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImGui::GetStyleColorVec4(ImGuiCol_Separator));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
    ImGui::Button(id, ImVec2(SPLITTER_W, -1));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActive()) {
      widthVar = ImClamp(widthVar + ImGui::GetIO().MouseDelta.x, minW, std::max(minW, maxW));
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
  };

  if (!splitMode) {
    // Width budget: tree + chip + 2*splitter + (grid >= CHIP_MAX_PAD).
    float twoColMax = std::max(TREE_MIN_W + CHIP_MIN_W,
                               fullAvail - CHIP_MAX_PAD - SPLITTER_W*2);
    folderTreeWidth = ImClamp(folderTreeWidth, TREE_MIN_W,
                              std::max(TREE_MIN_W, twoColMax - CHIP_MIN_W));
    chipPanelWidth  = ImClamp(chipPanelWidth, CHIP_MIN_W,
                              std::max(CHIP_MIN_W, twoColMax - folderTreeWidth));

    // Folder tree — virtual unified directory tree (union of assets/ and
    // src/user/). Same merge semantics as the breadcrumb's folder listing
    // so the user sees one tree regardless of which physical root a folder
    // lives in.
    ImGui::BeginChild("FOLDER_TREE", ImVec2(folderTreeWidth, 0), ImGuiChildFlags_Borders);

    std::function<void(const std::string&)> drawTreeChildren = [&](const std::string &virt) {
      std::vector<std::string> kids{};
      std::unordered_set<std::string> seen{};
      for (const auto &root : {assetsRootAbs, scriptsRootAbs}) {
        fs::path dir = root / virt;
        std::error_code ec;
        for (auto it = fs::directory_iterator(dir, ec);
             !ec && it != fs::directory_iterator();
             it.increment(ec)) {
          if (!it->is_directory()) continue;
          auto name = it->path().filename().string();
          // Hide the generated outputs directory at the Content root.
          if (virt.empty() && name == "p64") continue;
          if (seen.insert(name).second) kids.push_back(name);
        }
      }
      std::sort(kids.begin(), kids.end());

      for (const auto &name : kids) {
        std::string childVirt = joinDir(virt, name);

        // Probe one level down so leaf folders skip the expand arrow.
        bool hasKids = false;
        for (const auto &root : {assetsRootAbs, scriptsRootAbs}) {
          fs::path d2 = root / childVirt;
          std::error_code ec;
          for (auto it2 = fs::directory_iterator(d2, ec);
               !ec && it2 != fs::directory_iterator();
               it2.increment(ec)) {
            if (it2->is_directory()) { hasKids = true; break; }
          }
          if (hasKids) break;
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_OpenOnDoubleClick
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasKids) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (currentDir == childVirt) flags |= ImGuiTreeNodeFlags_Selected;

        // TreeNodeEx renders arrow + icon + name in default text color
        // (white). We then re-draw only the folder glyph in manilla on top
        // of the white one. Pixel positions must match exactly to avoid
        // ghosting; the X offset mirrors ImGui's TreeNodeBehavior:
        //   text_offset_x = FontSize + 2 * FramePadding.x
        // (without ItemInnerSpacing, which is what tripped a prior pass).
        std::string label = std::string(ICON_MDI_FOLDER " ") + name;
        bool open = ImGui::TreeNodeEx(("##tn_" + childVirt).c_str(), flags, "%s", label.c_str());
        {
          ImVec2 itemMin = ImGui::GetItemRectMin();
          float textOffsetX = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
          ImGui::GetWindowDrawList()->AddText(
            {itemMin.x + textOffsetX, itemMin.y},
            IM_COL32(0xC8, 0x96, 0x5A, 0xFF),
            ICON_MDI_FOLDER
          );
        }
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
          navigateTo(childVirt);
        }
        if (open && hasKids) {
          drawTreeChildren(childVirt);
          ImGui::TreePop();
        }
      }
    };

    // Always-open virtual Content root.
    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_SpanAvailWidth
                                 | ImGuiTreeNodeFlags_DefaultOpen;
    if (currentDir.empty()) rootFlags |= ImGuiTreeNodeFlags_Selected;
    std::string rootLabel = std::string(ICON_MDI_FOLDER_OPEN " Content");
    bool rootOpen = ImGui::TreeNodeEx("##tnRoot", rootFlags, "%s", rootLabel.c_str());
    {
      ImVec2 itemMin = ImGui::GetItemRectMin();
      float textOffsetX = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
      ImGui::GetWindowDrawList()->AddText(
        {itemMin.x + textOffsetX, itemMin.y},
        IM_COL32(0xC8, 0x96, 0x5A, 0xFF),
        ICON_MDI_FOLDER_OPEN
      );
    }
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
      navigateTo("");
    }
    if (rootOpen) {
      drawTreeChildren("");
      ImGui::TreePop();
    }

    ImGui::EndChild();

    vSplitter("##treeSplitter", folderTreeWidth,
              TREE_MIN_W,
              std::max(TREE_MIN_W, twoColMax - chipPanelWidth));

    // Chip rail — UE5-style filter pills with a left swatch carrying the
    // type color. The swatch encodes the asset kind, so the pill body drops
    // the icon glyph (would be redundant) and the label spans the rest of
    // the row. Body opaque when on, transparent off; swatch dim when off so
    // the identity is still readable at a glance.
    ImGui::SameLine();
    ImGui::BeginChild("LEFT", ImVec2(chipPanelWidth, 0), ImGuiChildFlags_Borders);

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("Filters");
    ImGui::PopStyleColor();
    ImGui::Separator();

    const float pillH     = ImGui::GetFrameHeight();
    const float pillRound = 4.0f;
    const float swatchW   = 4_px;
    ImDrawList* chipDL = ImGui::GetWindowDrawList();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3_px));
    for (int i = 0; i < ChipKind::CHIP_COUNT; ++i) {
      const auto &def = CHIP_DEFS[i];
      bool on = chips[i];

      ImVec2 pos = ImGui::GetCursorScreenPos();
      float fullW = ImGui::GetContentRegionAvail().x;
      std::string id = std::string("##chip") + std::to_string(i);
      ImGui::InvisibleButton(id.c_str(), ImVec2(fullW, pillH));
      bool hovered = ImGui::IsItemHovered();
      if (ImGui::IsItemClicked()) {
        chips[i] = !on;
        on = !on;
      }

      ImU32 bodyCol;
      if (on) {
        bodyCol = ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
      } else {
        bodyCol = hovered ? IM_COL32(255, 255, 255, 22) : IM_COL32(0, 0, 0, 0);
      }
      chipDL->AddRectFilled(pos, ImVec2(pos.x + fullW, pos.y + pillH),
                            bodyCol, pillRound);

      ImU32 swatch = chipColor(i);
      if (!on) {
        ImVec4 sc = ImGui::ColorConvertU32ToFloat4(swatch);
        swatch = IM_COL32((int)(sc.x * 255 * 0.55f),
                          (int)(sc.y * 255 * 0.55f),
                          (int)(sc.z * 255 * 0.55f),
                          0xCC);
      }
      chipDL->AddRectFilled(pos, ImVec2(pos.x + swatchW, pos.y + pillH),
                            swatch, pillRound, ImDrawFlags_RoundCornersLeft);

      ImU32 textCol = on
        ? ImGui::GetColorU32(ImGuiCol_Text)
        : ImGui::GetColorU32(ImGuiCol_TextDisabled);
      float textY = pos.y + (pillH - ImGui::GetTextLineHeight()) * 0.5f;
      std::string nameStr = def.name;
      ImGui::PushStyleColor(ImGuiCol_Text, textCol);
      ImGui::RenderTextEllipsis(
        chipDL,
        ImVec2(pos.x + swatchW + 6_px, textY),
        ImVec2(pos.x + fullW - 4_px, pos.y + pillH),
        0,
        nameStr.c_str(), nameStr.c_str() + nameStr.size(),
        nullptr
      );
      ImGui::PopStyleColor();

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
    ImGui::PopStyleVar();
    ImGui::EndChild();

    vSplitter("##chipSplitter", chipPanelWidth,
              CHIP_MIN_W,
              std::max(CHIP_MIN_W, twoColMax - folderTreeWidth));

    availWidth = ImGui::GetContentRegionAvail().x - 24_px - chipPanelWidth - SPLITTER_W
                 - folderTreeWidth - SPLITTER_W;
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

    // Unified-mode toolbar: [+ Add] [Import] [←] [→] before breadcrumb.
    // Add and Import share helpers with the empty-area context menu so the
    // create options live in one place. Back/forward walk dirHistory and
    // bypass navigateTo so they don't push their own targets.
    if (!splitMode) {
      if (ImGui::Button(ICON_MDI_PLUS " Add##cbAdd")) {
        ImGui::OpenPopup("CBAddMenu");
      }
      if (ImGui::BeginPopup("CBAddMenu")) {
        drawCreateMenu();
        ImGui::EndPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button(ICON_MDI_FILE_IMPORT_OUTLINE " Import##cbImport")) {
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
      ImGui::SameLine();

      bool canBack = dirHistoryIdx > 0;
      bool canFwd  = dirHistoryIdx >= 0
                  && dirHistoryIdx < (int)dirHistory.size() - 1;
      if (!canBack) ImGui::BeginDisabled();
      if (ImGui::Button(ICON_MDI_ARROW_LEFT "##cbBack")) {
        --dirHistoryIdx;
        currentDir = dirHistory[dirHistoryIdx];
      }
      if (!canBack) ImGui::EndDisabled();
      ImGui::SameLine();
      if (!canFwd) ImGui::BeginDisabled();
      if (ImGui::Button(ICON_MDI_ARROW_RIGHT "##cbFwd")) {
        ++dirHistoryIdx;
        currentDir = dirHistory[dirHistoryIdx];
      }
      if (!canFwd) ImGui::EndDisabled();
      ImGui::SameLine();
    }

    if (ImGui::Button(ICON_MDI_FOLDER " Content")) {
      navigateTo("");
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
        navigateTo(accum);
      }
    }
    ImGui::PopStyleVar(2);

    ImGui::SameLine();
    // Reserve trailing space for: kebab button + edge gap.
    constexpr float KEBAB_W = 22.0f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX()
                         - KEBAB_W - 2_px);
    if (ImGui::Button(ICON_MDI_COG "##cbSettings", ImVec2(KEBAB_W, 0))) {
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

  // Search row — own row above the grid, mirroring UE5's Content Browser
  // layout. Width is fixed (not full-width) so it reads as a focused control,
  // not as a header bar. Placeholder uses UE's wording.
  // The leading filter button is Unified-only — Split mode auto-scopes
  // chips to the active tab, so a manual override would conflict.
  if (!splitMode) {
    if (ImGui::Button(ICON_MDI_FILTER "##cbFilter")) {
      ImGui::OpenPopup("CBFilterPopup");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter by type");
    if (ImGui::BeginPopup("CBFilterPopup")) {
      for (int i = 0; i < ChipKind::CHIP_COUNT; ++i) {
        ImGui::PushStyleColor(ImGuiCol_Text, chipColor(i));
        if (ImGui::MenuItem(CHIP_DEFS[i].name, nullptr, chips[i])) {
          chips[i] = !chips[i];
        }
        ImGui::PopStyleColor();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Show All")) chips.fill(true);
      if (ImGui::MenuItem("Hide All")) chips.fill(false);
      ImGui::EndPopup();
    }
    ImGui::SameLine();
  }
  ImGui::SetNextItemWidth(280_px);
  ImGui::InputTextWithHint("##search", ICON_MDI_MAGNIFY "  Search Content", &searchFilter);

  // Reserve a one-line footer below the grid for the item / selection count.
  ImGui::BeginChild("ASSETS", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

  // Counted as items pass the search filter (incremented in checkLineBreak),
  // so the footer reflects what's actually rendered, not the total in the dir.
  int visibleItems = 0;

  // Card geometry — UE5 Content Browser parallel. The card is the click target;
  // every visual (thumbnail, stripe, name, type label, frame) is drawn on top
  // of an InvisibleButton via the window draw list so the whole card reads as
  // one unit. Heights derive from the current font's line metrics so the
  // layout scales correctly at different zoom levels.
  const float imageSize     = 96_px;
  const float cardPad       = 6_px;
  const float gapThumbName  = 4_px;
  const float gapNameType   = 2_px;
  const float lineH         = ImGui::GetTextLineHeight();
  const float typeLabelH    = lineH * 0.85f;
  const float nameAreaH     = 2.0f * lineH;       // up to two wrapped lines
  const float cardWidth     = imageSize + 2 * cardPad;
  const float cardHeight    = cardPad + imageSize + gapThumbName + nameAreaH
                            + gapNameType + typeLabelH + cardPad;
  const float gridGap       = 8_px;
  const float itemWidth     = cardWidth + gridGap;

  float currentWid = 0.0f;
  float cursorStartX = ImGui::GetCursorPosX();
  float cursorY      = ImGui::GetCursorPosY();

  auto checkLineBreak = [&]() {
    ++visibleItems;
    if ((currentWid + itemWidth*2) > availWidth) {
      currentWid = 0.0f;
      cursorY += cardHeight + gridGap;
      ImGui::SetCursorPos({cursorStartX, cursorY});
    } else {
      if (currentWid != 0) ImGui::SameLine(0.0f, gridGap);
    }
    currentWid += itemWidth;
  };

  // Apply a rename triggered from any card. Pulled out of the per-card draw
  // because we need to fire it AFTER the per-card popup/drag-drop checks have
  // bound to the InvisibleButton's "last item" state. Mirrors the legacy
  // drawRename body 1:1 (folder cross-root mirror, scene-folder rewrite,
  // file rename + .conf sidecar).
  auto runRenameCommit = [&]() {
    fs::path oldPath = renamePath;
    bool isDir = fs::is_directory(oldPath);

    if (isDir) {
      std::string newName = renameBuffer;
      if (!newName.empty() && newName != oldPath.filename().string()) {
        fs::path parent = oldPath.parent_path();
        fs::path newAbs = parent / newName;

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

        std::string oldVirt = joinDir(currentDir, oldPath.filename().string());
        std::string newVirt = joinDir(currentDir, newName);
        ctx.project->getScenes().renameSceneFolder(oldVirt, newVirt);
      }
    } else {
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
  };

  // Inline rename overlay positioned at the card's name slot. Called by the
  // grid loop AFTER all card-as-item queries (drag-drop, tooltip, popup) have
  // resolved against the InvisibleButton, so the InputText doesn't shadow
  // those queries.
  auto drawCardRename = [&](const ImVec2 &cardScreenPos) {
    ImVec2 nameMin{ cardScreenPos.x + cardPad,
                    cardScreenPos.y + cardPad + imageSize + gapThumbName };
    ImVec2 originalCursor = ImGui::GetCursorPos();
    ImGui::SetCursorScreenPos(nameMin);
    ImGui::SetNextItemWidth(cardWidth - 2 * cardPad);
    if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2_px, 0));
    if (ImGui::InputText("##renameInput", renameBuffer, sizeof(renameBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue
                         | ImGuiInputTextFlags_AutoSelectAll)) {
      runRenameCommit();
    }
    ImGui::PopStyleVar();

    if ((!ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
        || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      renamePath.clear();
    }
    ImGui::SetCursorPos(originalCursor);
  };

  // Render one asset card. The InvisibleButton is the click target so the
  // entire frame is selectable; everything else is draw-list calls. Returns
  // true on click. `outScreenPos` exposes the card's top-left for the rename
  // overlay; pass nullptr if not needed.
  auto drawAssetCard = [&](
    const std::string &id, ImTextureRef icon, const char* iconTxt,
    const std::string &displayName, const char* typeLabel,
    ImU32 typeColor, bool selected, float alpha,
    ImVec2 *outScreenPos
  ) -> bool
  {
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    if (outScreenPos) *outScreenPos = startPos;

    bool clicked = ImGui::InvisibleButton(id.c_str(), {cardWidth, cardHeight},
                                          ImGuiButtonFlags_AllowOverlap);
    bool hovered = ImGui::IsItemHovered();

    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 cardMin = startPos;
    ImVec2 cardMax = {startPos.x + cardWidth, startPos.y + cardHeight};
    ImVec2 thumbMin = {cardMin.x + cardPad, cardMin.y + cardPad};
    ImVec2 thumbMax = {thumbMin.x + imageSize, thumbMin.y + imageSize};
    const float rounding = 4.0f;

    // Card body — single dark fill behind the whole card.
    ImU32 bodyCol  = IM_COL32(28, 28, 32, 255);
    ImU32 thumbBg  = IM_COL32(40, 40, 44, 255);
    ImU32 nameCol  = IM_COL32(230, 230, 235, 255);
    ImU32 typeCol  = IM_COL32(140, 140, 145, 255);
    if (selected) bodyCol = IM_COL32(50, 70, 120, 255);
    dl->AddRectFilled(cardMin, cardMax, bodyCol, rounding);

    // Thumbnail panel.
    dl->AddRectFilled(thumbMin, thumbMax, thumbBg, rounding * 0.6f);

    // Checker pattern behind glyph-only thumbnails (UE5 uses this for
    // transparent textures; we use it everywhere there's no real thumbnail
    // so the panel doesn't read as flat).
    if (!icon._TexID) {
      const float cell = 8_px;
      ImU32 c1 = IM_COL32(36, 36, 40, 255);
      ImU32 c2 = IM_COL32(48, 48, 52, 255);
      int rows = (int)((thumbMax.y - thumbMin.y) / cell) + 1;
      int cols = (int)((thumbMax.x - thumbMin.x) / cell) + 1;
      dl->PushClipRect(thumbMin, thumbMax, true);
      for (int r = 0; r < rows; ++r) {
        for (int col = 0; col < cols; ++col) {
          ImVec2 cmin = { thumbMin.x + col*cell, thumbMin.y + r*cell };
          ImVec2 cmax = { cmin.x + cell, cmin.y + cell };
          dl->AddRectFilled(cmin, cmax, ((r + col) & 1) ? c1 : c2);
        }
      }
      dl->PopClipRect();
    }

    // Icon or texture content.
    int alphaI = (int)(alpha * 255.0f);
    if (icon._TexID) {
      dl->AddImage(icon._TexID, thumbMin, thumbMax, {0, 0}, {1, 1},
                   IM_COL32(255, 255, 255, alphaI));
    } else if (iconTxt && iconTxt[0]) {
      const float glyphSize = 40_px;
      ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(glyphSize, FLT_MAX, 0.0f, iconTxt);
      ImVec2 glyphPos = {
        (thumbMin.x + thumbMax.x - ts.x) * 0.5f,
        (thumbMin.y + thumbMax.y - ts.y) * 0.5f
      };
      dl->AddText(ImGui::GetFont(), glyphSize, glyphPos,
                  IM_COL32(220, 220, 225, alphaI), iconTxt);
    }

    // Type-color stripe at the bottom edge of the thumbnail (touching the
    // thumbnail rect — UE5 reads this as the type signal).
    if (typeColor) {
      const float stripeH = 2_px;
      dl->AddRectFilled(
        { thumbMin.x, thumbMax.y - stripeH },
        { thumbMax.x, thumbMax.y },
        typeColor
      );
    }

    // Type-icon corner badge for cards that show a real thumbnail. Without
    // this, a textured asset looks like a generic image — the badge restores
    // the type cue UE5 relies on.
    if (icon._TexID && iconTxt && iconTxt[0]) {
      const float badgeR = 9_px;
      ImVec2 badgePos = {thumbMax.x - badgeR - 4_px, thumbMax.y - badgeR - 6_px};
      dl->AddCircleFilled(badgePos, badgeR, IM_COL32(20, 20, 24, 210));
      const float gs = 14_px;
      ImVec2 gts = ImGui::GetFont()->CalcTextSizeA(gs, FLT_MAX, 0.0f, iconTxt);
      dl->AddText(ImGui::GetFont(), gs,
                  {badgePos.x - gts.x*0.5f, badgePos.y - gts.y*0.5f},
                  IM_COL32(230, 230, 235, 255), iconTxt);
    }

    // Name text — wrapped to two lines, ellipsised on overflow. Clipped to
    // the card's text area so a runaway substring (e.g. unbreakable token
    // wider than the card) can't bleed into the next cell. Skipped when
    // renaming because the rename overlay will replace this region.
    bool isRenaming = (id == renamePath);
    if (!isRenaming) {
      float textLeft  = cardMin.x + cardPad;
      float textRight = cardMax.x - cardPad;
      float wrapWidth = textRight - textLeft;
      float textY     = thumbMax.y + gapThumbName;

      const char* str = displayName.c_str();
      const char* end = str + displayName.size();
      ImFont* font = ImGui::GetFont();
      float fontSize = ImGui::GetFontSize();

      dl->PushClipRect({textLeft, textY},
                       {textRight, cardMax.y - cardPad * 0.5f},
                       true);

      const char* l1End = font->CalcWordWrapPositionA(1.0f, str, end, wrapWidth);
      if (l1End >= end) {
        // Fits on one line.
        dl->AddText(font, fontSize, {textLeft, textY}, nameCol, str, end);
      } else {
        dl->AddText(font, fontSize, {textLeft, textY}, nameCol, str, l1End);

        const char* l2Begin = l1End;
        while (l2Begin < end && (*l2Begin == ' ' || *l2Begin == '\n')) ++l2Begin;
        const char* l2End = font->CalcWordWrapPositionA(1.0f, l2Begin, end, wrapWidth);

        if (l2End >= end) {
          dl->AddText(font, fontSize, {textLeft, textY + lineH}, nameCol,
                      l2Begin, l2End);
        } else {
          // Truncate line 2 with an ellipsis. Reserve ellipsis width and
          // re-wrap so we never run past the card edge.
          const char* ell = "...";
          float ellW = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ell).x;
          const char* l2Trunc = font->CalcWordWrapPositionA(
            1.0f, l2Begin, end, std::max(0.0f, wrapWidth - ellW));
          if (l2Trunc <= l2Begin && l2Begin < end) l2Trunc = l2Begin + 1;
          std::string truncated(l2Begin, l2Trunc);
          truncated += ell;
          dl->AddText(font, fontSize, {textLeft, textY + lineH}, nameCol,
                      truncated.c_str());
        }
      }

      // Type label — smaller, dim. Folders pass nullptr/"" to skip.
      if (typeLabel && typeLabel[0]) {
        float typeY = textY + nameAreaH + gapNameType;
        dl->AddText(font, typeLabelH, {textLeft, typeY}, typeCol, typeLabel);
      }

      dl->PopClipRect();
    }

    // Border. Brighter on hover, accented on selected.
    ImU32 borderCol = IM_COL32(70, 70, 75, 255);
    float borderThick = 1.0f;
    if (selected) {
      borderCol  = IM_COL32(120, 160, 255, 255);
      borderThick = 2.0f;
    } else if (hovered) {
      borderCol  = IM_COL32(150, 150, 155, 255);
      borderThick = 1.5f;
    }
    dl->AddRect(cardMin, cardMax, borderCol, rounding, 0, borderThick);

    return clicked && !isRenaming;
  };

  // Folder card — distinct render so folders don't read as assets. UE5
  // parallel: a tan/beige folder shape with the name centered below, no
  // card frame, no type stripe, no type label. `filled` shades the folder
  // brighter to signal that it contains matching content under the active
  // chip filter. Sized to the same cell as drawAssetCard so the grid stays
  // uniform; the rename overlay reuses the asset-card name slot.
  auto drawFolderCard = [&](
    const std::string &id, const std::string &name,
    bool filled, bool selected, ImVec2 *outScreenPos
  ) -> bool
  {
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    if (outScreenPos) *outScreenPos = startPos;

    bool clicked = ImGui::InvisibleButton(id.c_str(), {cardWidth, cardHeight},
                                          ImGuiButtonFlags_AllowOverlap);
    bool hovered = ImGui::IsItemHovered();

    auto* dl = ImGui::GetWindowDrawList();

    // Folder occupies the same square as a card's thumbnail area. The MDI
    // folder glyph (filled when the folder has matching content, outline when
    // empty) is drawn big and tinted manilla so the silhouette matches UE5
    // without inventing custom geometry.
    ImVec2 fMin = {startPos.x + cardPad, startPos.y + cardPad};
    ImVec2 fMax = {fMin.x + imageSize, fMin.y + imageSize};

    ImU32 colFolder = filled ? IM_COL32(0xC8, 0x96, 0x5A, 0xFF)
                              : IM_COL32(0xA8, 0x80, 0x4D, 0xFF);
    if (selected)     colFolder = IM_COL32(0xE6, 0xB8, 0x78, 0xFF);
    else if (hovered) colFolder = IM_COL32(0xD2, 0xA0, 0x64, 0xFF);

    const char* iconTxt = filled ? ICON_MDI_FOLDER : ICON_MDI_FOLDER_OUTLINE;
    const float glyphSize = imageSize * 0.95f;
    ImFont* font = ImGui::GetFont();
    ImVec2 gts = font->CalcTextSizeA(glyphSize, FLT_MAX, 0.0f, iconTxt);
    dl->AddText(
      font, glyphSize,
      { (fMin.x + fMax.x - gts.x) * 0.5f,
        (fMin.y + fMax.y - gts.y) * 0.5f },
      colFolder, iconTxt
    );

    // Selection halo around the folder slot.
    if (selected) {
      dl->AddRect(
        {fMin.x - 2_px, fMin.y - 2_px},
        {fMax.x + 2_px, fMax.y + 2_px},
        IM_COL32(120, 160, 255, 255),
        5.0f, 0, 2.0f
      );
    }

    // Folder name — centered below the folder, single line, ellipsised on
    // overflow. Clipped to card width so very long names can't bleed into
    // the next cell. Skipped during rename (the overlay replaces this slot).
    bool isRenaming = (id == renamePath);
    if (!isRenaming) {
      float fontSize = ImGui::GetFontSize();
      float maxW = cardWidth - 2 * cardPad;

      std::string drawn = name;
      ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, drawn.c_str());
      if (ts.x > maxW) {
        const char* end = name.c_str() + name.size();
        const char* ell = "...";
        float ellW = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ell).x;
        const char* trunc = font->CalcWordWrapPositionA(
          1.0f, name.c_str(), end, std::max(0.0f, maxW - ellW));
        if (trunc <= name.c_str() && !name.empty()) trunc = name.c_str() + 1;
        drawn = std::string(name.c_str(), trunc);
        drawn += ell;
        ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, drawn.c_str());
      }
      ImVec2 textPos = {
        startPos.x + (cardWidth - ts.x) * 0.5f,
        fMax.y + gapThumbName
      };
      dl->PushClipRect(
        {startPos.x + cardPad, fMax.y},
        {startPos.x + cardWidth - cardPad, startPos.y + cardHeight},
        true
      );
      dl->AddText(font, fontSize, textPos,
                  IM_COL32(230, 230, 235, 255), drawn.c_str());
      dl->PopClipRect();
    }

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

      // Only the user's explicit click highlights the card. The currently
      // loaded scene used to share the same highlight, but that conflated
      // "what's open" with "what I just clicked" and made the browser feel
      // unresponsive on click. isLoaded still drives liveName so an unsaved
      // rename of the open scene shows up in its card.
      bool isLoaded = activeScene && (activeScene->getId() == scene.id);
      bool isUserSel = (selectedSceneId == scene.id);
      const auto &liveName = isLoaded ? activeScene->getName() : scene.name;
      const auto &displayName = liveName.empty() ? "(unnamed)" : liveName;

      if (!searchFilter.empty() && displayName.find(searchFilter) == std::string::npos) continue;

      checkLineBreak();

      std::string sceneId = "scene://" + std::to_string(scene.id);
      bool pressed = drawAssetCard(
        sceneId,
        ImTextureRef(nullptr),
        ICON_MDI_EARTH_BOX,
        displayName,
        "Scene",
        SCENE_TYPE_COLOR,
        isUserSel,
        1.0f,
        nullptr
      );
      bool isDblClick = ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered();

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
    std::string virtChild = joinDir(currentDir, folder);
    std::string folderId  = "folder://" + virtChild;

    bool filled = folderHasContent[folder];
    bool isFolderSel = (selectedFolder == virtChild);

    // Resolve abs path on the side that exists so it can double as the card
    // id — letting renamePath (also an abs path) match this card and trigger
    // the inline rename overlay below.
    fs::path assetSidePre  = assetsRootAbs  / virtChild;
    fs::path scriptSidePre = scriptsRootAbs / virtChild;
    std::string folderAbsId = (fs::exists(assetSidePre) ? assetSidePre : scriptSidePre).string();

    ImVec2 folderCardPos;
    bool clicked = drawFolderCard(folderAbsId, folder, filled, isFolderSel, &folderCardPos);
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
      navigateTo(virtChild);
      selectedFolder.clear();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Folder: %s\n(double-click to open)", virtChild.c_str());
    }

    if (folderAbsId == renamePath) drawCardRename(folderCardPos);
  }

  // Files
  for (const auto *assetPtr : assetItems)
  {
    const auto &asset = *assetPtr;
    // Display name without extension (UE5 parallel — extension is metadata,
    // not part of the user-facing name). Search still matches against the
    // stem, which is what users type.
    std::string displayName = fs::path(asset.name).stem().string();
    if (displayName.empty()) displayName = asset.name;
    if (!searchFilter.empty() && displayName.find(searchFilter) == std::string::npos) continue;

    checkLineBreak();

    auto icon = ImTextureRef(nullptr);
    const char* iconTxt = ICON_MDI_FILE_OUTLINE;
    switch (asset.type) {
      case FileType::IMAGE:       iconTxt = ICON_MDI_FILE_IMAGE_OUTLINE;       break;
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
    if (asset.texture) {
      icon = ImTextureRef(asset.texture->getGPUTex());
    }

    bool isSelected = (ctx.selAssetUUID == asset.getUUID());
    ImVec2 cardPos;
    bool clicked = drawAssetCard(
      asset.path,
      icon,
      iconTxt,
      displayName,
      assetTypeLabel(asset.type),
      assetTypeColor(asset.type),
      isSelected,
      asset.conf.exclude ? 0.25f : 1.0f,
      &cardPos
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
        ImGui::Image(icon, {64_px, 64_px});
      } else {
        ImGui::PushFont(nullptr, 32_px);
        ImGui::TextUnformatted(iconTxt);
        ImGui::PopFont();
      }
      ImGui::TextUnformatted(displayName.c_str());
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

    if (asset.path == renamePath) drawCardRename(cardPos);
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
  // Body is shared with the toolbar Add menu via the hoisted drawCreateMenu()
  // lambda; the wantsNew* bools it sets are consumed below.
  if (ImGui::BeginPopupContextWindow("AssetsBrowserCtx",
        ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
  {
    drawCreateMenu();
    ImGui::EndPopup();
  }
  ImGui::Dummy({0, 10_px});

  ImGui::EndChild();

  // Status footer — UE5-parallel "N items (M selected)" line. Selection is
  // mutually exclusive across files/scenes/folders (each click site clears
  // the others), so this collapses to 0 or 1.
  int selectedCount = (ctx.selAssetUUID != 0 ? 1 : 0)
                    + (selectedSceneId != 0 ? 1 : 0)
                    + (selectedFolder.empty() ? 0 : 1);
  if (selectedCount > 0) {
    ImGui::TextDisabled("%d item%s (%d selected)",
      visibleItems, visibleItems == 1 ? "" : "s", selectedCount);
  } else {
    ImGui::TextDisabled("%d item%s",
      visibleItems, visibleItems == 1 ? "" : "s");
  }

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
