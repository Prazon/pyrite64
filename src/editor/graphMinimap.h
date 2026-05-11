/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <algorithm>

#include "imgui.h"
#include "ImNodeFlow.h"

// Bottom-right minimap overlay. Templated on the wrapping Graph type
// so the same code paints script-graph and material-graph editors;
// per-frame state (a click-drag flag) lives in a small struct the
// caller owns. Drawn on the foreground draw list so it sits above
// the node bodies regardless of when the editor lays out the rest of
// its UI.
namespace Editor::GraphMinimap
{
  struct State
  {
    bool dragging = false;
  };

  // canvasMin / canvasSize: the screen-space rect ImNodeFlow renders
  // into for this graph window (the same ones the existing focus and
  // bad-node-outline blocks already capture).
  template<typename GraphT, typename NodeBaseT>
  inline void draw(GraphT &g, ImVec2 canvasMin, ImVec2 canvasSize, State &state)
  {
    auto &flow  = g.graph;
    auto &nodes = flow.getNodes();

    constexpr float MAP_W = 200.0f;
    constexpr float MAP_H = 140.0f;
    constexpr float PAD   = 8.0f;

    // Anchor bottom-right within the canvas rect.
    ImVec2 mapMx{canvasMin.x + canvasSize.x - PAD,
                 canvasMin.y + canvasSize.y - PAD};
    ImVec2 mapMn{mapMx.x - MAP_W, mapMx.y - MAP_H};

    auto *dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(mapMn, mapMx, IM_COL32(0x10, 0x12, 0x10, 0xCC), 4.0f);
    dl->AddRect(mapMn, mapMx, IM_COL32(0x80, 0x80, 0x80, 0x80), 4.0f, 0, 1.0f);

    if (nodes.empty()) return;

    // Compute graph-space bbox over all nodes. Add a margin so the
    // viewport rect overlay can never collapse to a single point even
    // when every node sits at the origin.
    ImVec2 mn{+FLT_MAX, +FLT_MAX};
    ImVec2 mx{-FLT_MAX, -FLT_MAX};
    for (auto &kv : nodes) {
      auto *n = static_cast<NodeBaseT*>(kv.second.get());
      if (!n) continue;
      ImVec2 p = n->getPos();
      ImVec2 s = n->getSize();
      if (s.x <= 0.0f) s.x = 80.0f;
      if (s.y <= 0.0f) s.y = 40.0f;
      mn.x = std::min(mn.x, p.x);     mn.y = std::min(mn.y, p.y);
      mx.x = std::max(mx.x, p.x+s.x); mx.y = std::max(mx.y, p.y+s.y);
    }
    // Pad so contents don't touch the edge.
    {
      const float marg = 80.0f;
      mn.x -= marg; mn.y -= marg;
      mx.x += marg; mx.y += marg;
    }

    const float gw = std::max(1.0f, mx.x - mn.x);
    const float gh = std::max(1.0f, mx.y - mn.y);
    // Uniform scale to fit; centre the smaller axis inside the map.
    const float sx = (MAP_W - 8.0f) / gw;
    const float sy = (MAP_H - 8.0f) / gh;
    const float s  = std::min(sx, sy);
    const float drawnW = gw * s;
    const float drawnH = gh * s;
    const ImVec2 mapOrigin{
      mapMn.x + 4.0f + (MAP_W - 8.0f - drawnW) * 0.5f,
      mapMn.y + 4.0f + (MAP_H - 8.0f - drawnH) * 0.5f
    };

    auto gridToMap = [&](ImVec2 p) {
      return ImVec2{mapOrigin.x + (p.x - mn.x) * s,
                    mapOrigin.y + (p.y - mn.y) * s};
    };

    // Node dots. Style colour pulled from the node's NodeStyle so the
    // category palette established in Phase 4 carries into the map.
    for (auto &kv : nodes) {
      auto *n = static_cast<NodeBaseT*>(kv.second.get());
      if (!n) continue;
      ImVec2 p = n->getPos();
      ImVec2 sz = n->getSize();
      if (sz.x <= 0.0f) sz.x = 80.0f;
      if (sz.y <= 0.0f) sz.y = 40.0f;
      ImVec2 a = gridToMap(p);
      ImVec2 b = gridToMap({p.x + sz.x, p.y + sz.y});
      // Style is owned by the Pin/Node class; getStyle exists on
      // ImFlow::BaseNode and returns a shared_ptr<NodeStyle>.
      ImU32 col = IM_COL32(0xCC, 0xCC, 0xCC, 0xFF);
      if (auto st = n->getStyle()) col = st->header_bg;
      dl->AddRectFilled(a, b, col, 1.5f);
      if (n->isSelected()) {
        dl->AddRect(a, b, IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), 1.5f, 0, 1.0f);
      }
    }

    // Visible-region overlay: project the canvas rect into grid space
    // (canvas top-left = -scroll in grid coords) and draw it on the map.
    const ImVec2 scroll = flow.getGrid().scroll();
    const float  scale  = flow.getGrid().scale();
    ImVec2 viewGridMn{ -scroll.x / scale, -scroll.y / scale };
    ImVec2 viewGridMx{ viewGridMn.x + canvasSize.x / scale,
                       viewGridMn.y + canvasSize.y / scale };
    ImVec2 va = gridToMap(viewGridMn);
    ImVec2 vb = gridToMap(viewGridMx);
    // Clamp to the map rect so a far-zoomed-out view doesn't paint
    // outside the chrome.
    va.x = std::clamp(va.x, mapMn.x + 1, mapMx.x - 1);
    va.y = std::clamp(va.y, mapMn.y + 1, mapMx.y - 1);
    vb.x = std::clamp(vb.x, mapMn.x + 1, mapMx.x - 1);
    vb.y = std::clamp(vb.y, mapMn.y + 1, mapMx.y - 1);
    dl->AddRect(va, vb, IM_COL32(0xFF, 0xFF, 0xFF, 0xC0), 2.0f, 0, 1.5f);

    // Click / drag inside the map pans the canvas so the click point
    // becomes the centre of the visible region. Tiny hit-test using
    // mouse pos against the map rect; ImGui's IsMouseHoveringRect on
    // the foreground would also work but a manual check keeps this
    // header free of state dependencies.
    ImVec2 mp = ImGui::GetMousePos();
    bool hovered = (mp.x >= mapMn.x && mp.x <= mapMx.x
                 && mp.y >= mapMn.y && mp.y <= mapMx.y);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      state.dragging = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      state.dragging = false;
    }
    if (state.dragging) {
      // Map the mouse position back to grid space, then choose scroll
      // so the canvas centre lands there.
      float u = (mp.x - mapOrigin.x) / std::max(1.0f, drawnW);
      float v = (mp.y - mapOrigin.y) / std::max(1.0f, drawnH);
      u = std::clamp(u, 0.0f, 1.0f);
      v = std::clamp(v, 0.0f, 1.0f);
      ImVec2 targetGrid{mn.x + u * gw, mn.y + v * gh};
      ImVec2 targetScroll{
        canvasSize.x * 0.5f / scale - targetGrid.x,
        canvasSize.y * 0.5f / scale - targetGrid.y
      };
      const_cast<ImVec2&>(flow.getGrid().scroll()) =
        ImVec2{targetScroll.x * scale, targetScroll.y * scale};
    }
  }

}
