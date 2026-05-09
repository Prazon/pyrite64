/**
* Per-asset AUDIO/MUSIC_XM editor window. The main pane is a thin
* placeholder for now (no waveform/playback infrastructure yet); the
* right strip hosts AssetInspector for sample-rate, compression, etc.
*/
#include "audioEditor.h"

#include "assetEditorDocking.h"
#include "../../../../context.h"
#include "../assetInspector.h"
#include "../../../imgui/helper.h"
#include "imgui_internal.h"

namespace
{
  ImVec2 DEF_WIN_SIZE{560, 360};
}

bool Editor::AudioEditor::draw(ImGuiID defDockId)
{
  auto &assetManager = ctx.project->getAssets();
  auto asset = assetManager.getEntryByUUID(assetUUID);
  if (!asset) return false;
  if (asset->type != Project::FileType::AUDIO
   && asset->type != Project::FileType::MUSIC_XM) return false;

  const char *kindLabel = (asset->type == Project::FileType::MUSIC_XM)
    ? "Music" : "Audio";
  winName = std::string(kindLabel) + ": " + asset->name
    + "###AudioEditorWin_" + std::to_string(assetUUID);

  Editor::setupAssetEditorDocking(defDockId, firstDockFrame);

  auto *mvp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(
    {
      mvp->Pos.x + (mvp->Size.x - DEF_WIN_SIZE.x) * 0.5f,
      mvp->Pos.y + (mvp->Size.y - DEF_WIN_SIZE.y) * 0.5f,
    },
    ImGuiCond_FirstUseEver
  );

  if (forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  ImGui::Begin(winName.c_str(), &isOpen);

  ImVec2 fullAvail = ImGui::GetContentRegionAvail();
  float splitterW  = 6_px;
  float minRightW  = 220_px;
  float minLeftW   = 160_px;
  float rightW     = std::clamp(fullAvail.x * splitFrac, minRightW, std::max(minRightW, fullAvail.x - minLeftW - splitterW));
  float leftW      = std::max(minLeftW, fullAvail.x - splitterW - rightW);

  ImGui::BeginChild("##audioMain", ImVec2(leftW, 0), ImGuiChildFlags_Borders);
  ImGui::TextDisabled("%s", kindLabel);
  ImGui::Separator();
  ImGui::TextWrapped("File: %s", asset->name.c_str());
  ImGui::Separator();
  ImGui::TextDisabled("(In-editor playback and waveform preview not yet implemented.)");
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::InvisibleButton("##audioSplit", ImVec2(splitterW, -1));
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    splitDragging = true;
    float dx = ImGui::GetIO().MouseDelta.x;
    if (fullAvail.x > splitterW * 2) {
      splitFrac -= dx / (fullAvail.x - splitterW);
      splitFrac = std::clamp(splitFrac, 0.20f, 0.60f);
    }
  } else {
    splitDragging = false;
  }
  if (ImGui::IsItemHovered() || splitDragging) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  {
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    ImU32 col = ImGui::GetColorU32(splitDragging ? ImGuiCol_SeparatorActive : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(
      {(a.x + b.x) * 0.5f - 1.0f, a.y},
      {(a.x + b.x) * 0.5f + 1.0f, b.y},
      col
    );
  }

  ImGui::SameLine();
  ImGui::BeginChild("##audioInspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
  Editor::AssetInspector::draw(assetUUID);
  ImGui::EndChild();

  ImGui::End();
  return isOpen;
}

void Editor::AudioEditor::focus() const
{
  ImGui::SetWindowFocus(winName.c_str());
}
