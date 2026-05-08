#include "compileErrorsWindow.h"

#include <string>

#include "imgui.h"
#include "IconsMaterialDesignIcons.h"

#include "../../../context.h"
#include "../../../utils/logger.h"
#include "../../../project/assetManager.h"
#include "../../../project/compile/compileErrors.h"
#include "../../pages/editorScene.h"

namespace
{
  ImU32 colorFor(Project::Compile::Severity sev)
  {
    return sev == Project::Compile::Severity::ERROR
      ? IM_COL32(0xF0, 0x55, 0x55, 0xFF)
      : IM_COL32(0xE6, 0xB4, 0x4C, 0xFF);
  }

  const char* iconFor(Project::Compile::Severity sev)
  {
    return sev == Project::Compile::Severity::ERROR
      ? ICON_MDI_ALERT_CIRCLE
      : ICON_MDI_ALERT;
  }

  std::string assetLabelFor(uint64_t assetUUID)
  {
    if(!ctx.project || assetUUID == 0) return "<no asset>";
    auto *entry = ctx.project->getAssets().getEntryByUUID(assetUUID);
    return entry ? entry->name : ("uuid:" + std::to_string(assetUUID));
  }
}

void Editor::CompileErrorsWindow::draw()
{
  const auto &all = ctx.compileErrors.all();
  size_t errCount  = ctx.compileErrors.errorCount();
  size_t warnCount = ctx.compileErrors.warningCount();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0, 0));

  // Toolbar — counts + clear. Counts left-justified, button right-aligned so
  // the panel reads at a glance like Log's toolbar.
  ImGui::BeginChild("CE_TOP", ImVec2(0, 22),
    ImGuiChildFlags_AlwaysUseWindowPadding,
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);

    if(errCount > 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, colorFor(Project::Compile::Severity::ERROR));
      ImGui::Text("%s %zu error%s", ICON_MDI_ALERT_CIRCLE, errCount, errCount == 1 ? "" : "s");
      ImGui::PopStyleColor();
      ImGui::SameLine();
    }
    if(warnCount > 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, colorFor(Project::Compile::Severity::WARNING));
      ImGui::Text("%s %zu warning%s", ICON_MDI_ALERT, warnCount, warnCount == 1 ? "" : "s");
      ImGui::PopStyleColor();
      ImGui::SameLine();
    }
    if(errCount == 0 && warnCount == 0) {
      ImGui::TextDisabled("No compile errors.");
      ImGui::SameLine();
    }

    float btnWidth = 64;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btnWidth);
    if(ImGui::Button("Clear", {btnWidth, 0})) {
      ctx.compileErrors.clear();
    }

    ImGui::PopStyleVar(2);
  ImGui::EndChild();

  // List body. One Selectable per row; double-click → navigate. Single-click
  // does nothing today — keep room for future keyboard navigation.
  ImGui::BeginChild("CE_LIST", ImVec2(0, 0), ImGuiChildFlags_Borders);

  if(all.empty()) {
    ImGui::Dummy({4, 8});
    ImGui::Indent(8);
    ImGui::TextDisabled("Run a build to see graph compile errors here.");
    ImGui::Unindent(8);
  }

  for(size_t i = 0; i < all.size(); ++i) {
    const auto &e = all[i];

    ImGui::PushID(static_cast<int>(i));
    ImGui::PushStyleColor(ImGuiCol_Text, colorFor(e.severity));

    std::string row = std::string{iconFor(e.severity)} + "  "
                    + assetLabelFor(e.assetUUID) + "  ·  " + e.message;
    bool selected = false;
    ImGui::Selectable(row.c_str(), &selected,
      ImGuiSelectableFlags_AllowDoubleClick);

    ImGui::PopStyleColor();

    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
      if(ctx.editorScene) {
        ctx.editorScene->revealCompileError(e);
      }
    }
    ImGui::PopID();
  }

  ImGui::EndChild();

  ImGui::PopStyleVar(2);
}
