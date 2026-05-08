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
#include "../../undoRedo.h"

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
  auto *scene = ctx.project ? ctx.project->getScenes().getLoadedScene() : nullptr;
  if (!scene) {
    ImGui::TextDisabled("(no scene loaded)");
    return;
  }
  fbW = scene->conf.fbWidth;
  fbH = scene->conf.fbHeight;
  if (fbW <= 0) fbW = 320;
  if (fbH <= 0) fbH = 240;

  auto &selection = ctx.mainSelection;

  if (ImGui::SmallButton("Reset View")) {
    originInit = false;
    zoom = 2.0f;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("zoom %.2fx  |  hover: %s",
    zoom, hoveredUUID ? "yes" : "no");

  ImVec2 canvasTL = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  if (canvasSize.x < 10.0f || canvasSize.y < 10.0f) return;

  if (!originInit) {
    origin.x = canvasTL.x + (canvasSize.x - fbW * zoom) * 0.5f;
    origin.y = canvasTL.y + (canvasSize.y - fbH * zoom) * 0.5f;
    originInit = true;
  }

  ImGui::InvisibleButton("##v2dCanvas", canvasSize,
    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
  bool hovered = ImGui::IsItemHovered();
  ImDrawList *dl = ImGui::GetWindowDrawList();

  ImVec2 bgTL = canvasTL;
  ImVec2 bgBR{canvasTL.x + canvasSize.x, canvasTL.y + canvasSize.y};
  dl->AddRectFilled(bgTL, bgBR, IM_COL32(20, 20, 22, 255));

  ImVec2 fbTL = canvasToScreen({0.0f, 0.0f});
  ImVec2 fbBR = canvasToScreen({(float)fbW, (float)fbH});
  dl->AddRectFilled(fbTL, fbBR, IM_COL32(40, 40, 48, 255));
  dl->AddRect(fbTL, fbBR, IM_COL32(120, 120, 130, 200), 0.0f, 0, 1.0f);

  // 10% safe-area inset, NTSC convention.
  float insetX = fbW * 0.1f;
  float insetY = fbH * 0.1f;
  ImVec2 saTL = canvasToScreen({insetX, insetY});
  ImVec2 saBR = canvasToScreen({fbW - insetX, fbH - insetY});
  dl->AddRect(saTL, saBR, IM_COL32(120, 200, 80, 80), 0.0f, 0, 1.0f);

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
      // Snap to integer pixels — N64 framebuffer has no subpixel.
      auto p = o->pos.resolve(o->propOverrides);
      p.x = std::round(dragObjStartPos.x + dx);
      p.y = std::round(dragObjStartPos.y + dy);
      o->pos.value = p;
    }
  }
  if (dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    Editor::UndoRedo::getHistory().markChanged("Move 2D Object");
    dragging = false;
    dragObjectUUID = 0;
  }
}
