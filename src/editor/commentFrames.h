/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <unordered_map>
#include <cstdint>

#include "imgui.h"

namespace Project::Graph { class Graph; }

// Comment-frame rendering + drag containment for script-graph editors.
// Note nodes carry a user-set width/height/colour but ImNodeFlow has
// no native concept of a "frame" that draws behind other nodes. This
// module fills the gap:
//
//   preUpdate(graph, canvasMin)
//     Paints each Note's coloured rect on the parent window's draw
//     list (which renders before ImNodeFlow's nodes copy in, so the
//     rect sits behind them) and records the previous-frame position
//     of each Note for the post-update delta calculation.
//
//   postUpdate(graph)
//     For every Note that moved this frame, propagates the same delta
//     to non-selected child nodes whose centre lay inside the Note's
//     prior rect. Selected children are left alone so they can be
//     dragged out of the comment.
//
// Both calls are no-ops in graphs with no Note nodes, so the cost is
// proportional to comments + contained nodes only.
namespace Editor::CommentFrames
{
  class State
  {
    public:
      void preUpdate(::Project::Graph::Graph &g, ImVec2 canvasMin);
      void postUpdate(::Project::Graph::Graph &g);

    private:
      // Note uuid -> grid-space top-left at start of this frame.
      std::unordered_map<uint64_t, ImVec2> prevPos;
  };
}
