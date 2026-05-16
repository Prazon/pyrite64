/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "settingsShell.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "IconsMaterialDesignIcons.h"
#include "../../imgui/helper.h"
#include "../../imgui/theme.h"

#include <cctype>

namespace
{
  std::string lower(const std::string &s) {
    std::string o; o.reserve(s.size());
    for (char c : s) o += (char)std::tolower((unsigned char)c);
    return o;
  }
}

void Editor::drawSettingsShell(const char *idStr,
                               const std::vector<SettingsCategory> &cats,
                               SettingsShellState &st)
{
  ImGui::PushID(idStr);

  // --- Search bar --------------------------------------------------------
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(ICON_MDI_MAGNIFY);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##search", "Search settings", &st.search);
  ImGui::Separator();

  const bool searching = !st.search.empty();
  const std::string q = lower(st.search);
  auto labelMatch = [&](const std::string &s) {
    return !searching || lower(s).find(q) != std::string::npos;
  };

  // If the query matches no category label/section, keep every category in
  // the tree so a row-level match inside any of them is still reachable
  // (the row filter, pushed below, does the precise narrowing).
  bool anyLabel = false;
  for (const auto &c : cats) {
    if (labelMatch(c.label) || labelMatch(c.section)) { anyLabel = true; break; }
  }
  const bool showAllInTree = searching && !anyLabel;
  auto inTree = [&](const SettingsCategory &c) {
    return !searching || showAllInTree || labelMatch(c.label) || labelMatch(c.section);
  };

  if (st.selectedId.empty() && !cats.empty()) st.selectedId = cats.front().id;

  // Keep the selection on a visible category so the content pane is never
  // blank while searching.
  bool selVisible = false;
  for (const auto &c : cats) {
    if (c.id == st.selectedId && inTree(c)) { selVisible = true; break; }
  }
  if (!selVisible) {
    for (const auto &c : cats) {
      if (inTree(c)) { st.selectedId = c.id; break; }
    }
  }

  // Push the search string into ImTable's row filter for the duration of
  // the content draw, then clear it so other windows are unaffected.
  ImTable::setFilter(st.search);

  // --- Left tree ---------------------------------------------------------
  ImGui::BeginChild("##left", ImVec2(200_px, 0), ImGuiChildFlags_Borders);
  std::string curSection;
  for (const auto &c : cats) {
    if (!inTree(c)) continue;
    if (c.section != curSection) {
      curSection = c.section;
      ImGui::SeparatorText(curSection.c_str());
    }
    std::string lbl = c.icon && *c.icon ? std::string(c.icon) + "  " + c.label : c.label;
    lbl += "##" + c.id;
    if (ImGui::Selectable(lbl.c_str(), st.selectedId == c.id)) {
      st.selectedId = c.id;
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();

  // --- Right content -----------------------------------------------------
  ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_Borders);
  for (const auto &c : cats) {
    if (c.id != st.selectedId) continue;
    std::string hdr = c.icon && *c.icon ? std::string(c.icon) + "  " + c.label : c.label;
    ImGui::SeparatorText(hdr.c_str());
    if (c.draw) c.draw();
    break;
  }
  ImGui::EndChild();

  ImTable::setFilter("");
  ImGui::PopID();
}
