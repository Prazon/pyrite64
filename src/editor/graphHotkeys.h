/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "ImNodeFlow.h"
#include "json.hpp"

// Graph-editor hotkeys shared between the script-graph and material-
// graph editor windows. Templated on GraphT (the wrapping Graph class
// holding an ImNodeFlow) and NodeBaseT (the per-kind Node base type
// that owns serialize / deserialize / type / uuid). Both kinds satisfy
// the shape: graph.graph (ImNodeFlow), graph.addNode(typeIdx, pos),
// node->isSelected(), node->getPos(), node->setPos(), node->getSize(),
// node->destroy(), node->serialize(json), node->deserialize(json),
// node->type, node->uuid.
//
// Returns true if any state-changing action fired this frame so the
// caller can flip its dirty flag.
namespace Editor::GraphHotkeys
{
  template<typename NodeBaseT>
  inline NodeBaseT* asNode(ImFlow::BaseNode* n) { return static_cast<NodeBaseT*>(n); }

  template<typename GraphT, typename NodeBaseT>
  inline bool apply(GraphT &g,
                    const ImVec2 &canvasSize,
                    std::string *clipboard)
  {
    bool changed = false;
    auto &flow  = g.graph;
    auto &nodes = flow.getNodes();
    ImGuiIO &io = ImGui::GetIO();

    // Suppress every key when text input or a popup-internal item has
    // focus, so editing a Func node's name (or typing into the palette
    // search) doesn't double up as a graph hotkey. Ctrl is exempt for
    // the Ctrl+S save handler the caller still owns.
    const bool textActive = ImGui::IsAnyItemActive();
    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;
    const bool alt = io.KeyAlt;

    auto bbox = [&](bool selectedOnly) -> std::pair<ImVec2, ImVec2> {
      ImVec2 mn{+FLT_MAX, +FLT_MAX};
      ImVec2 mx{-FLT_MAX, -FLT_MAX};
      bool any = false;
      for (auto &kv : nodes) {
        auto *n = asNode<NodeBaseT>(kv.second.get());
        if (!n) continue;
        if (selectedOnly && !n->isSelected()) continue;
        ImVec2 p = n->getPos();
        ImVec2 s = n->getSize();
        if (s.x <= 0.0f) s.x = 120.0f;
        if (s.y <= 0.0f) s.y = 60.0f;
        mn.x = std::min(mn.x, p.x);     mn.y = std::min(mn.y, p.y);
        mx.x = std::max(mx.x, p.x+s.x); mx.y = std::max(mx.y, p.y+s.y);
        any = true;
      }
      if (!any) return {{0,0},{0,0}};
      return {mn, mx};
    };

    auto frame = [&](bool selectedOnly) {
      auto [mn, mx] = bbox(selectedOnly);
      if (mn.x == 0 && mx.x == 0 && mn.y == 0 && mx.y == 0) {
        // No selection: fall back to all.
        if (selectedOnly) { auto p = bbox(false); mn = p.first; mx = p.second; }
        if (mn.x == 0 && mx.x == 0 && mn.y == 0 && mx.y == 0) return;
      }
      ImVec2 c{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
      ImVec2 target{canvasSize.x * 0.5f - c.x, canvasSize.y * 0.5f - c.y};
      // Same const_cast trick the existing focus-from-Compile-Errors
      // code uses (see nodeEditor.cpp). ImNodeFlow exposes scroll() as
      // const-ref only.
      const_cast<ImVec2&>(flow.getGrid().scroll()) = target;
    };

    auto serializeSelected = [&]() -> nlohmann::json {
      // Same shape as Graph::serialize so paste can reuse the existing
      // deserialize path. Keep node uuids in the clipboard so links
      // between selected nodes survive the round-trip; paste rewrites
      // them to fresh uuids before insert.
      nlohmann::json data;
      data["nodes"] = nlohmann::json::array();
      data["links"] = nlohmann::json::array();
      std::unordered_map<uint64_t, bool> selectedUUIDs;
      for (auto &kv : nodes) {
        auto *n = asNode<NodeBaseT>(kv.second.get());
        if (!n || !n->isSelected()) continue;
        nlohmann::json jn;
        jn["uuid"] = n->uuid;
        jn["type"] = n->type;
        jn["pos"]  = {n->getPos().x, n->getPos().y};
        n->serialize(jn);
        data["nodes"].push_back(std::move(jn));
        selectedUUIDs[n->uuid] = true;
      }
      // Preserve only links whose endpoints are both in the selection.
      for (const auto &weakLink : flow.getLinks()) {
        if (auto link = weakLink.lock()) {
          auto *l = link->left();
          auto *r = link->right();
          if (!l || !r) continue;
          auto *ln = asNode<NodeBaseT>(l->getParent());
          auto *rn = asNode<NodeBaseT>(r->getParent());
          if (!ln || !rn) continue;
          if (!selectedUUIDs.count(ln->uuid) || !selectedUUIDs.count(rn->uuid)) continue;
          // Look up port indices. Walk the parent's pin list to find
          // which slot the pin sits in. O(n*m) but n,m are tiny.
          auto findIdx = [&](auto &pins, ImFlow::Pin* p) -> uint32_t {
            for (size_t i = 0; i < pins.size(); ++i) {
              if (pins[i].get() == p) return static_cast<uint32_t>(i);
            }
            return 0;
          };
          nlohmann::json jl;
          jl["src"]     = ln->uuid;
          jl["srcPort"] = findIdx(ln->getOuts(), l);
          jl["dst"]     = rn->uuid;
          jl["dstPort"] = findIdx(rn->getIns(),  r);
          data["links"].push_back(std::move(jl));
        }
      }
      return data;
    };

    auto deleteSelected = [&]() -> bool {
      std::vector<ImFlow::BaseNode*> kill;
      for (auto &kv : nodes) {
        auto *n = asNode<NodeBaseT>(kv.second.get());
        if (n && n->isSelected()) kill.push_back(kv.second.get());
      }
      for (auto *n : kill) n->destroy();
      return !kill.empty();
    };

    auto pasteJson = [&](const nlohmann::json &data, ImVec2 mouseGrid) {
      if (!data.contains("nodes") || !data["nodes"].is_array()) return;
      // Compute bbox of pasted nodes so we can centre on the mouse.
      ImVec2 mn{+FLT_MAX, +FLT_MAX}, mx{-FLT_MAX, -FLT_MAX};
      bool any = false;
      for (const auto &jn : data["nodes"]) {
        if (!jn.contains("pos") || !jn["pos"].is_array() || jn["pos"].size() != 2) continue;
        float x = jn["pos"][0], y = jn["pos"][1];
        mn.x = std::min(mn.x, x); mn.y = std::min(mn.y, y);
        mx.x = std::max(mx.x, x); mx.y = std::max(mx.y, y);
        any = true;
      }
      ImVec2 srcCentre = any
        ? ImVec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f}
        : ImVec2{0,0};
      ImVec2 offset{mouseGrid.x - srcCentre.x, mouseGrid.y - srcCentre.y};

      // Map old uuid -> new node so links can be re-pointed and the
      // freshly-inserted nodes can be left selected for chained ops.
      std::unordered_map<uint64_t, std::shared_ptr<NodeBaseT>> created;
      // Clear existing selection so the paste lands as a fresh group.
      for (auto &kv : nodes) {
        auto *n = asNode<NodeBaseT>(kv.second.get());
        if (n) n->selected(false);
      }
      for (const auto &jn : data["nodes"]) {
        if (!jn.contains("type") && !jn.contains("typeId")) continue;
        ImVec2 pos{0,0};
        if (jn.contains("pos") && jn["pos"].is_array() && jn["pos"].size() == 2) {
          pos = {jn["pos"][0].get<float>() + offset.x,
                 jn["pos"][1].get<float>() + offset.y};
        }
        // Spec-driven nodes carry only the string id; graphs whose type
        // supports the string overload (script graphs) spawn through it.
        decltype(g.addNode(0u, pos)) newNode{};
        if (jn.contains("type")) {
          newNode = g.addNode(jn["type"].get<uint32_t>(), pos);
        } else if constexpr (requires { g.addNode(std::string{}, pos); }) {
          newNode = g.addNode(jn["typeId"].get<std::string>(), pos);
        }
        if (!newNode) continue;
        newNode->setPos(pos);
        // Apply per-node fields. Skip uuid in the json so the random
        // uuid assigned by addNode is preserved (ensures uniqueness).
        nlohmann::json jnCopy = jn;
        jnCopy.erase("uuid");
        newNode->deserialize(jnCopy);
        newNode->selected(true);
        uint64_t oldUUID = jn.value("uuid", uint64_t{0});
        if (oldUUID) created[oldUUID] = newNode;
      }
      // Recreate links between pasted nodes only.
      if (data.contains("links") && data["links"].is_array()) {
        for (const auto &jl : data["links"]) {
          uint64_t srcOld = jl.value("src", uint64_t{0});
          uint64_t dstOld = jl.value("dst", uint64_t{0});
          uint32_t srcPort = jl.value("srcPort", 0u);
          uint32_t dstPort = jl.value("dstPort", 0u);
          auto sIt = created.find(srcOld);
          auto dIt = created.find(dstOld);
          if (sIt == created.end() || dIt == created.end()) continue;
          auto &outs = sIt->second->getOuts();
          auto &ins  = dIt->second->getIns();
          if (srcPort >= outs.size() || dstPort >= ins.size()) continue;
          if (outs[srcPort] && ins[dstPort]) {
            outs[srcPort]->createLink(ins[dstPort].get());
          }
        }
      }
    };

    // --- Frame ---
    if (!textActive && ImGui::IsKeyPressed(ImGuiKey_F, false)) frame(true);
    if (!textActive && ImGui::IsKeyPressed(ImGuiKey_Home, false)) frame(false);
    // Avoid clashing with Ctrl+A select-all (not implemented but reserved).
    if (!textActive && !ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) frame(false);

    // --- Delete ---
    if (!textActive && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
      if (deleteSelected()) changed = true;
    }

    // --- Clipboard ops ---
    if (clipboard) {
      if (!textActive && ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        auto data = serializeSelected();
        if (!data["nodes"].empty()) *clipboard = data.dump();
      }
      if (!textActive && ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
        auto data = serializeSelected();
        if (!data["nodes"].empty()) {
          *clipboard = data.dump();
          if (deleteSelected()) changed = true;
        }
      }
      if (!textActive && ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !clipboard->empty()) {
        ImVec2 mp = ImGui::GetMousePos();
        ImVec2 mg = flow.screen2grid(mp);
        try {
          auto data = nlohmann::json::parse(*clipboard);
          pasteJson(data, mg);
          changed = true;
        } catch (...) { /* malformed clipboard: drop */ }
      }
      if (!textActive && ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        // Duplicate in place: serialize selection, paste with a small
        // visible offset so the clones don't overlap the originals.
        auto data = serializeSelected();
        if (!data["nodes"].empty()) {
          // Offset target: bbox centre + (24, 24) on the grid so the
          // duplicates land just down/right of the source.
          ImVec2 mn{+FLT_MAX, +FLT_MAX}, mx{-FLT_MAX, -FLT_MAX};
          for (const auto &jn : data["nodes"]) {
            float x = jn["pos"][0], y = jn["pos"][1];
            mn.x = std::min(mn.x, x); mn.y = std::min(mn.y, y);
            mx.x = std::max(mx.x, x); mx.y = std::max(mx.y, y);
          }
          ImVec2 c{(mn.x + mx.x) * 0.5f + 24.0f,
                   (mn.y + mx.y) * 0.5f + 24.0f};
          pasteJson(data, c);
          changed = true;
        }
      }
    }

    // --- Nudge ---
    {
      const float step = shift ? 25.0f : 5.0f;
      ImVec2 nudge{0,0};
      if (!textActive && ImGui::IsKeyPressed(ImGuiKey_LeftArrow,  true))  nudge.x -= step;
      if (!textActive && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))  nudge.x += step;
      if (!textActive && ImGui::IsKeyPressed(ImGuiKey_UpArrow,    true))  nudge.y -= step;
      if (!textActive && ImGui::IsKeyPressed(ImGuiKey_DownArrow,  true))  nudge.y += step;
      if (nudge.x != 0.0f || nudge.y != 0.0f) {
        for (auto &kv : nodes) {
          auto *n = asNode<NodeBaseT>(kv.second.get());
          if (n && n->isSelected()) {
            n->setPos(ImVec2{n->getPos().x + nudge.x, n->getPos().y + nudge.y});
            changed = true;
          }
        }
      }
    }

    // --- Alt+click hovered pin: break all its links ---
    // ImNodeFlow exposes the hovered pin as a public m_hovering member
    // updated each frame. When alt is held and the user left-clicks,
    // wipe its link list. Pin::deleteLinks() is virtual on InPin/OutPin.
    if (alt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (auto *p = flow.m_hovering) {
        p->deleteLinks();
        changed = true;
      }
    }

    return changed;
  }
}
