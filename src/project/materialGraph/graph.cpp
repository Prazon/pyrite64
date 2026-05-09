/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "graph.h"

#include <unordered_set>

#include "json.hpp"
#include "../../utils/string.h"
#include "../../utils/logger.h"

#include "nodes/nodeOutput.h"
#include "nodes/nodeColorCombiner.h"
#include "nodes/nodeColors.h"
#include "nodes/nodeRenderMode.h"
#include "nodes/nodeTexture.h"
#include "nodes/nodeGeometry.h"

namespace Project::MaterialGraph::Node
{
  std::shared_ptr<ImFlow::PinStyle> PIN_STYLE_MATPROP = ImFlow::PinStyle::brown();
}

namespace
{
  using NodeCreateFunc =
    std::function<std::shared_ptr<Project::MaterialGraph::Node::Base>(
      ImFlow::ImNodeFlow &m, const ImVec2&)>;

  struct TableEntry
  {
    NodeCreateFunc create;
    const char* name;
  };

  uint32_t getIndexLeft(ImFlow::Pin* pin)
  {
    auto leftNode = (Project::MaterialGraph::Node::Base*)pin->getParent();
    auto &outs = leftNode->getOuts();
    for (size_t i = 0; i < outs.size(); ++i) {
      if (outs[i].get() == pin) return static_cast<uint32_t>(i);
    }
    return 0;
  }

  uint32_t getIndexRight(ImFlow::Pin* pin)
  {
    auto rightNode = (Project::MaterialGraph::Node::Base*)pin->getParent();
    auto &ins = rightNode->getIns();
    for (size_t i = 0; i < ins.size(); ++i) {
      if (ins[i].get() == pin) return static_cast<uint32_t>(i);
    }
    return 0;
  }
}

#define TABLE_ENTRY(name) TableEntry{ \
    [](ImFlow::ImNodeFlow &m, const ImVec2& pos) { return m.addNode<Node::name>(pos); }, \
    Node::name::NAME \
  }

namespace Project::MaterialGraph
{
  // Stable indices — persisted in saved graphs. Append only; never reorder.
  constexpr uint32_t TYPE_OUTPUT          = 0;
  constexpr uint32_t TYPE_COLOR_COMBINER  = 1;
  constexpr uint32_t TYPE_COLORS          = 2;
  constexpr uint32_t TYPE_RENDER_MODE     = 3;
  constexpr uint32_t TYPE_TEXTURE         = 4;
  constexpr uint32_t TYPE_GEOMETRY        = 5;

  static auto NODE_TABLE = std::to_array<TableEntry>({
    TABLE_ENTRY(Output),
    TABLE_ENTRY(ColorCombiner),
    TABLE_ENTRY(Colors),
    TABLE_ENTRY(RenderMode),
    TABLE_ENTRY(Texture),
    TABLE_ENTRY(Geometry),
  });

  const std::vector<std::string>& Graph::getNodeNames()
  {
    static std::vector<std::string> names = {};
    if (names.empty()) {
      for (const auto &entry : NODE_TABLE) names.emplace_back(entry.name);
    }
    return names;
  }

  std::shared_ptr<Node::Base> Graph::addNode(uint32_t type, const ImVec2 &pos)
  {
    if (type >= NODE_TABLE.size()) return nullptr;
    auto newNode = NODE_TABLE[type].create(graph, pos);
    newNode->type = type;
    newNode->uuid = Utils::Hash::randomU64();
    return newNode;
  }

  void Graph::seedDefaults()
  {
    // The RDP default combiner samples a texture, so a Colors node alone
    // would produce no visible colour. Wire a PRIM-solid CC to the Output
    // too, so a fresh material previews as a flat colour the user can edit.
    auto outputNode = addNode(TYPE_OUTPUT,         ImVec2{460.0f, 60.0f});
    auto ccNode     = addNode(TYPE_COLOR_COMBINER, ImVec2{60.0f, 40.0f});
    auto colorsNode = addNode(TYPE_COLORS,         ImVec2{60.0f, 260.0f});

    // PRIM solid: D=3 picks Prim / Prim-alpha; A,B,C use the "0" slots
    // from N64::CC::NAMES_* in ccMapping.h.
    glm::ivec4 cc0Color{8, 8, 16, 3};
    glm::ivec4 cc0Alpha{7, 7,  7, 3};
    nlohmann::json ccJ;
    ccJ["cc"] = N64::CC::packCC(cc0Color, cc0Alpha, cc0Color, cc0Alpha);
    ccNode->deserialize(ccJ);

    nlohmann::json colJ;
    colJ["setPrim"] = true;
    colJ["prim"]    = {0.8f, 0.8f, 0.8f, 1.0f};
    colJ["setEnv"]  = false;
    colJ["env"]     = {0.5f, 0.5f, 0.5f, 1.0f};
    colorsNode->deserialize(colJ);

    // Output IN pins in declaration order: 0 Color Combiner, 3 Colors.
    ccNode->getOuts()[0]->createLink(outputNode->getIns()[0].get());
    colorsNode->getOuts()[0]->createLink(outputNode->getIns()[3].get());
  }

  bool Graph::deserialize(const std::string &jsonData)
  {
    if (jsonData.empty()) return true;

    auto nodeData = nlohmann::json::parse(jsonData, nullptr, false);
    if (!nodeData.is_object()) return false;

    std::unordered_map<uint64_t, std::shared_ptr<Node::Base>> newNodes{};
    if (nodeData.contains("nodes")) {
      for (auto &savedNode : nodeData["nodes"]) {
        uint32_t type = savedNode.value<uint32_t>("type", UINT32_MAX);
        if (type >= NODE_TABLE.size()) {
          Utils::Logger::log(
            "Material graph: unknown node type " + std::to_string(type) + ", skipping.",
            Utils::Logger::LEVEL_ERROR
          );
          continue;
        }
        auto newNode = NODE_TABLE[type].create(graph, {});
        newNode->deserialize(savedNode);
        if (savedNode.contains("pos")) {
          newNode->setPos({savedNode["pos"][0], savedNode["pos"][1]});
        }
        newNode->type = type;
        newNode->uuid = savedNode.value<uint64_t>("uuid", Utils::Hash::randomU64());
        newNodes[newNode->uuid] = newNode;
      }
    }

    if (nodeData.contains("links")) {
      for (auto &savedLink : nodeData["links"]) {
        auto srcIt = newNodes.find(savedLink.value<uint64_t>("src", 0));
        auto dstIt = newNodes.find(savedLink.value<uint64_t>("dst", 0));
        if (srcIt == newNodes.end() || dstIt == newNodes.end()) continue;
        auto &outs = srcIt->second->getOuts();
        auto &ins  = dstIt->second->getIns();
        uint32_t srcIdx = savedLink.value<uint32_t>("srcPort", 0);
        uint32_t dstIdx = savedLink.value<uint32_t>("dstPort", 0);
        auto pinA = srcIdx < outs.size() ? outs[srcIdx].get() : nullptr;
        auto pinB = dstIdx < ins.size()  ? ins[dstIdx].get()  : nullptr;
        if (pinA && pinB) pinA->createLink(pinB);
      }
    }
    return true;
  }

  std::string Graph::serialize()
  {
    nlohmann::json data{};
    data["nodes"] = nlohmann::json::array();
    for (const auto& [uid, node] : graph.getNodes()) {
      auto p64Node = (Node::Base*)node.get();
      nlohmann::json jNode{};
      jNode["uuid"] = p64Node->uuid;
      jNode["type"] = p64Node->type;
      jNode["pos"]  = {p64Node->getPos().x, p64Node->getPos().y};
      p64Node->serialize(jNode);
      data["nodes"].push_back(jNode);
    }

    data["links"] = nlohmann::json::array();
    for (const auto &weakLink : graph.getLinks()) {
      auto link = weakLink.lock();
      if (!link) continue;
      auto leftPin  = link->left();
      auto rightPin = link->right();
      if (!leftPin || !rightPin) continue;
      auto leftNode  = leftPin->getParent();
      auto rightNode = rightPin->getParent();
      if (!leftNode || !rightNode) continue;

      nlohmann::json jLink{};
      jLink["src"] = ((Node::Base*)leftNode)->uuid;
      jLink["srcPort"] = getIndexLeft(leftPin);
      jLink["dst"] = ((Node::Base*)rightNode)->uuid;
      jLink["dstPort"] = getIndexRight(rightPin);
      data["links"].push_back(jLink);
    }
    return data.dump(2);
  }

  void Graph::compile(::Project::Assets::Material &out)
  {
    // Reset to defaults — fromT3D() establishes the same defaults a
    // freshly-imported model gets, and the inline-override path treats
    // these values as the "do not set" baseline.
    out = {};
    out.persp.value     = true;
    out.zmode.value     = 0b11;
    out.dither.value    = 15;
    out.primColor.value = {0.0f, 0.0f, 0.0f, 1.0f};
    out.envColor.value  = {0.5f, 0.5f, 0.5f, 1.0f};
    out.isCustom.value  = true;  // material assets always behave as overrides

    // Find the (single) Output sink. Without one the graph contributes
    // nothing — return defaults.
    Node::Base* sink = nullptr;
    for (const auto &kv : graph.getNodes()) {
      auto *n = (Node::Base*)kv.second.get();
      if (n && n->type == TYPE_OUTPUT) { sink = n; break; }
    }
    if (!sink) return;

    // BFS upstream from the sink's IN pins. Each provider node we visit
    // contributes its fields exactly once.
    std::unordered_map<uint64_t, Node::Base*> nodeByUUID{};
    std::unordered_map<uint64_t, std::vector<uint64_t>> incomingByDst{};
    for (const auto &kv : graph.getNodes()) {
      auto *n = (Node::Base*)kv.second.get();
      if (n) nodeByUUID[n->uuid] = n;
    }
    for (const auto &weakLink : graph.getLinks()) {
      auto link = weakLink.lock();
      if (!link) continue;
      auto leftPin  = link->left();
      auto rightPin = link->right();
      if (!leftPin || !rightPin) continue;
      auto *L = (Node::Base*)leftPin->getParent();
      auto *R = (Node::Base*)rightPin->getParent();
      if (L && R) incomingByDst[R->uuid].push_back(L->uuid);
    }

    std::unordered_set<uint64_t> visited{};
    std::vector<uint64_t> frontier{sink->uuid};
    while (!frontier.empty()) {
      uint64_t cur = frontier.back();
      frontier.pop_back();
      auto it = incomingByDst.find(cur);
      if (it == incomingByDst.end()) continue;
      for (uint64_t up : it->second) {
        if (!visited.insert(up).second) continue;
        auto upIt = nodeByUUID.find(up);
        if (upIt == nodeByUUID.end()) continue;
        upIt->second->contribute(out);
        frontier.push_back(up);
      }
    }
  }
}
