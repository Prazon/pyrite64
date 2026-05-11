/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "commentFrames.h"

#include "imgui.h"
#include "ImNodeFlow.h"

#include "../project/graph/graph.h"
#include "../project/graph/nodes/baseNode.h"
#include "../project/graph/nodes/nodeNote.h"

namespace Editor::CommentFrames
{
  namespace
  {
    // Every Note node persists its type as 12 (NODE_TABLE order in
    // graph.cpp). Cheaper than a dynamic_cast on every frame.
    constexpr uint32_t TYPE_NOTE = 12;

    // Convert a grid-space rect to screen-space using ImNodeFlow's
    // current scroll + scale. canvasMin is the parent window's cursor
    // position right before graph.graph.update(): the same anchor the
    // existing focus / bad-node-outline code already uses.
    inline void gridRectToScreen(ImVec2 canvasMin, ImVec2 scroll, float scale,
                                 ImVec2 gridPos, ImVec2 gridSize,
                                 ImVec2 &mn, ImVec2 &mx)
    {
      mn.x = canvasMin.x + (scroll.x + gridPos.x) * scale;
      mn.y = canvasMin.y + (scroll.y + gridPos.y) * scale;
      mx.x = mn.x + gridSize.x * scale;
      mx.y = mn.y + gridSize.y * scale;
    }
  }

  void State::preUpdate(::Project::Graph::Graph &g, ImVec2 canvasMin)
  {
    auto &flow = g.graph;
    ImVec2 scroll = flow.getGrid().scroll();
    float  scale  = flow.getGrid().scale();
    auto  *dl     = ImGui::GetWindowDrawList();

    prevPos.clear();
    for (const auto &kv : flow.getNodes()) {
      auto *n = static_cast<::Project::Graph::Node::Base*>(kv.second.get());
      if (!n || n->type != TYPE_NOTE) continue;
      auto *note = static_cast<::Project::Graph::Node::Note*>(n);

      ImVec2 gridPos = note->getPos();
      prevPos[note->uuid] = gridPos;

      ImVec2 mn, mx;
      gridRectToScreen(canvasMin, scroll, scale,
                       gridPos, note->size, mn, mx);

      // Body fill. Slight border so an empty rect is still discoverable
      // on a darker canvas; alpha mirrors the user-picked colour.
      const uint32_t fill   = note->color;
      const uint32_t border = IM_COL32(
        ((fill >>  0) & 0xFF),
        ((fill >>  8) & 0xFF),
        ((fill >> 16) & 0xFF),
        0xC0);
      const float    radius = 6.0f * scale;
      dl->AddRectFilled(mn, mx, fill, radius);
      dl->AddRect(mn, mx, border, radius, 0, 1.5f * scale);
    }
  }

  void State::postUpdate(::Project::Graph::Graph &g)
  {
    auto &flow = g.graph;
    if (prevPos.empty()) return;

    // For each Note that moved this frame, walk every non-Note node
    // whose centre lay inside the Note's prior rect and apply the
    // same delta. Skip selected children so the user can drag a node
    // out of the frame without it snapping back. Skip dragged-itself
    // edge cases by zero-delta short-circuit.
    for (const auto &kv : flow.getNodes()) {
      auto *n = static_cast<::Project::Graph::Node::Base*>(kv.second.get());
      if (!n || n->type != TYPE_NOTE) continue;
      auto it = prevPos.find(n->uuid);
      if (it == prevPos.end()) continue;
      const ImVec2 prev = it->second;
      const ImVec2 cur  = n->getPos();
      const float  dx   = cur.x - prev.x;
      const float  dy   = cur.y - prev.y;
      if (dx == 0.0f && dy == 0.0f) continue;

      auto *note = static_cast<::Project::Graph::Node::Note*>(n);
      const float left   = prev.x;
      const float top    = prev.y;
      const float right  = prev.x + note->size.x;
      const float bottom = prev.y + note->size.y;

      for (const auto &kv2 : flow.getNodes()) {
        auto *child = static_cast<::Project::Graph::Node::Base*>(kv2.second.get());
        if (!child || child == n) continue;
        if (child->isSelected()) continue;
        const ImVec2 cp = child->getPos();
        const ImVec2 cs = child->getSize();
        const float  cx = cp.x + cs.x * 0.5f;
        const float  cy = cp.y + cs.y * 0.5f;
        if (cx < left || cx > right || cy < top || cy > bottom) continue;
        child->setPos(ImVec2{cp.x + dx, cp.y + dy});
      }
    }
  }
}
