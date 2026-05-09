/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <array>
#include <string>

#include "../../../renderer/texture.h"

namespace Editor
{
  class AssetsBrowser
  {
    public:
      // One toggle per file kind shown in the unified Content view. Order
      // is the display order in the left rail and is also the index into
      // CHIP_DEFS (assetsBrowser.cpp). All-on by default; reset per session.
      // Public so the .cpp's CHIP_DEFS table can index by these names.
      enum ChipKind : int {
        CHIP_SCENES = 0,
        CHIP_PREFABS,
        CHIP_IMAGES,
        CHIP_MODELS,
        CHIP_AUDIO,
        CHIP_MUSIC_XM,
        CHIP_FONTS,
        CHIP_CODE_OBJ,
        CHIP_CODE_GLOBAL,
        CHIP_NODE_GRAPH,
        CHIP_RESOURCE_TYPE,
        CHIP_RESOURCE_INSTANCE,
        CHIP_MATERIAL,
        CHIP_COUNT
      };

      // Split-mode tab indices. Order matches the TabBar in draw().
      enum SplitTab : int {
        TAB_SCENES  = 0,
        TAB_ASSETS  = 1,
        TAB_SCRIPTS = 2,
        TAB_PREFABS = 3,
        TAB_COUNT   = 4,
      };

    private:
      std::array<bool, CHIP_COUNT> chips{};
      // Width of the left chip rail in pixels. Session-only; not persisted.
      // Clamped at draw time so it can never starve the grid pane.
      float chipPanelWidth{110.0f};
      // Virtual content-browser path; "" = Content/ root. Maps onto both
      // assets/<currentDir> and src/user/<currentDir> in parallel.
      // In Split mode this aliases tabDirs[activeTab] each frame.
      std::string currentDir{};
      // Split-mode state: each tab keeps its own nav stack so switching
      // tabs doesn't clobber where the user was browsing in another.
      int activeTab{TAB_ASSETS};
      std::array<std::string, TAB_COUNT> tabDirs{};
      // Single-click selection for non-asset items. Files use ctx.selAssetUUID;
      // these mirror that pattern for folders / scenes so single-click only
      // highlights and double-click is what actually navigates / loads.
      std::string selectedFolder{};
      int selectedSceneId{0};
      std::string searchFilter{};
      std::string renamePath{};
      std::string deletePath{};
      // Pending folder delete (virtual path under Content/). Populated when the
      // user clicks Delete on a folder; the modal in draw() resolves it across
      // both physical roots and any nested scenes.
      std::string deleteFolderPath{};
      // Pending move target for a scene drag-drop. Folder cells set both fields
      // when they accept a SCENE payload; the move is applied after the grid
      // pass so we don't mutate SceneManager mid-iteration.
      int pendingSceneMoveId{0};
      std::string pendingSceneMoveTarget{};
      char renameBuffer[256]{};

    public:
      AssetsBrowser() { chips.fill(true); }
      void draw();
      void showContextMenu(const std::string& path);
  };
}
