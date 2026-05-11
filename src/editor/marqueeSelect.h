/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <algorithm>

#include "imgui.h"
#include "ImNodeFlow.h"

// UE-Blueprint-style box (marquee) selection. Click-and-drag on empty
// grid space paints a translucent selection rectangle; on mouse
// release every node whose bounding box overlaps the rectangle is
// selected. Shift or Ctrl during the drag makes the selection
// additive instead of replace-mode.
//
// Replace-mode works without any explicit clear because ImNodeFlow's
// per-node update at ImNodeFlow.cpp:163-165 already deselects every
// non-clicked node on a free-space LMB click when Ctrl is not held;
// our marquee simply re-selects whatever it overlaps. Holding Ctrl
// suppresses ImNodeFlow's clear AND tells us to keep the prior
// selection, giving the additive behavior for free.
namespace Editor::MarqueeSelect
{
  struct State
  {
    bool   active = false;
    ImVec2 startScreen{0, 0};
  };

  template<typename GraphT, typename NodeBaseT>
  inline void apply(GraphT &g, State &state)
  {
    auto &flow  = g.graph;
    ImGuiIO &io = ImGui::GetIO();

    // --- Begin? ---
    // Conditions: not already in a marquee, LMB just pressed this
    // frame, on empty grid (not over a pin / link / node), no node
    // is being dragged. Alt is reserved for the break-link hotkey
    // and shouldn't kick off a marquee.
    if (!state.active
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !io.KeyAlt
        && flow.on_free_space()
        && !flow.isNodeDragged()) {
      state.active = true;
      state.startScreen = ImGui::GetMousePos();
    }

    if (!state.active) return;

    // --- Live: paint the rect on the foreground draw list so it
    // sits over the canvas chrome regardless of when ImNodeFlow
    // copies its draw data into the parent window.
    ImVec2 a = state.startScreen;
    ImVec2 b = ImGui::GetMousePos();
    ImVec2 mn{std::min(a.x, b.x), std::min(a.y, b.y)};
    ImVec2 mx{std::max(a.x, b.x), std::max(a.y, b.y)};
    auto *dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(mn, mx, IM_COL32(0xFF, 0xFF, 0xFF, 0x18));
    dl->AddRect      (mn, mx, IM_COL32(0xFF, 0xFF, 0xFF, 0xC0),
                      0.0f, 0, 1.0f);

    // --- End on release ---
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      state.active = false;

      // A near-zero drag is just a click; ignore it so the user
      // doesn't have to be careful with empty-canvas clicks.
      if ((mx.x - mn.x) < 4.0f && (mx.y - mn.y) < 4.0f) return;

      const ImVec2 mnGrid = flow.screen2grid(mn);
      const ImVec2 mxGrid = flow.screen2grid(mx);

      for (auto &kv : flow.getNodes()) {
        auto *n = static_cast<NodeBaseT*>(kv.second.get());
        if (!n) continue;
        const ImVec2 p = n->getPos();
        const ImVec2 s = n->getSize();
        // Standard AABB overlap (not full containment) so partially
        // covered nodes are still picked up, matching UE's behavior.
        const bool overlap = !(p.x + s.x < mnGrid.x
                            || p.x        > mxGrid.x
                            || p.y + s.y < mnGrid.y
                            || p.y        > mxGrid.y);
        if (overlap) n->selected(true);
        // Non-overlapping nodes: leave alone. ImNodeFlow already
        // cleared everything else on the initial click (when Ctrl
        // wasn't held); when Ctrl IS held the prior selection sticks
        // and the marquee just adds to it.
      }
    }
  }
}
