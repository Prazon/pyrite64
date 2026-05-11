/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

namespace Project::Graph { class Graph; }

// Insert a Reroute knot when the user double-clicks on an existing
// exec wire. v1: only logic-style (exec) links are reroutable; the
// in-graph node Reroute carries TypeLogic pins so wiring up other
// types would require either a generic-typed node or a per-type
// variant. Both are out of scope for now.
namespace Editor::RerouteHandler
{
  // Call once per frame after graph.graph.update(). Walks the link
  // list, checks for hovered + double-click, and on hit splits the
  // link with a fresh Reroute at the mouse-grid position.
  void handle(::Project::Graph::Graph &g);
}
