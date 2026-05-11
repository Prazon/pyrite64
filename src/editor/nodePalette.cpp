/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "nodePalette.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace Editor::NodePalette
{
  namespace
  {
    // Per-popup state. Reset whenever ImGui reports the popup window is
    // appearing, so reopening with a new context starts clean.
    struct State {
      std::string query;
      int         selected = 0;   // index into the visible row list
      bool        focusOnOpen = true;
    };
    State g_state;

    // Subsequence-with-bonus fuzzy match. Higher score = better.
    // Returns -1 when no subsequence match exists. Bonuses bias toward
    // start-of-token hits and consecutive-character runs so "var" beats
    // "Variable Get" against an entry like "Get Variable" only when the
    // pattern actually appears that way.
    int fuzzyScore(const char* pattern, const char* candidate)
    {
      if (!pattern || !*pattern) return 0; // empty query matches everything
      int score = 0;
      int run = 0;
      const char* p = pattern;
      const char* c = candidate;
      bool prevWasSep = true;
      while (*c) {
        char cl = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
        char pl = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        if (*p && cl == pl) {
          score += 1 + run;
          if (prevWasSep) score += 4;
          ++run;
          ++p;
        } else {
          run = 0;
        }
        prevWasSep = (*c == ' ' || *c == '_' || *c == '-' || *c == '/');
        ++c;
      }
      return *p == '\0' ? score : -1;
    }

    // Direction-aware compatibility: an OUT-pin drag wants nodes with a
    // matching IN slot; an IN-pin drag wants nodes with a matching OUT
    // slot. Pin direction is exposed via Pin::getType (NORMAL/OUT/IN);
    // ImNodeFlow's enum is PinType, with INPUT and OUTPUT values.
    bool isCompatible(const Entry &e, ImFlow::Pin* dragSrc)
    {
      if (!dragSrc) return true;
      // The dragged pin's style pointer maps back to a singleton in
      // nodeStyles.cpp. Find which PinDataType it corresponds to by
      // scanning the singleton table once per check (small constant).
      auto srcStyle = dragSrc->getStyle().get();
      TypeMask wanted = 0;
      for (uint32_t i = 0; i <= static_cast<uint32_t>(::Project::Graph::PinDataType::MatProp); ++i) {
        auto t = static_cast<::Project::Graph::PinDataType>(i);
        if (::Project::Graph::pinStyle(t).get() == srcStyle) {
          wanted = maskOf(t);
          break;
        }
      }
      if (wanted == 0) return true; // unknown style; don't filter
      // Output-pin drags look for nodes that can receive the type;
      // input-pin drags look for nodes that produce it.
      const bool dragIsOutput = (dragSrc->getType() == ImFlow::PinType_Output);
      return dragIsOutput ? (e.inTypes & wanted) != 0
                          : (e.outTypes & wanted) != 0;
    }

    struct Row {
      const Entry* entry;
      int          score;
    };

    // Sort by category then alpha within. Used in the no-query view.
    void buildCategorised(std::span<const Entry> all,
                          ImFlow::Pin* dragSrc,
                          std::vector<Row> &out)
    {
      out.clear();
      for (const auto &e : all) {
        if (!isCompatible(e, dragSrc)) continue;
        out.push_back({&e, 0});
      }
      std::sort(out.begin(), out.end(), [](const Row &a, const Row &b) {
        int c = std::strcmp(a.entry->category, b.entry->category);
        if (c != 0) return c < 0;
        return std::strcmp(a.entry->name, b.entry->name) < 0;
      });
    }

    // Ranked flat list when a query is present.
    void buildRanked(std::span<const Entry> all,
                     ImFlow::Pin* dragSrc,
                     const std::string &q,
                     std::vector<Row> &out)
    {
      out.clear();
      for (const auto &e : all) {
        if (!isCompatible(e, dragSrc)) continue;
        int s = fuzzyScore(q.c_str(), e.name);
        if (s < 0) continue;
        out.push_back({&e, s});
      }
      std::sort(out.begin(), out.end(), [](const Row &a, const Row &b) {
        if (a.score != b.score) return a.score > b.score;
        return std::strcmp(a.entry->name, b.entry->name) < 0;
      });
    }
  }

  ImFlow::Pin* firstMatchingInputPin(ImFlow::BaseNode* node, ImFlow::Pin* dragSrc)
  {
    if (!node || !dragSrc) return nullptr;
    auto srcStyle = dragSrc->getStyle().get();
    auto &ins = node->getIns();
    for (auto &p : ins) {
      if (p && p->getStyle().get() == srcStyle) return p.get();
    }
    return nullptr;
  }

  bool draw(std::span<const Entry> entries,
            ImFlow::Pin* dragSourcePin,
            uint32_t* outTypeIndex)
  {
    // Reset state on reopen. IsWindowAppearing() is true on the very
    // first frame the popup window exists this open; the focus flag is
    // used by SetKeyboardFocusHere on the input below.
    if (ImGui::IsWindowAppearing()) {
      g_state.query.clear();
      g_state.selected = 0;
      g_state.focusOnOpen = true;
    }

    ImGui::TextUnformatted(dragSourcePin ? "Add Node (filtered)" : "Add Node");
    ImGui::Separator();

    if (g_state.focusOnOpen) {
      ImGui::SetKeyboardFocusHere();
      g_state.focusOnOpen = false;
    }
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##palette_q", "Search...", &g_state.query);

    static std::vector<Row> rows;
    if (g_state.query.empty()) {
      buildCategorised(entries, dragSourcePin, rows);
    } else {
      buildRanked(entries, dragSourcePin, g_state.query, rows);
    }

    if (rows.empty()) {
      ImGui::TextDisabled("(no matches)");
      return false;
    }

    // Clamp selection within the visible list.
    if (g_state.selected < 0) g_state.selected = 0;
    if (g_state.selected >= static_cast<int>(rows.size())) {
      g_state.selected = static_cast<int>(rows.size()) - 1;
    }

    // Keyboard nav. We bind these on the popup window so they don't
    // leak to the underlying editor when the palette is open.
    bool accept = false;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++g_state.selected;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   --g_state.selected;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)
     || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) accept = true;
    g_state.selected = std::clamp(g_state.selected, 0, static_cast<int>(rows.size()) - 1);

    ImGui::BeginChild("##palette_rows",
                      ImVec2(280.0f, 280.0f),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    int rowIdx = 0;
    if (g_state.query.empty()) {
      // Categorised view: insert pseudo-headers between category groups.
      const char* lastCat = nullptr;
      for (const auto &r : rows) {
        if (lastCat == nullptr || std::strcmp(lastCat, r.entry->category) != 0) {
          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xC8, 0xA0, 0x6E, 0xFF));
          ImGui::TextUnformatted(r.entry->category);
          ImGui::PopStyleColor();
          ImGui::Separator();
          lastCat = r.entry->category;
        }
        bool sel = (rowIdx == g_state.selected);
        if (ImGui::Selectable(r.entry->name, sel,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
          g_state.selected = rowIdx;
          accept = true;
        }
        if (sel && ImGui::IsKeyPressed(ImGuiKey_Space, false)) accept = true;
        ++rowIdx;
      }
    } else {
      for (const auto &r : rows) {
        bool sel = (rowIdx == g_state.selected);
        if (ImGui::Selectable(r.entry->name, sel,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
          g_state.selected = rowIdx;
          accept = true;
        }
        ++rowIdx;
      }
    }

    // Auto-scroll to keep the selected row visible during keyboard nav.
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)
     || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
      // Approximate row height via ImGui's text line height with spacing.
      float rowH = ImGui::GetTextLineHeightWithSpacing();
      ImGui::SetScrollY(static_cast<float>(g_state.selected) * rowH);
    }

    ImGui::EndChild();

    if (accept && g_state.selected >= 0
              && g_state.selected < static_cast<int>(rows.size())) {
      *outTypeIndex = rows[g_state.selected].entry->typeIndex;
      return true;
    }
    return false;
  }
}
