/**
* Editor 2D viewport implementation.
* See viewport2D.h for the architectural notes. The actual per-component
* painting lives next to the component definitions (compXxx2D.cpp); this
* file just walks the scene tree, transforms canvas coords to screen, and
* dispatches into Component::TABLE[id].funcDraw2D.
*/
#include "viewport2D.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "../../../context.h"
#include "../../../project/component/components.h"
#include "../../../project/scene/scene.h"
#include "../../../project/scene/sceneManager.h"
#include "../../../project/assetManager.h"
#include "../../../project/selection.h"
#include "../../../project/scene/object.h"
#include "../../undoRedo.h"
#include "../../dragDropPayloads.h"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace
{
  // Anchor table mirrors sceneBuilder.cpp: 0=TL, 1=TC, 2=TR, 3=ML, 4=C,
  // 5=MR, 6=BL, 7=BC, 8=BR.
  ImVec2 anchorOffsetForCanvas(int anchor, int fbW, int fbH)
  {
    int col = anchor % 3;
    int row = anchor / 3;
    float ox = (col == 1 ? fbW * 0.5f : (col == 2 ? (float)fbW : 0.0f));
    float oy = (row == 1 ? fbH * 0.5f : (row == 2 ? (float)fbH : 0.0f));
    return ImVec2(ox, oy);
  }
}

Editor::Viewport2D::Viewport2D() {}

Editor::Viewport2D::Viewport2D(Project::Scene &scene, Project::Selection &selection)
  : boundScene{&scene}, boundSelection{&selection}
{}

ImVec2 Editor::Viewport2D::canvasToScreen(ImVec2 c) const {
  return ImVec2(origin.x + c.x * zoom, origin.y + c.y * zoom);
}

ImVec2 Editor::Viewport2D::screenToCanvas(ImVec2 s) const {
  return ImVec2((s.x - origin.x) / zoom, (s.y - origin.y) / zoom);
}

void Editor::Viewport2D::paintObject(
    Project::Scene &scene, Project::Selection &sel,
    Project::Object &obj, ImDrawList *dl,
    bool inCanvas, ImVec2 anchorOffset)
{
  bool isThisCanvas = obj.isCanvas2D;
  bool effectiveInCanvas = inCanvas || isThisCanvas;

  // Canvases set the framebuffer-space origin for their subtree; nested
  // anchors stack on top of the parent-resolved origin.
  ImVec2 nodeAnchor = anchorOffset;
  if (isThisCanvas) {
    nodeAnchor = anchorOffsetForCanvas(obj.anchor2D, fbW, fbH);
  } else if (effectiveInCanvas && obj.anchor2D != 0) {
    auto extra = anchorOffsetForCanvas(obj.anchor2D, fbW, fbH);
    nodeAnchor.x += extra.x;
    nodeAnchor.y += extra.y;
  }

  if (effectiveInCanvas) {
    auto pos = obj.pos.resolve(obj.propOverrides);
    float canvasX = nodeAnchor.x + pos.x;
    float canvasY = nodeAnchor.y + pos.y;
    ImVec2 originScreen = canvasToScreen({canvasX, canvasY});

    // Track the union of every 2D component's bounding rect on this
    // Object so a selected Object gets one outline rather than per-
    // component noise. Last-painted wins for hover hit-test.
    ImVec2 boundsMin{0,0}, boundsMax{0,0};
    bool haveBounds = false;
    bool isSelected = sel.isSelected(obj.uuid);

    for (auto &comp : obj.components) {
      if (comp.id < 0 || (size_t)comp.id >= Project::Component::TABLE.size()) continue;
      const auto &info = Project::Component::TABLE[comp.id];
      if (!info.funcDraw2D) continue;

      ImVec2 cMin{}, cMax{};
      info.funcDraw2D(obj, comp, dl, originScreen, zoom, &cMin, &cMax);

      if (cMax.x > cMin.x && cMax.y > cMin.y) {
        if (!haveBounds) { boundsMin = cMin; boundsMax = cMax; haveBounds = true; }
        else {
          boundsMin.x = std::min(boundsMin.x, cMin.x);
          boundsMin.y = std::min(boundsMin.y, cMin.y);
          boundsMax.x = std::max(boundsMax.x, cMax.x);
          boundsMax.y = std::max(boundsMax.y, cMax.y);
        }
      }
    }

    if (haveBounds) {
      ImVec2 mp = ImGui::GetMousePos();
      if (mp.x >= boundsMin.x && mp.x <= boundsMax.x
          && mp.y >= boundsMin.y && mp.y <= boundsMax.y) {
        hoveredUUID = obj.uuid;
      }

      if (isSelected) {
        dl->AddRect(boundsMin, boundsMax, IM_COL32(255, 200, 0, 255), 0.0f, 0, 2.0f);
        ImVec2 piv = canvasToScreen({canvasX, canvasY});
        dl->AddCircleFilled(piv, 3.0f, IM_COL32(255, 80, 80, 220));
      }
    }
  }

  for (auto &child : obj.children) {
    paintObject(scene, sel, *child, dl, effectiveInCanvas, nodeAnchor);
  }
}

void Editor::Viewport2D::draw()
{
  // Resolve scene + selection bindings. Default-constructed Viewport2D falls
  // back to the project's loaded scene + main selection for backward
  // compatibility with the outer scene editor's 2D-Viewport tab.
  Project::Scene *scene = boundScene
    ? boundScene
    : (ctx.project ? ctx.project->getScenes().getLoadedScene() : nullptr);
  Project::Selection *selPtr = boundSelection ? boundSelection : &ctx.mainSelection;

  if (!scene || !selPtr) {
    ImGui::TextDisabled("(no scene loaded)");
    return;
  }
  Project::Selection &selection = *selPtr;

  fbW = scene->conf.fbWidth;
  fbH = scene->conf.fbHeight;
  if (fbW <= 0) fbW = 320;
  if (fbH <= 0) fbH = 240;

  if (ImGui::SmallButton("Reset View")) {
    originInit = false;
    zoom = canvasMode ? 3.0f : 2.0f;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("zoom %.2fx  |  hover: %s%s",
    zoom, hoveredUUID ? "yes" : "no",
    canvasMode ? "  |  CANVAS" : "");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80.0f);
  constexpr const char* SNAP_LABELS[] = {"1px", "4px", "8px", "12px", "16px", "32px"};
  constexpr int SNAP_VALUES[] = {1, 4, 8, 12, 16, 32};
  int snapIdx = 0;
  for (int i = 0; i < 6; ++i) if (SNAP_VALUES[i] == snapStep) { snapIdx = i; break; }
  if (ImGui::Combo("snap", &snapIdx, SNAP_LABELS, 6)) {
    snapStep = SNAP_VALUES[snapIdx];
  }

  ImVec2 canvasTL = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  if (canvasSize.x < 10.0f || canvasSize.y < 10.0f) return;

  if (!originInit) {
    if (canvasMode) zoom = 3.0f;
    origin.x = canvasTL.x + (canvasSize.x - fbW * zoom) * 0.5f;
    origin.y = canvasTL.y + (canvasSize.y - fbH * zoom) * 0.5f;
    originInit = true;
  }

  ImGui::InvisibleButton("##v2dCanvas", canvasSize,
    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
  bool hovered = ImGui::IsItemHovered();

  // Canvas-mode palette drop: spawn a new widget Object under the blueprint
  // root at the drop position. Wired here (rather than on a per-Object
  // target like the SceneGraph drop) so users can drop into empty canvas
  // space and land at the cursor, matching UMG's Designer behavior.
  if (canvasMode && ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
          Editor::DragDrop::TYPE_WIDGET_PALETTE)) {
      const auto *pl = (const Editor::DragDrop::WidgetPalettePayload*)payload->Data;
      auto &root = scene->getRootObject();
      Project::Object *canvasRoot = root.children.empty()
        ? nullptr : root.children.front().get();
      if (canvasRoot) {
        auto child = scene->addObject(*canvasRoot);
        if (child) {
          ImVec2 mp = ImGui::GetMousePos();
          ImVec2 mpC = screenToCanvas(mp);
          glm::vec3 p = child->pos.value;
          p.x = std::round(mpC.x);
          p.y = std::round(mpC.y);
          p.z = 0.0f;
          child->pos.value = p;
          child->name = "Widget";
          child->addComponent((int)pl->componentID);
          selection.set(child->uuid);
          Editor::UndoRedo::getHistory().markChanged("Add Widget");
        }
      }
    }
    ImGui::EndDragDropTarget();
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();

  ImVec2 bgTL = canvasTL;
  ImVec2 bgBR{canvasTL.x + canvasSize.x, canvasTL.y + canvasSize.y};
  dl->AddRectFilled(bgTL, bgBR, IM_COL32(20, 20, 22, 255));

  ImVec2 fbTL = canvasToScreen({0.0f, 0.0f});
  ImVec2 fbBR = canvasToScreen({(float)fbW, (float)fbH});

  if (canvasMode) {
    // Checkerboard inside the framebuffer rect: makes alpha visible and
    // signals "this is the screen" without committing to a particular
    // backdrop color. 8-pixel cells (in canvas space) read clearly across
    // the typical 2x..6x zoom range.
    constexpr int CELL = 8;
    int cellsX = (fbW + CELL - 1) / CELL;
    int cellsY = (fbH + CELL - 1) / CELL;
    for (int cy = 0; cy < cellsY; ++cy) {
      for (int cx = 0; cx < cellsX; ++cx) {
        bool dark = ((cx + cy) & 1) != 0;
        ImU32 col = dark ? IM_COL32(36, 36, 40, 255) : IM_COL32(54, 54, 60, 255);
        ImVec2 a = canvasToScreen({(float)(cx * CELL),       (float)(cy * CELL)});
        float bx = std::min((float)((cx + 1) * CELL), (float)fbW);
        float by = std::min((float)((cy + 1) * CELL), (float)fbH);
        ImVec2 b = canvasToScreen({bx, by});
        dl->AddRectFilled(a, b, col);
      }
    }
    dl->AddRect(fbTL, fbBR, IM_COL32(220, 180, 80, 255), 0.0f, 0, 2.0f);
  } else {
    dl->AddRectFilled(fbTL, fbBR, IM_COL32(40, 40, 48, 255));
    dl->AddRect(fbTL, fbBR, IM_COL32(120, 120, 130, 200), 0.0f, 0, 1.0f);

    // 10% safe-area inset, NTSC convention. Hidden in canvas mode because
    // widget authors commonly intend to fill the whole framebuffer.
    float insetX = fbW * 0.1f;
    float insetY = fbH * 0.1f;
    ImVec2 saTL = canvasToScreen({insetX, insetY});
    ImVec2 saBR = canvasToScreen({fbW - insetX, fbH - insetY});
    dl->AddRect(saTL, saBR, IM_COL32(120, 200, 80, 80), 0.0f, 0, 1.0f);
  }

  hoveredUUID = 0;

  dl->PushClipRect(canvasTL, bgBR, true);
  paintObject(*scene, selection, scene->getRootObject(), dl, false, ImVec2{0,0});
  dl->PopClipRect();

  // ----- Input handling -----

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panActive = true;
  if (panActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
    auto delta = ImGui::GetIO().MouseDelta;
    origin.x += delta.x;
    origin.y += delta.y;
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) panActive = false;

  if (hovered && ImGui::GetIO().KeyCtrl) {
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
      ImVec2 mp = ImGui::GetMousePos();
      ImVec2 mpC = screenToCanvas(mp);
      zoom *= (wheel > 0.0f ? 1.1f : 1.0f / 1.1f);
      zoom = std::clamp(zoom, 0.25f, 16.0f);
      origin.x = mp.x - mpC.x * zoom;
      origin.y = mp.y - mpC.y * zoom;
    }
  }

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !panActive) {
    if (hoveredUUID) {
      selection.set(hoveredUUID);
    } else if (!ImGui::GetIO().KeyCtrl) {
      selection.clear();
    }
  }

  if (hovered
      && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)
      && !panActive
      && !dragging
      && hoveredUUID
      && selection.isSelected(hoveredUUID))
  {
    dragging = true;
    dragObjectUUID = hoveredUUID;
    dragMouseStart = ImGui::GetMousePos();

    Project::Object *o = scene->getObjectByUUID(hoveredUUID).get();
    if (o) {
      auto p = o->pos.resolve(o->propOverrides);
      dragObjStartPos = ImVec2(p.x, p.y);
    }
  }
  if (dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    Project::Object *o = scene->getObjectByUUID(dragObjectUUID).get();
    if (o) {
      ImVec2 mp = ImGui::GetMousePos();
      float dx = (mp.x - dragMouseStart.x) / zoom;
      float dy = (mp.y - dragMouseStart.y) / zoom;
      auto p = o->pos.resolve(o->propOverrides);
      // Snap to the configured grid step. step=1 is the original pixel
      // snap (N64 framebuffer has no subpixel); larger steps line up
      // with Grid2D tile sizes for board / tilemap authoring.
      int step = snapStep > 0 ? snapStep : 1;
      float tx = dragObjStartPos.x + dx;
      float ty = dragObjStartPos.y + dy;
      p.x = (float)((int)std::round(tx / (float)step) * step);
      p.y = (float)((int)std::round(ty / (float)step) * step);
      o->pos.value = p;
    }
  }
  if (dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    Editor::UndoRedo::getHistory().markChanged("Move 2D Object");
    dragging = false;
    dragObjectUUID = 0;
  }
}
