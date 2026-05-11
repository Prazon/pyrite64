/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "ImNodeFlow.h"

// Find-by-title overlay (Ctrl+F). Templated on the graph kind so the
// script-graph editor and the prefab event-graph editor (and any
// future graph editor) share one implementation. Selecting a result
// returns its uuid; the caller is expected to feed that to its own
// requestFocusNode() so the existing pan + flash machinery handles
// the actual visual seek.
namespace Editor::NodeFinder
{
  class State
  {
    public:
      void openPopup() {
        wantOpen   = true;
        query.clear();
        selected   = 0;
        focusOnOpen = true;
      }
      [[nodiscard]] bool wantsOpen() const { return wantOpen; }
      void clearWantOpen() { wantOpen = false; }

      // Returns a non-zero node uuid when the user accepts a result
      // this frame; 0 otherwise. Caller manages the popup ID through
      // ImGui::OpenPopup / BeginPopup around this call.
      template<typename GraphT, typename NodeBaseT>
      uint64_t draw(GraphT &g)
      {
        if (focusOnOpen) {
          ImGui::SetKeyboardFocusHere();
          focusOnOpen = false;
        }
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputTextWithHint("##finder_q", "Find node...", &query);

        struct Row { uint64_t uuid; const char* title; int score; };
        std::vector<Row> rows;
        rows.reserve(g.graph.getNodes().size());
        for (auto &kv : g.graph.getNodes()) {
          auto *n = static_cast<NodeBaseT*>(kv.second.get());
          if (!n) continue;
          const std::string &title = n->getName();
          int s = score(query.c_str(), title.c_str());
          if (s < 0) continue;
          rows.push_back({n->uuid, title.c_str(), s});
        }
        std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
          if (a.score != b.score) return a.score > b.score;
          return std::strcmp(a.title, b.title) < 0;
        });

        if (rows.empty()) {
          ImGui::TextDisabled("(no matches)");
          return 0;
        }

        if (selected < 0) selected = 0;
        if (selected >= static_cast<int>(rows.size())) {
          selected = static_cast<int>(rows.size()) - 1;
        }
        bool accept = false;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++selected;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   --selected;
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)
         || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) accept = true;
        selected = std::clamp(selected, 0, static_cast<int>(rows.size()) - 1);

        ImGui::BeginChild("##finder_rows",
                          ImVec2(300.0f, 280.0f),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        int idx = 0;
        for (const auto &r : rows) {
          bool sel = (idx == selected);
          if (ImGui::Selectable(r.title, sel,
                                ImGuiSelectableFlags_AllowDoubleClick)) {
            selected = idx;
            accept = true;
          }
          ++idx;
        }
        // Auto-scroll on keyboard nav.
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)
         || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
          ImGui::SetScrollY(static_cast<float>(selected)
                            * ImGui::GetTextLineHeightWithSpacing());
        }
        ImGui::EndChild();

        if (accept && selected >= 0 && selected < static_cast<int>(rows.size())) {
          return rows[selected].uuid;
        }
        return 0;
      }

    private:
      bool        wantOpen   = false;
      bool        focusOnOpen = false;
      std::string query;
      int         selected   = 0;

      // Mirrors nodePalette.cpp's fuzzy ranking: subsequence with bonus
      // for prefix and consecutive-character runs. Empty query matches
      // everything with score 0 so the full list shows.
      static int score(const char* pattern, const char* candidate)
      {
        if (!pattern || !*pattern) return 0;
        int s = 0;
        int run = 0;
        const char* p = pattern;
        const char* c = candidate;
        bool prevWasSep = true;
        while (*c) {
          char cl = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
          char pl = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
          if (*p && cl == pl) {
            s += 1 + run;
            if (prevWasSep) s += 4;
            ++run;
            ++p;
          } else {
            run = 0;
          }
          prevWasSep = (*c == ' ' || *c == '_' || *c == '-' || *c == '/');
          ++c;
        }
        return *p == '\0' ? s : -1;
      }
  };
}
