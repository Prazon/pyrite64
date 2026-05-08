/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "imgui.h"

namespace Editor
{
  class MemoryDashboard
  {
    public:
      void draw();
      void refresh();

    private:
      enum class AssetCategory : int {
        Textures = 0,
        Models,
        Audio,
        Fonts,
        Scenes,
        Code,
        Other,
        _COUNT
      };

      struct AssetEntry {
        std::string name{};
        AssetCategory category{};
        uint64_t sizeBytes{0};
        std::string compression{};
      };

      // Cart size lives in ProjectConf now (see project.h). The dashboard is
      // a read-only view of the budget the user picked in Project Settings.
      uint64_t totalRomSize{0};
      uint64_t categoryTotals[static_cast<int>(AssetCategory::_COUNT)]{};
      std::vector<AssetEntry> entries{};
      int sortColumn{2}; // default sort by size
      bool sortAscending{false}; // default descending (largest first)
      bool hasData{false};

      void scanBuildOutputs();
      void sortEntries();
      void drawBudgetBar();
      void drawCategorySummary();
      void drawAssetTable();

      static const char* categoryName(AssetCategory cat);
      static ImVec4 categoryColor(AssetCategory cat);
  };
}
