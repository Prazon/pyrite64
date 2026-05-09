/**
* Per-asset RESOURCE_TYPE editor window. Hosts the schema editor on the
* left and AssetInspector on the right.
*/
#include "resourceTypeEditorWindow.h"

#include "assetEditorDocking.h"
#include "resourceTypeEditor.h"
#include "../assetInspector.h"
#include "../../../imgui/helper.h"
#include "../../../../context.h"
#include "imgui_internal.h"

namespace
{
  ImVec2 DEF_WIN_SIZE{640, 480};
}

bool Editor::ResourceTypeEditorWindow::draw(ImGuiID defDockId)
{
  auto &assetManager = ctx.project->getAssets();
  auto asset = assetManager.getEntryByUUID(assetUUID);
  if (!asset) return false;
  if (asset->type != Project::FileType::RESOURCE_TYPE) return false;

  winName = "Resource Type: " + asset->name
    + "###ResourceTypeEditorWin_" + std::to_string(assetUUID);

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
  float minLeftW   = 240_px;
  float rightW     = std::clamp(fullAvail.x * splitFrac, minRightW, std::max(minRightW, fullAvail.x - minLeftW - splitterW));
  float leftW      = std::max(minLeftW, fullAvail.x - splitterW - rightW);

  ImGui::BeginChild("##restypeMain", ImVec2(leftW, 0), ImGuiChildFlags_Borders);
  if (asset->resourceType) {
    Editor::ResourceTypeEditor::draw(*asset);
  } else {
    ImGui::TextDisabled("(resource type body missing — open the .p64restype in a text editor to repair)");
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::InvisibleButton("##restypeSplit", ImVec2(splitterW, -1));
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    splitDragging = true;
    float dx = ImGui::GetIO().MouseDelta.x;
    if (fullAvail.x > splitterW * 2) {
      splitFrac -= dx / (fullAvail.x - splitterW);
      splitFrac = std::clamp(splitFrac, 0.20f, 0.55f);
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
  ImGui::BeginChild("##restypeInspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
  Editor::AssetInspector::draw(assetUUID);
  ImGui::EndChild();

  ImGui::End();
  return isOpen;
}

void Editor::ResourceTypeEditorWindow::focus() const
{
  ImGui::SetWindowFocus(winName.c_str());
}
