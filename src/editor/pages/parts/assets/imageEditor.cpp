/**
* SPBF64 fork: in-editor image preview window with zoom/pan/checker
* and a sprite-sheet slicing overlay (visualization-only, mirrors the
* SpriteBillboard component's cellW/cellH/pivot/frame fields).
*/
#include "imageEditor.h"

#include "../../../../context.h"
#include "../../../imgui/helper.h"
#include "imgui_internal.h"

namespace
{
  ImVec2 DEF_WIN_SIZE{520, 520};

  float zoomFactor(Editor::ImageEditor::ZoomMode m, ImVec2 avail, int imgW, int imgH)
  {
    using ZM = Editor::ImageEditor::ZoomMode;
    switch (m) {
      case ZM::X1:  return 1.0f;
      case ZM::X2:  return 2.0f;
      case ZM::X4:  return 4.0f;
      case ZM::X8:  return 8.0f;
      case ZM::X16: return 16.0f;
      case ZM::Fit:
      default: {
        if (imgW <= 0 || imgH <= 0 || avail.x <= 0 || avail.y <= 0) return 1.0f;
        float fx = avail.x / (float)imgW;
        float fy = avail.y / (float)imgH;
        return std::min(fx, fy);
      }
    }
  }

  void drawCheckerBackground(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell)
  {
    constexpr ImU32 c0 = IM_COL32(60, 60, 60, 255);
    constexpr ImU32 c1 = IM_COL32(90, 90, 90, 255);
    dl->AddRectFilled(a, b, c0);
    bool toggle = false;
    for (float y = a.y; y < b.y; y += cell) {
      bool t = toggle;
      for (float x = a.x; x < b.x; x += cell) {
        if (t) {
          ImVec2 ca{x, y};
          ImVec2 cb{std::min(x + cell, b.x), std::min(y + cell, b.y)};
          dl->AddRectFilled(ca, cb, c1);
        }
        t = !t;
      }
      toggle = !toggle;
    }
  }
}

bool Editor::ImageEditor::draw(ImGuiID defDockId)
{
  auto &assetManager = ctx.project->getAssets();
  auto asset = assetManager.getEntryByUUID(assetUUID);
  if (!asset) return false;
  if (asset->type != Project::FileType::IMAGE) return false;

  // Stable ImGui ID via ###suffix so renaming the asset (display title) doesn't
  // throw away the window's saved position/dock state. The Win suffix also
  // invalidates pre-multi-viewport imgui.ini entries that had no ### at all.
  winName = "Image: " + asset->name
    + "###ImageEditorWin_" + std::to_string(assetUUID);

  // Dock as a sibling tab of Scene Editor; OS chrome on undock — see
  // PrefabEditor::draw for rationale.
  ImGuiWindowClass cls{};
  cls.ViewportFlagsOverrideSet   = ImGuiViewportFlags_NoAutoMerge;
  cls.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
  ImGui::SetNextWindowClass(&cls);

  if (defDockId) ImGui::SetNextWindowDockID(defDockId, ImGuiCond_FirstUseEver);

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

  if (!asset->texture) {
    ImGui::TextDisabled("Image is not loaded.");
    ImGui::End();
    return isOpen;
  }

  int imgW = asset->texture->getWidth();
  int imgH = asset->texture->getHeight();

  drawToolbar(imgW, imgH);

  ImGui::Separator();

  // Reserve a vertical slice for the slicing panel below the canvas.
  float slicePanelH = sliceShow ? 132_px : 28_px;
  ImVec2 fullAvail = ImGui::GetContentRegionAvail();
  float canvasH = std::max(64_px, fullAvail.y - slicePanelH - 4_px);

  ImGui::BeginChild("##canvas", ImVec2(0, canvasH), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  drawCanvas(ImTextureID(asset->texture->getGPUTex()), imgW, imgH);
  ImGui::EndChild();

  drawSlicePanel(imgW, imgH);

  ImGui::End();
  return isOpen;
}

void Editor::ImageEditor::drawToolbar(int imgW, int imgH)
{
  static const char* ZOOM_LABELS[] = { "Fit", "1:1", "2x", "4x", "8x", "16x" };
  int zi = (int)zoomMode;
  ImGui::SetNextItemWidth(80_px);
  if (ImGui::Combo("##zoom", &zi, ZOOM_LABELS, IM_ARRAYSIZE(ZOOM_LABELS))) {
    zoomMode = (ZoomMode)zi;
    panOffset = {0, 0};
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset View")) {
    zoomMode = ZoomMode::Fit;
    panOffset = {0, 0};
  }
  ImGui::SameLine();
  ImGui::Checkbox("Checker", &showChecker);

  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12_px);
  ImGui::TextDisabled("%dx%dpx", imgW, imgH);
}

void Editor::ImageEditor::drawCanvas(ImTextureID tex, int imgW, int imgH)
{
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 areaMin = origin;
  ImVec2 areaMax{origin.x + avail.x, origin.y + avail.y};

  ImDrawList* dl = ImGui::GetWindowDrawList();

  if (showChecker) {
    drawCheckerBackground(dl, areaMin, areaMax, 8_px);
  } else {
    dl->AddRectFilled(areaMin, areaMax, IM_COL32(40, 40, 40, 255));
  }

  // Capture interactions over the canvas area.
  ImGui::InvisibleButton("##canvasHit", avail,
                         ImGuiButtonFlags_MouseButtonLeft |
                         ImGuiButtonFlags_MouseButtonMiddle |
                         ImGuiButtonFlags_MouseButtonRight);
  bool hovered = ImGui::IsItemHovered();
  bool active  = ImGui::IsItemActive();

  ImGuiIO &io = ImGui::GetIO();

  // Wheel zoom: cycle through ZoomMode values, anchored at cursor.
  if (hovered && io.MouseWheel != 0.0f) {
    int n = (int)ZoomMode::X16 + 1;
    int curr = (int)zoomMode;
    int next = std::clamp(curr + (io.MouseWheel > 0 ? 1 : -1), 0, n - 1);
    if (next != curr) {
      // Try to keep the pixel under the cursor stationary.
      float oldZ = zoomFactor(zoomMode, avail, imgW, imgH);
      ZoomMode prev = zoomMode;
      zoomMode = (ZoomMode)next;
      float newZ = zoomFactor(zoomMode, avail, imgW, imgH);

      ImVec2 cursorRel{io.MousePos.x - (areaMin.x + avail.x * 0.5f + panOffset.x),
                       io.MousePos.y - (areaMin.y + avail.y * 0.5f + panOffset.y)};
      float scale = (oldZ > 0.0f) ? (newZ / oldZ) : 1.0f;
      panOffset.x += cursorRel.x * (1.0f - scale);
      panOffset.y += cursorRel.y * (1.0f - scale);
      (void)prev;
    }
  }

  // Middle / right drag = pan
  if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                 ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
    ImVec2 d = io.MouseDelta;
    panOffset.x += d.x;
    panOffset.y += d.y;
  }

  // Compute draw rect for the image (centered in canvas, offset by panOffset).
  float z = zoomFactor(zoomMode, avail, imgW, imgH);
  ImVec2 drawSize{imgW * z, imgH * z};
  ImVec2 center{areaMin.x + avail.x * 0.5f + panOffset.x,
                areaMin.y + avail.y * 0.5f + panOffset.y};
  ImVec2 a{center.x - drawSize.x * 0.5f, center.y - drawSize.y * 0.5f};
  ImVec2 b{a.x + drawSize.x, a.y + drawSize.y};

  // Clip to canvas so panned content doesn't leak into the toolbar/slice panel.
  dl->PushClipRect(areaMin, areaMax, true);
  dl->AddImage(tex, a, b);

  // Sprite-slicing overlay
  if (sliceShow && sliceCellW > 0 && sliceCellH > 0) {
    constexpr ImU32 gridCol = IM_COL32(0, 200, 255, 140);
    constexpr ImU32 frameCol = IM_COL32(255, 220, 0, 230);
    constexpr ImU32 pivotCol = IM_COL32(255, 80, 80, 255);

    int cols = std::max(1, imgW / sliceCellW);
    int rows = std::max(1, imgH / sliceCellH);

    for (int x = 0; x <= cols; ++x) {
      float fx = a.x + x * sliceCellW * z;
      dl->AddLine({fx, a.y}, {fx, std::min(a.y + rows * sliceCellH * z, b.y)}, gridCol, 1.0f);
    }
    for (int y = 0; y <= rows; ++y) {
      float fy = a.y + y * sliceCellH * z;
      dl->AddLine({a.x, fy}, {std::min(a.x + cols * sliceCellW * z, b.x), fy}, gridCol, 1.0f);
    }

    int totalCells = cols * rows;
    if (totalCells > 0) {
      int fi = ((sliceFrame % totalCells) + totalCells) % totalCells;
      int cx = fi % cols;
      int cy = fi / cols;
      ImVec2 fa{a.x + cx * sliceCellW * z, a.y + cy * sliceCellH * z};
      ImVec2 fb{fa.x + sliceCellW * z, fa.y + sliceCellH * z};
      dl->AddRect(fa, fb, frameCol, 0.0f, 0, 2.0f);

      ImVec2 pv{fa.x + slicePivotX * z, fa.y + slicePivotY * z};
      dl->AddCircleFilled(pv, std::max(2.0f, 2.0f * z), pivotCol);
      dl->AddCircle(pv, std::max(3.0f, 3.0f * z), IM_COL32(0, 0, 0, 200));
    }
  }

  dl->PopClipRect();
}

void Editor::ImageEditor::drawSlicePanel(int imgW, int imgH)
{
  ImGui::Checkbox("Sprite Slicing", &sliceShow);

  if (!sliceShow) return;

  ImGui::SameLine();
  ImGui::TextDisabled(" — visualization only; copy values into a SpriteBillboard component");

  ImGui::PushItemWidth(80_px);
  ImGui::InputInt("Cell W", &sliceCellW, 1, 8);
  ImGui::SameLine();
  ImGui::InputInt("Cell H", &sliceCellH, 1, 8);

  ImGui::InputInt("Pivot X", &slicePivotX, 1, 8);
  ImGui::SameLine();
  ImGui::InputInt("Pivot Y", &slicePivotY, 1, 8);

  ImGui::InputInt("Frame", &sliceFrame, 1, 4);
  ImGui::PopItemWidth();

  // Clamp to sane ranges so a stray value doesn't crash the overlay math.
  sliceCellW = std::clamp(sliceCellW, 1, std::max(1, imgW));
  sliceCellH = std::clamp(sliceCellH, 1, std::max(1, imgH));
  slicePivotX = std::clamp(slicePivotX, 0, sliceCellW);
  slicePivotY = std::clamp(slicePivotY, 0, sliceCellH);

  int cols = std::max(1, imgW / sliceCellW);
  int rows = std::max(1, imgH / sliceCellH);
  ImGui::SameLine();
  ImGui::TextDisabled(" %d x %d cells (%d total)", cols, rows, cols * rows);
}

void Editor::ImageEditor::focus() const
{
  ImGui::SetWindowFocus(winName.c_str());
}
