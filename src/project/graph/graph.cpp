/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "graph.h"

#include <unordered_set>

#include "json.hpp"
#include "../../utils/string.h"
#include "../../utils/logger.h"
#include "../compile/compileErrors.h"

#include "nodes/nodeWait.h"
#include "nodes/nodeObjDel.h"
#include "nodes/nodeStart.h"
#include "nodes/nodeObjEvent.h"
#include "nodes/nodeCompare.h"
#include "nodes/nodeValue.h"
#include "nodes/nodeRepeat.h"
#include "nodes/nodeFunc.h"
#include "nodes/nodeCompBool.h"
#include "nodes/nodeSceneLoad.h"
#include "nodes/nodeArg.h"
#include "nodes/nodeSwitchCase.h"
#include "nodes/nodeNote.h"
#include "nodes/nodePrefabEvent.h"
#include "nodes/nodePrefabFunc.h"
#include "nodes/nodePrefabSuper.h"
#include "nodes/nodePrefabVarGet.h"
#include "nodes/nodeReroute.h"
#include "nodes/nodeDeltaTime.h"
#include "nodes/nodeMathAdd.h"
#include "nodes/nodeMathSub.h"
#include "nodes/nodeMathMul.h"
#include "nodes/nodeMathDiv.h"
#include "nodes/nodeMathMod.h"
#include "nodes/nodeMathMin.h"
#include "nodes/nodeMathMax.h"
#include "nodes/nodeMathClamp.h"
#include "nodes/nodeMathAbs.h"
#include "nodes/nodeMathFloor.h"
#include "nodes/nodeMathCeil.h"
#include "nodes/nodeMathRound.h"
#include "nodes/nodeMathSign.h"
#include "nodes/nodeMathSqrt.h"
#include "nodes/nodeMathPow.h"
#include "nodes/nodeBoolAnd.h"
#include "nodes/nodeBoolOr.h"
#include "nodes/nodeBoolNot.h"
#include "nodes/nodeBoolXor.h"
#include "nodes/nodeCmpEq.h"
#include "nodes/nodeCmpNe.h"
#include "nodes/nodeCmpLt.h"
#include "nodes/nodeCmpLe.h"
#include "nodes/nodeCmpGt.h"
#include "nodes/nodeCmpGe.h"
#include "nodes/nodeRandomFloat.h"
#include "nodes/nodeRandomInt.h"
#include "nodes/nodeStringConst.h"
#include "nodes/nodeStringConcat.h"
#include "nodes/nodeToString.h"
#include "nodes/nodeStringLength.h"
#include "nodes/nodeSubstring.h"
#include "nodes/nodeStringFormat.h"
#include "nodes/nodeArrayMake.h"
#include "nodes/nodeArrayLength.h"
#include "nodes/nodeArrayGet.h"
#include "nodes/nodeArraySet.h"
#include "nodes/nodeArrayPush.h"
#include "nodes/nodeArrayPop.h"
#include "nodes/nodeArrayInsert.h"
#include "nodes/nodeArrayRemoveAt.h"
#include "nodes/nodeArrayClear.h"
#include "nodes/nodeArrayFind.h"
#include "nodes/nodeArrayContains.h"
#include "nodes/nodeForRange.h"
#include "nodes/nodeWhile.h"
#include "nodes/nodeForEach.h"
#include "nodes/nodeBreak.h"
#include "nodes/nodeContinue.h"
#include "nodes/nodeSaveCommit.h"
#include "nodes/nodeSaveReload.h"
#include "nodes/nodeSaveClearAll.h"
#include "nodes/nodeSaveGetInt.h"
#include "nodes/nodeSaveGetFloat.h"
#include "nodes/nodeSaveGetBool.h"
#include "nodes/nodeSaveGetString.h"
#include "nodes/nodeSaveSetInt.h"
#include "nodes/nodeSaveSetFloat.h"
#include "nodes/nodeSaveSetBool.h"
#include "nodes/nodeSaveSetString.h"

namespace Project::Graph::Node
{
  PrefabCtx& activePrefabCtx()
  {
    // File-scope context — set by PrefabEventGraphEditor before each frame's
    // graph.update() and reset after. Single-thread ImGui means no reentry.
    static PrefabCtx ctx;
    return ctx;
  }
}

namespace
{
  typedef std::function<std::shared_ptr<Project::Graph::Node::Base>(ImFlow::ImNodeFlow &m, const ImVec2&)> NodeCreateFunc;

  struct TableEntry
  {
    NodeCreateFunc create;
    const char* name;
  };

  uint32_t getIndexLeft(ImFlow::Pin* pin)
  {
    auto leftNode = (Project::Graph::Node::Base*)pin->getParent();
    auto &leftOuts = leftNode->getOuts();
    for(size_t i = 0; i < leftOuts.size(); ++i) {
      if(leftOuts[i].get() == pin) {
        return static_cast<uint32_t>(i);
      }
    }
    return 0;
  }

  uint32_t getIndexRight(ImFlow::Pin* pin)
  {
    auto rightNode = (Project::Graph::Node::Base*)pin->getParent();
    auto &rightIns = rightNode->getIns();
    for(size_t i = 0; i < rightIns.size(); ++i) {
      if(rightIns[i].get() == pin) {
        return static_cast<uint32_t>(i);
      }
    }
    return 0;
  }
}

#define TABLE_ENTRY(name) TableEntry{ \
    [](ImFlow::ImNodeFlow &m, const ImVec2& pos) { return m.addNode<Node::name>(pos); }, \
    Node::name::NAME \
  }

namespace Project::Graph::Node
{
  // Legacy aliases: the validator at Graph::validate() distinguishes
  // exec edges from data edges by pointer identity against these two
  // shared_ptrs (see isLogicPin lambda below). Routing them through the
  // canonical PinDataType singletons keeps that test working while every
  // node migrates to the typed pinStyle(...) API.
  std::shared_ptr<ImFlow::PinStyle> PIN_STYLE_LOGIC =
    ::Project::Graph::pinStyle(::Project::Graph::PinDataType::Exec);
  std::shared_ptr<ImFlow::PinStyle> PIN_STYLE_VALUE =
    ::Project::Graph::pinStyle(::Project::Graph::PinDataType::Float);
}

namespace Project::Graph
{
  auto NODE_TABLE = std::to_array<TableEntry>({
    TABLE_ENTRY(Start),
    TABLE_ENTRY(Wait),
    TABLE_ENTRY(ObjDel),
    TABLE_ENTRY(ObjEvent),
    TABLE_ENTRY(Compare),
    TABLE_ENTRY(Value),
    TABLE_ENTRY(Repeat),
    TABLE_ENTRY(Func),
    TABLE_ENTRY(CompBool),
    TABLE_ENTRY(SceneLoad),
    TABLE_ENTRY(Arg),
    TABLE_ENTRY(SwitchCase),
    TABLE_ENTRY(Note),
    // Prefab-only nodes (Phase 3.2). Indices appended to keep saved graphs
    // stable; the persisted "type" field is an index into this table. The
    // TYPE_PREFAB_* constants in graph.h reference these indices — bump the
    // constants if you reorder anything here.
    TABLE_ENTRY(PrefabEvent),     // 13
    TABLE_ENTRY(PrefabFunc),      // 14 — TYPE_PREFAB_FUNC
    TABLE_ENTRY(PrefabVarGet),    // 15 — TYPE_PREFAB_VAR_GET
    TABLE_ENTRY(Reroute),         // 16 — routing knot
    TABLE_ENTRY(DeltaTime),       // 17 — Float pin: current frame deltaTime
    TABLE_ENTRY(MathAdd),         // 18
    TABLE_ENTRY(MathSub),         // 19
    TABLE_ENTRY(MathMul),         // 20
    TABLE_ENTRY(MathDiv),         // 21
    TABLE_ENTRY(MathMod),         // 22
    TABLE_ENTRY(MathMin),         // 23
    TABLE_ENTRY(MathMax),         // 24
    TABLE_ENTRY(MathClamp),       // 25
    TABLE_ENTRY(MathAbs),         // 26
    TABLE_ENTRY(MathFloor),       // 27
    TABLE_ENTRY(MathCeil),        // 28
    TABLE_ENTRY(MathRound),       // 29
    TABLE_ENTRY(MathSign),        // 30
    TABLE_ENTRY(MathSqrt),        // 31
    TABLE_ENTRY(MathPow),         // 32
    TABLE_ENTRY(BoolAnd),         // 33
    TABLE_ENTRY(BoolOr),          // 34
    TABLE_ENTRY(BoolNot),         // 35
    TABLE_ENTRY(BoolXor),         // 36
    TABLE_ENTRY(CmpEq),           // 37
    TABLE_ENTRY(CmpNe),           // 38
    TABLE_ENTRY(CmpLt),           // 39
    TABLE_ENTRY(CmpLe),           // 40
    TABLE_ENTRY(CmpGt),           // 41
    TABLE_ENTRY(CmpGe),           // 42
    TABLE_ENTRY(RandomFloat),     // 43
    TABLE_ENTRY(RandomInt),       // 44
    TABLE_ENTRY(StringConst),     // 45
    TABLE_ENTRY(StringConcat),    // 46
    TABLE_ENTRY(ToString),        // 47
    TABLE_ENTRY(StringLength),    // 48
    TABLE_ENTRY(Substring),       // 49
    TABLE_ENTRY(StringFormat),    // 50
    TABLE_ENTRY(ArrayMake),       // 51
    TABLE_ENTRY(ArrayLength),     // 52
    TABLE_ENTRY(ArrayGet),        // 53
    TABLE_ENTRY(ArraySet),        // 54
    TABLE_ENTRY(ArrayPush),       // 55
    TABLE_ENTRY(ArrayPop),        // 56
    TABLE_ENTRY(ArrayInsert),     // 57
    TABLE_ENTRY(ArrayRemoveAt),   // 58
    TABLE_ENTRY(ArrayClear),      // 59
    TABLE_ENTRY(ArrayFind),       // 60
    TABLE_ENTRY(ArrayContains),   // 61
    TABLE_ENTRY(ForRange),        // 62
    TABLE_ENTRY(While),           // 63
    TABLE_ENTRY(ForEach),         // 64
    TABLE_ENTRY(Break),           // 65
    TABLE_ENTRY(Continue),        // 66
    TABLE_ENTRY(PrefabSuper),     // 67 — Super:: call into parent prefab dispatcher
    TABLE_ENTRY(SaveCommit),      // 68
    TABLE_ENTRY(SaveReload),      // 69
    TABLE_ENTRY(SaveClearAll),    // 70
    TABLE_ENTRY(SaveGetInt),      // 71
    TABLE_ENTRY(SaveGetFloat),    // 72
    TABLE_ENTRY(SaveGetBool),     // 73
    TABLE_ENTRY(SaveGetString),   // 74
    TABLE_ENTRY(SaveSetInt),      // 75
    TABLE_ENTRY(SaveSetFloat),    // 76
    TABLE_ENTRY(SaveSetBool),     // 77
    TABLE_ENTRY(SaveSetString),   // 78
  });

  const std::vector<std::string> & Graph::getNodeNames()
  {
    static std::vector<std::string> names = {};
    if(names.empty()) {
      for(const auto &entry : NODE_TABLE) {
        names.emplace_back(entry.name);
      }
    }
    return names;
  }

  // Palette metadata. Indices MUST stay in lockstep with NODE_TABLE
  // above. The category column drives the grouped view in the Add-Node
  // popup; the type masks drive context-aware filtering when the popup
  // is opened from a dropped link. Pin types here mirror what each
  // node's ctor calls addIN/addOUT with (mostly Exec for control flow,
  // Float as the legacy "value" stand-in until typed value pins land).
  std::span<const ::Editor::NodePalette::Entry> Graph::getPaletteEntries()
  {
    using namespace ::Editor::NodePalette;
    using ::Project::Graph::PinDataType;
    constexpr TypeMask EXEC  = TypeMask{1u} << static_cast<uint32_t>(PinDataType::Exec);
    constexpr TypeMask FLOAT = TypeMask{1u} << static_cast<uint32_t>(PinDataType::Float);
    constexpr TypeMask INT   = TypeMask{1u} << static_cast<uint32_t>(PinDataType::Int);
    constexpr TypeMask BOOL  = TypeMask{1u} << static_cast<uint32_t>(PinDataType::Bool);
    constexpr TypeMask STR   = TypeMask{1u} << static_cast<uint32_t>(PinDataType::String);

    static const Entry table[] = {
      // typeIndex, name,                                                 category,         inTypes,       outTypes
      {  0, Node::Start::NAME,         "Events",       0,            EXEC          },
      {  1, Node::Wait::NAME,          "Flow Control", EXEC,         EXEC          },
      {  2, Node::ObjDel::NAME,        "Functions",    EXEC,         EXEC          },
      {  3, Node::ObjEvent::NAME,      "Functions",    EXEC,         EXEC          },
      {  4, Node::Compare::NAME,       "Flow Control", EXEC | FLOAT, EXEC          },
      {  5, Node::Value::NAME,         "Variables",    0,            FLOAT         },
      {  6, Node::Repeat::NAME,        "Flow Control", EXEC,         EXEC          },
      {  7, Node::Func::NAME,          "Functions",    EXEC,         EXEC | FLOAT  },
      {  8, Node::CompBool::NAME,      "Flow Control", EXEC | FLOAT, EXEC          },
      {  9, Node::SceneLoad::NAME,     "Functions",    EXEC | FLOAT, EXEC          },
      { 10, Node::Arg::NAME,           "Variables",    0,            FLOAT         },
      { 11, Node::SwitchCase::NAME,    "Flow Control", EXEC | FLOAT, EXEC          },
      { 12, Node::Note::NAME,          "Comments",     0,            0             },
      { 13, Node::PrefabEvent::NAME,   "Events",       0,            EXEC          },
      { 14, Node::PrefabFunc::NAME,    "Functions",    EXEC,         EXEC          },
      { 15, Node::PrefabVarGet::NAME,  "Variables",    0,            FLOAT         },
      { 16, Node::Reroute::NAME,       "Flow Control", EXEC,         EXEC          },
      { 17, Node::DeltaTime::NAME,     "Variables",    0,            FLOAT         },
      { 18, Node::MathAdd::NAME,       "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 19, Node::MathSub::NAME,       "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 20, Node::MathMul::NAME,       "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 21, Node::MathDiv::NAME,       "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 22, Node::MathMod::NAME,       "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 23, Node::MathMin::NAME,       "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 24, Node::MathMax::NAME,       "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 25, Node::MathClamp::NAME,     "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 26, Node::MathAbs::NAME,       "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 27, Node::MathFloor::NAME,     "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 28, Node::MathCeil::NAME,      "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 29, Node::MathRound::NAME,     "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 30, Node::MathSign::NAME,      "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 31, Node::MathSqrt::NAME,      "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 32, Node::MathPow::NAME,       "Math",         EXEC | FLOAT, EXEC | FLOAT  },
      { 33, Node::BoolAnd::NAME,       "Logic",        EXEC,         EXEC          },
      { 34, Node::BoolOr::NAME,        "Logic",        EXEC,         EXEC          },
      { 35, Node::BoolNot::NAME,       "Logic",        EXEC,         EXEC          },
      { 36, Node::BoolXor::NAME,       "Logic",        EXEC,         EXEC          },
      { 37, Node::CmpEq::NAME,         "Logic",        EXEC | FLOAT, EXEC          },
      { 38, Node::CmpNe::NAME,         "Logic",        EXEC | FLOAT, EXEC          },
      { 39, Node::CmpLt::NAME,         "Logic",        EXEC | FLOAT, EXEC          },
      { 40, Node::CmpLe::NAME,         "Logic",        EXEC | FLOAT, EXEC          },
      { 41, Node::CmpGt::NAME,         "Logic",        EXEC | FLOAT, EXEC          },
      { 42, Node::CmpGe::NAME,         "Logic",        EXEC | FLOAT, EXEC          },
      { 43, Node::RandomFloat::NAME,   "Random",       EXEC | FLOAT, EXEC | FLOAT  },
      { 44, Node::RandomInt::NAME,     "Random",       EXEC | INT,   EXEC | INT    },
      { 45, Node::StringConst::NAME,   "String",       0,            STR           },
      { 46, Node::StringConcat::NAME,  "String",       EXEC | STR,   EXEC | STR    },
      { 47, Node::ToString::NAME,      "String",       EXEC | FLOAT, EXEC | STR    },
      { 48, Node::StringLength::NAME,  "String",       EXEC | STR,   EXEC | INT    },
      { 49, Node::Substring::NAME,     "String",       EXEC | STR | INT, EXEC | STR },
      { 50, Node::StringFormat::NAME,  "String",       EXEC | STR,   EXEC | STR    },
      { 51, Node::ArrayMake::NAME,     "Array",        EXEC | FLOAT, EXEC          },
      { 52, Node::ArrayLength::NAME,   "Array",        EXEC,         EXEC | INT    },
      { 53, Node::ArrayGet::NAME,      "Array",        EXEC | INT,   EXEC | FLOAT  },
      { 54, Node::ArraySet::NAME,      "Array",        EXEC | INT | FLOAT, EXEC    },
      { 55, Node::ArrayPush::NAME,     "Array",        EXEC | FLOAT, EXEC          },
      { 56, Node::ArrayPop::NAME,      "Array",        EXEC,         EXEC | FLOAT  },
      { 57, Node::ArrayInsert::NAME,   "Array",        EXEC | INT | FLOAT, EXEC    },
      { 58, Node::ArrayRemoveAt::NAME, "Array",        EXEC | INT,   EXEC          },
      { 59, Node::ArrayClear::NAME,    "Array",        EXEC,         EXEC          },
      { 60, Node::ArrayFind::NAME,     "Array",        EXEC | FLOAT, EXEC | INT    },
      { 61, Node::ArrayContains::NAME, "Array",        EXEC | FLOAT, EXEC | BOOL   },
      { 62, Node::ForRange::NAME,      "Flow Control", EXEC | INT,   EXEC | INT    },
      { 63, Node::While::NAME,         "Flow Control", EXEC | BOOL,  EXEC          },
      { 64, Node::ForEach::NAME,       "Flow Control", EXEC,         EXEC | FLOAT  },
      { 65, Node::Break::NAME,         "Flow Control", EXEC,         0             },
      { 66, Node::Continue::NAME,      "Flow Control", EXEC,         0             },
      { 67, Node::PrefabSuper::NAME,   "Functions",    EXEC,         EXEC          },
      { 68, Node::SaveCommit::NAME,    "Save",         EXEC,         EXEC          },
      { 69, Node::SaveReload::NAME,    "Save",         EXEC,         EXEC          },
      { 70, Node::SaveClearAll::NAME,  "Save",         EXEC,         EXEC          },
      { 71, Node::SaveGetInt::NAME,    "Save",         EXEC,         EXEC | INT    },
      { 72, Node::SaveGetFloat::NAME,  "Save",         EXEC,         EXEC | FLOAT  },
      { 73, Node::SaveGetBool::NAME,   "Save",         EXEC,         EXEC | BOOL   },
      { 74, Node::SaveGetString::NAME, "Save",         EXEC,         EXEC | STR    },
      { 75, Node::SaveSetInt::NAME,    "Save",         EXEC | INT,   EXEC          },
      { 76, Node::SaveSetFloat::NAME,  "Save",         EXEC | FLOAT, EXEC          },
      { 77, Node::SaveSetBool::NAME,   "Save",         EXEC | BOOL,  EXEC          },
      { 78, Node::SaveSetString::NAME, "Save",         EXEC | STR,   EXEC          },
    };
    static_assert(sizeof(table) / sizeof(table[0]) == NODE_TABLE.size(),
      "Palette entries out of sync with NODE_TABLE");
    return table;
  }

  std::shared_ptr<Node::Base> Graph::addNode(uint32_t type, const ImVec2 &pos)
  {
    assert(type < NODE_TABLE.size() && "Unknown node type in graph addNode");
    auto newNode = NODE_TABLE[type].create(graph, pos);
    newNode->type = type;
    newNode->uuid = Utils::Hash::randomU64();
    return newNode;
  }

  bool Graph::deserialize(const std::string &jsonData)
  {
    auto nodeData = nlohmann::json::parse(jsonData);

    std::unordered_map<uint64_t, std::shared_ptr<Node::Base>> newNodes{};
    for(auto &savedNode : nodeData["nodes"]) {
      uint32_t type = savedNode["type"];
      if(type >= NODE_TABLE.size()) {
        // Future / corrupted graph file. Drop the node and keep loading so
        // the user sees the rest of the graph instead of an empty editor.
        // build() will additionally re-emit a structured ERROR per asset.
        Utils::Logger::log(
          "Unknown node type " + std::to_string(type) + " in graph; skipping node.",
          Utils::Logger::LEVEL_ERROR
        );
        continue;
      }
      auto newNode = NODE_TABLE[type].create(graph, {});
      newNode->deserialize(savedNode);
      newNode->setPos({savedNode["pos"][0], savedNode["pos"][1]});
      newNode->type = type;
      newNode->uuid = savedNode["uuid"];
      newNodes[newNode->uuid] = newNode;
    }

    for(auto &savedLink : nodeData["links"]) {
      auto nodeAIt = newNodes.find(savedLink["src"]);
      auto nodeBIt = newNodes.find(savedLink["dst"]);
      if(nodeAIt != newNodes.end() && nodeBIt != newNodes.end()) {
        auto &outs = nodeAIt->second->getOuts();
        auto &ins = nodeBIt->second->getIns();
        uint32_t srcIndex = savedLink.value("srcPort", 0);
        uint32_t dstIndex = savedLink.value("dstPort", 0);

        auto pinA = srcIndex < outs.size() ? outs[ srcIndex ].get() : nullptr;
        auto pinB = dstIndex < ins.size() ? ins[ dstIndex ].get() : nullptr;
        if(pinA && pinB) {
          pinA->createLink(pinB);
        }
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
      jNode["pos"] = {p64Node->getPos().x, p64Node->getPos().y};
      p64Node->serialize(jNode);
      data["nodes"].push_back(jNode);
    }

    data["links"] = nlohmann::json::array();
    auto &links = graph.getLinks();
    for (const auto& weakLink : links) {
      if (auto link = weakLink.lock()) {
        auto leftPin = link->left();
        auto rightPin = link->right();

        if (leftPin && rightPin) {
          auto leftNode = leftPin->getParent();
          auto rightNode = rightPin->getParent();
          if(leftNode && rightNode) {

            uint32_t leftIndex = getIndexLeft(leftPin);
            uint32_t rightIndex = getIndexRight(rightPin);

            /*printf("Node Link: %s:%s:%d -> %s:%s:%d\n",
              leftNode->getName().c_str(), leftPin->getName().c_str(), leftIndex,
              rightNode->getName().c_str(), rightPin->getName().c_str(), rightIndex
            );*/
            nlohmann::json jLink{};
            jLink["src"] = ((Node::Base*)leftNode)->uuid;
            jLink["srcPort"] = leftIndex;
            jLink["dst"] = ((Node::Base*)rightNode)->uuid;
            jLink["dstPort"] = rightIndex;
            data["links"].push_back(jLink);
          }
        }
      }
    }

    return data.dump(2);
  }

  void Graph::validate(
    ::Project::Compile::ErrorList *errs,
    uint64_t assetUUID
  )
  {
    if(!errs) return;
    auto &nodes = graph.getNodes();
    if(nodes.empty()) return;

    auto pushErr = [&](::Project::Compile::Severity sev, uint64_t nodeUUID, std::string msg) {
      errs->push(::Project::Compile::Error{sev, assetUUID, nodeUUID, msg});
      // Mirror to the Logger stream so the existing Log window remains a
      // superset view; the structured panel is the navigable slice.
      Utils::Logger::log(msg,
        sev == ::Project::Compile::Severity::ERROR
          ? Utils::Logger::LEVEL_ERROR
          : Utils::Logger::LEVEL_WARN);
    };

    // Pin-style identity check: graph nodes share the two singleton style
    // shared_ptrs PIN_STYLE_LOGIC / PIN_STYLE_VALUE, so pointer equality
    // separates logic edges from value edges without inspecting fields.
    auto isLogicPin = [](ImFlow::Pin *p) {
      return p && p->getStyle().get() == ::Project::Graph::Node::PIN_STYLE_LOGIC.get();
    };

    std::unordered_set<uint64_t> hasIncomingLogic{};
    for(const auto &weakLink : graph.getLinks()) {
      if(auto link = weakLink.lock()) {
        auto leftPin  = link->left();
        auto rightPin = link->right();
        if(leftPin && rightPin && isLogicPin(leftPin) && isLogicPin(rightPin)) {
          auto rightNode = (Node::Base*)rightPin->getParent();
          if(rightNode) hasIncomingLogic.insert(rightNode->uuid);
        }
      }
    }

    // Entry-point detection. Type 0 is Start (canonical entry). Type 13 is
    // PrefabEvent — also an entry point used by prefab event graphs (Ready
    // / Enable / Disable etc.). Either is sufficient for the graph to have
    // an entry. The constants below mirror NODE_TABLE order in this file.
    constexpr uint32_t TYPE_START        = 0;
    constexpr uint32_t TYPE_PREFAB_EVENT = 13;

    uint32_t startCount = 0;
    uint64_t firstExtraStart = 0;
    bool hasAnyEntry = false;
    for(const auto &node : nodes | std::views::values) {
      auto p64Node = (Node::Base*)node.get();
      if(p64Node->type == TYPE_START) {
        ++startCount;
        if(startCount == 2) firstExtraStart = p64Node->uuid;
        hasAnyEntry = true;
      } else if(p64Node->type == TYPE_PREFAB_EVENT) {
        hasAnyEntry = true;
      }
    }

    if(!hasAnyEntry) {
      pushErr(::Project::Compile::Severity::ERROR, 0,
        "Graph has no entry node (Start or PrefabEvent).");
    }

    if(startCount > 1) {
      pushErr(::Project::Compile::Severity::WARNING, firstExtraStart,
        "Graph has " + std::to_string(startCount) +
        " Start nodes; only the first one is reachable.");
    }

    // Per-node reachability: any node whose first input pin is logic-styled
    // but has no incoming logic edge can never run. Entry-point types are
    // skipped (they have no logic-in pin and the loop check naturally
    // excludes them, but we still guard against future schema changes).
    for(const auto &node : nodes | std::views::values) {
      auto p64Node = (Node::Base*)node.get();
      if(p64Node->type == TYPE_START)        continue;
      if(p64Node->type == TYPE_PREFAB_EVENT) continue;

      auto &ins = p64Node->getIns();
      if(ins.empty()) continue;
      if(!isLogicPin(ins[0].get())) continue;

      if(!hasIncomingLogic.contains(p64Node->uuid)) {
        pushErr(::Project::Compile::Severity::ERROR, p64Node->uuid,
          "Node '" + p64Node->getName() + "' is unreachable (no incoming logic edge).");
      }
    }
  }

  void Graph::build(
    Utils::BinaryFile &f,
    std::string &source,
    uint64_t uuid,
    ::Project::Compile::ErrorList *errs,
    uint64_t assetUUID
  )
  {
    // Run validation up front. Code emission still proceeds afterwards — the
    // build driver is the policy point that decides whether errorCount() > 0
    // turns into a project-build failure.
    validate(errs, assetUUID);
    auto &nodes = graph.getNodes();

    uint16_t stackSize = 4096;
    f.write<uint64_t>(uuid);
    f.write<uint16_t>(stackSize);


    // maps a node's UUID to its own position in the file
    std::unordered_map<uint64_t, uint32_t> nodeSelfPosMap{};
    // map of nodes and their outgoing links to other nodes
    std::unordered_map<uint64_t, std::vector<uint64_t>> nodeOutgoingMap{};
    std::unordered_map<uint64_t, std::vector<uint64_t>> nodeIngoingValMap{};

    // collect all active links
    for (const auto& weakLink : graph.getLinks())
    {
      if (auto link = weakLink.lock()) {
        auto leftPin = link->left();
        auto rightPin = link->right();
        if (leftPin && rightPin) {
          auto leftNode = (Node::Base*)leftPin->getParent();
          auto rightNode = (Node::Base*)rightPin->getParent();

          uint32_t leftIndex = getIndexLeft(leftPin);
          uint32_t rightIndex = getIndexRight(rightPin);

          /*printf("Link: %016llX @ %d %s:%s -> %016llX @ %d %s:%s\n",
            leftNode->uuid, leftIndex,
            leftNode->getName().c_str(), leftPin->getName().c_str(),
            rightNode->uuid, rightIndex,
            rightNode->getName().c_str(), rightPin->getName().c_str()
          );*/

          auto &e = nodeOutgoingMap[leftNode->uuid];
          if(leftIndex >= e.size()) {
            e.resize(leftIndex + 1, 0);
          }
          e[leftIndex] = rightNode->uuid;

          // for value nodes, also track ingoing connections
          auto &ev = nodeIngoingValMap[rightNode->uuid];
          if(rightIndex >= ev.size()) {
            ev.resize(rightIndex + 1, 0);
          }
          ev[rightIndex] = leftNode->uuid;
        }
      }
    }

    BuildCtx nodeCtx{};
    nodeCtx.source = "";

    // convert nodes to vector, and make sure the start node (type=0) is first
    std::vector<Node::Base*> nodeVec{};
    std::unordered_map<uint64_t, Node::Base*> nodeMap{};
    nodeVec.reserve(nodes.size());
    for(const auto &node : nodes | std::views::values)
    {
      auto p64Node = (Node::Base*)node.get();
      if(p64Node->type == 0) {
        nodeVec.insert(nodeVec.begin(), (Node::Base*)node.get());
      } else {
        nodeVec.push_back((Node::Base*)node.get());
      }
      nodeMap[p64Node->uuid] = p64Node;
    }


    for(auto &[nodeUUID, ingoingVals] : nodeIngoingValMap)
    {
      if(ingoingVals.empty())continue;

      auto p64Node = nodeMap.at(nodeUUID);
      // only keep indices where type is 1 in p64Node->valInputTypes
      std::vector<uint64_t> filteredIngoingVals{};
      for(size_t i = 0; i < p64Node->valInputTypes.size(); ++i)
      {
        if(p64Node->valInputTypes[i] == 1 && i < ingoingVals.size()){
          filteredIngoingVals.push_back(ingoingVals[i]);
        }
      }
      ingoingVals = filteredIngoingVals;
    }

    source += R"(#include <script/nodeGraph.h>)" "\n";
    source += R"(#include <scene/object.h>)" "\n";
    source += R"(#include <scene/scene.h>)" "\n";
    source += R"(#include <math.h>)" "\n";   // Group A math nodes use fmodf/sqrtf/fabsf/etc.
    source += R"(#include <string>)" "\n";   // Group C string nodes use std::string
    source += R"(#include <cstdio>)" "\n";   // StringFormat uses snprintf
    source += R"(#include <vector>)" "\n";   // Group D arrays
    source += R"(#include <type_traits>)" "\n"; // decltype(arr)::value_type element-kind inference
    source += "\n";

    source += "namespace P64::NodeGraph::G" + Utils::toHex64(uuid) + " {\n";
    source += R"(void run(void* arg) {)" "\n";

    source += R"(  P64::NodeGraph::Instance* inst = (P64::NodeGraph::Instance*)arg; )" "\n";
    // Bind deltaTime as a reference so the DeltaTime node (and any
    // other ticked-value users) can resolve the identifier. The host
    // refreshes Instance::lastDeltaTime before each coro_resume.
    source += R"(  float& deltaTime = inst->lastDeltaTime; (void)deltaTime;)" "\n";

    auto nodeLabel = [&](uint64_t uuid) {
      return "NODE_" + Utils::toHex64(uuid);
    };

    // Identify exec output count per node (exec pins come before
    // value pins by convention in every node ctor today). Used to
    // limit BFS to exec edges only when computing loop body subgraphs.
    auto execOutCount = [&](Node::Base* n) -> uint32_t {
      auto execStyle = ::Project::Graph::pinStyle(::Project::Graph::PinDataType::Exec).get();
      auto &outs = n->getOuts();
      uint32_t cnt = 0;
      for (auto &p : outs) {
        if (!p) break;
        if (p->getStyle().get() != execStyle) break;
        cnt++;
      }
      return cnt;
    };

    // Pre-walk: identify each loop's body subgraph by exec-BFS from
    // outUUIDs[0] (Body), stopping at nested loop nodes (which keep
    // their body to themselves). First-come-wins on loopOwner so the
    // outermost loop encountered owns shared nodes.
    std::unordered_map<uint64_t, uint64_t> loopOwner;
    {
      std::function<void(uint64_t, uint64_t, std::unordered_set<uint64_t>&)> markBody;
      markBody = [&](uint64_t nodeUUID, uint64_t loopUUID,
                     std::unordered_set<uint64_t> &visited) {
        if (nodeUUID == 0 || visited.count(nodeUUID)) return;
        visited.insert(nodeUUID);
        auto it = nodeMap.find(nodeUUID);
        if (it == nodeMap.end()) return;
        auto* n = it->second;
        if (!loopOwner.count(n->uuid)) loopOwner[n->uuid] = loopUUID;
        // Inner loop encountered: mark it as body of the outer loop
        // but don't walk its body (it owns its own). Walk only its
        // Done branch (post-inner-loop, still in outer body).
        auto outIt = nodeOutgoingMap.find(n->uuid);
        if (outIt == nodeOutgoingMap.end()) return;
        if (n->isLoop()) {
          if (outIt->second.size() > 1) {
            markBody(outIt->second[1], loopUUID, visited);
          }
          return;
        }
        uint32_t ec = execOutCount(n);
        for (uint32_t i = 0; i < ec && i < outIt->second.size(); ++i) {
          markBody(outIt->second[i], loopUUID, visited);
        }
      };
      for (auto* loop : nodeVec) {
        if (!loop->isLoop()) continue;
        auto outIt = nodeOutgoingMap.find(loop->uuid);
        if (outIt == nodeOutgoingMap.end() || outIt->second.empty()) continue;
        std::unordered_set<uint64_t> visited;
        visited.insert(loop->uuid);
        markBody(outIt->second[0], loop->uuid, visited);
      }
    }

    // Recursive emitter: handles loop nodes by inlining their body
    // subgraph between buildLoopHeader / buildLoopFooter. Top-level
    // call passes insideLoop=false; loop body iterations recurse
    // with insideLoop=true so jump() turns chain falloff into
    // continue; instead of return;.
    std::function<void(Node::Base*, bool)> emitNode;
    emitNode = [&](Node::Base* node, bool insideLoop) {
      auto savedOut = nodeCtx.outUUIDs;
      auto savedIn  = nodeCtx.inValUUIDs;
      auto savedFlag = nodeCtx.insideLoopBody;
      nodeCtx.outUUIDs = &nodeOutgoingMap[node->uuid];
      nodeCtx.inValUUIDs = &nodeIngoingValMap[node->uuid];
      nodeCtx.insideLoopBody = insideLoop;

      nodeCtx.source += "  " + nodeLabel(node->uuid) + ": // " + node->getName() + "\n";
      nodeCtx.source += "  {\n";

      if (node->isLoop()) {
        node->build(nodeCtx);
        node->buildLoopHeader(nodeCtx);
        for (auto* body : nodeVec) {
          auto it = loopOwner.find(body->uuid);
          if (it == loopOwner.end() || it->second != node->uuid) continue;
          emitNode(body, true);
        }
        node->buildLoopFooter(nodeCtx);
        // After the for/while exits, route to the loop's Done branch
        // (outUUIDs[1]). Restore the pointer since the recursive
        // body emissions reset it.
        nodeCtx.outUUIDs = &nodeOutgoingMap[node->uuid];
        if (nodeCtx.outUUIDs->size() > 1 && nodeCtx.outUUIDs->at(1)) {
          nodeCtx.line("goto NODE_" + Utils::toHex64(nodeCtx.outUUIDs->at(1)) + ";");
        } else if (insideLoop) {
          nodeCtx.line("continue;");
        } else {
          nodeCtx.line("return;");
        }
      } else {
        node->build(nodeCtx);
        if (nodeCtx.outUUIDs->empty() || nodeCtx.outUUIDs->at(0) == 0) {
          nodeCtx.line(insideLoop ? "continue;" : "return;");
        } else {
          nodeCtx.jump(0);
        }
      }

      nodeCtx.source += "  }\n";
      nodeCtx.outUUIDs = savedOut;
      nodeCtx.inValUUIDs = savedIn;
      nodeCtx.insideLoopBody = savedFlag;
    };

    // Pure-eval pre-pass: any node opted in via canBePure() that has
    // no incoming exec edge gets its globalVar emitted at function-top
    // in topological-dependency order, with its expression inlined
    // into the initializer (no NODE block, no exec wires required).
    // This enables UE-style "value Add fed straight into Set" without
    // needing a redundant exec wire through the math.
    std::unordered_set<uint64_t> emittedPure;
    {
      auto execStyle2 = ::Project::Graph::pinStyle(::Project::Graph::PinDataType::Exec).get();
      std::unordered_set<uint64_t> hasIncomingExec;
      for (const auto &weak : graph.getLinks()) {
        auto link = weak.lock();
        if (!link) continue;
        auto rightPin = link->right();
        if (!rightPin) continue;
        if (rightPin->getStyle().get() != execStyle2) continue;
        auto rightNode = (Node::Base*)rightPin->getParent();
        if (rightNode) hasIncomingExec.insert(rightNode->uuid);
      }

      // inProgress tracks the current recursion stack so a cycle in
      // the value-dependency graph is caught instead of overflowing
      // the host stack. On detection we emit a static_assert into
      // the generated source that names the offending node, so the
      // host compiler fails loudly during build and the user can
      // locate the cycle by uuid.
      std::unordered_set<uint64_t> inProgress;
      std::function<void(Node::Base*)> emitPure;
      emitPure = [&](Node::Base *n) {
        if (emittedPure.count(n->uuid)) return;
        if (inProgress.count(n->uuid)) {
          nodeCtx.vars.push_back({"[[maybe_unused]] static constexpr bool",
            "p64_cyclic_pure_" + Utils::toHex64(n->uuid),
            "([] { static_assert(false, \"Cyclic pure-value dependency at node "
              + n->getName() + " (" + Utils::toHex64(n->uuid)
              + ")\"); return false; }())"});
          emittedPure.insert(n->uuid);
          return;
        }
        inProgress.insert(n->uuid);
        // Recurse into pure-eligible value-input deps first so this
        // node's initializer references already-declared globalVars.
        auto inIt = nodeIngoingValMap.find(n->uuid);
        if (inIt != nodeIngoingValMap.end()) {
          for (uint64_t inUUID : inIt->second) {
            if (inUUID == 0) continue;
            auto it = nodeMap.find(inUUID);
            if (it == nodeMap.end()) continue;
            auto* up = it->second;
            if (up->canBePure() && !hasIncomingExec.count(up->uuid)) {
              emitPure(up);
            }
          }
        }
        auto savedOut = nodeCtx.outUUIDs;
        auto savedIn  = nodeCtx.inValUUIDs;
        nodeCtx.outUUIDs   = &nodeOutgoingMap[n->uuid];
        nodeCtx.inValUUIDs = &nodeIngoingValMap[n->uuid];
        n->buildAsPure(nodeCtx);
        nodeCtx.outUUIDs   = savedOut;
        nodeCtx.inValUUIDs = savedIn;
        inProgress.erase(n->uuid);
        emittedPure.insert(n->uuid);
      };

      for (auto* n : nodeVec) {
        if (!n->canBePure()) continue;
        if (hasIncomingExec.count(n->uuid)) continue;
        emitPure(n);
      }
    }

    // Top-level emission: skip nodes owned by a loop (they're
    // inlined via emitNode's loop recursion) and skip pure nodes
    // already emitted at function top.
    for (const auto &node : nodeVec) {
      if (loopOwner.count(node->uuid)) continue;
      if (emittedPure.count(node->uuid)) continue;
      emitNode(node, false);
    }

    source += "\n// ==== GLOBAL VARS ==== //\n";
    for(auto &globalVar : nodeCtx.vars) {
      source += "  " + globalVar.type + " " + globalVar.name + " = " + globalVar.value + ";\n";
    }

    source += "\n// ==== CODE ==== //\n";
    source += nodeCtx.source;
    source += "}\n";
    source += "}\n";

  }
}
