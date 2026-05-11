/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "rerouteHandler.h"

#include <memory>

#include "imgui.h"
#include "ImNodeFlow.h"

#include "../project/graph/graph.h"
#include "../project/graph/nodes/baseNode.h"
#include "../project/graph/nodeStyles.h"

namespace Editor::RerouteHandler
{
  // Index in NODE_TABLE for the Reroute node (graph.cpp). Trips the
  // static_assert on getPaletteEntries() if someone reorders entries
  // without updating both sides.
  constexpr uint32_t TYPE_REROUTE = 16;

  void handle(::Project::Graph::Graph &g)
  {
    if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) return;

    // Find the first hovered link with an exec-style source. We bail
    // out of the loop before mutating anything so the iteration over
    // ImNodeFlow's link list never sees an invalidated entry.
    std::shared_ptr<ImFlow::Link> target;
    ImFlow::Pin* src = nullptr;
    ImFlow::Pin* dst = nullptr;
    auto execStyle = ::Project::Graph::pinStyle(
      ::Project::Graph::PinDataType::Exec).get();

    for (const auto &weakLink : g.graph.getLinks()) {
      auto link = weakLink.lock();
      if (!link || !link->isHovered()) continue;
      auto *l = link->left();
      auto *r = link->right();
      if (!l || !r) continue;
      if (l->getStyle().get() != execStyle) continue;
      target = link;
      src = l;
      dst = r;
      break;
    }
    if (!target) return;

    // Spawn the knot at the click point so the wire visibly bends
    // through it without further user effort.
    ImVec2 gp = g.graph.screen2grid(ImGui::GetMousePos());
    auto reroute = g.addNode(TYPE_REROUTE, gp);
    if (!reroute) return;
    reroute->setPos(gp);

    // Break the original link. Drop our shared_ptr first so the InPin
    // erase_if takes the refcount to zero, which triggers ~Link()'s
    // m_left->deleteLink and cleans the OutPin side too.
    ImFlow::Link* raw = target.get();
    target.reset();
    dst->deleteLink(raw);

    // Re-wire src -> reroute_in, reroute_out -> dst.
    auto &rIns  = reroute->getIns();
    auto &rOuts = reroute->getOuts();
    if (!rIns.empty() && !rOuts.empty()) {
      rIns[0]->createLink(src);
      rOuts[0]->createLink(dst);
    }
  }
}
