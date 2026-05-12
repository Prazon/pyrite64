#include "prefabScaffolder.h"

#include <array>
#include <unordered_set>

#include "json.hpp"

#include "graph/graph.h"
#include "prefabFunctions.h"
#include "scene/prefab.h"
#include "../utils/hash.h"
#include "../utils/string.h"

namespace
{
  using Project::PrefabScaffolder::LifecycleEvent;
  using Kind = Project::Graph::Node::PrefabEvent::Kind;

  // Mirrors the historical seeds in PrefabEventGraphEditor's ctor. Order is
  // load-bearing: the i'th entry's PrefabEvent is wired to the i'th
  // PrefabFunc when seedDefaultEventGraph builds its JSON.
  constexpr std::array<LifecycleEvent, 3> kLifecycle{{
    {Kind::Ready,   "OnReady"},
    {Kind::Enable,  "OnEnable"},
    {Kind::Disable, "OnDisable"},
  }};

  // Walk the saved-graph "nodes" array and skim only what the scaffolder
  // needs — type index plus the per-node payload fields it cares about.
  // Cheap enough to do on every CLI call; avoids spinning up an ImNodeFlow
  // graph (which needs an active ImGui context).
  bool parseGraphJSON(const std::string &json, nlohmann::json &out)
  {
    if (json.empty()) return false;
    try { out = nlohmann::json::parse(json); }
    catch (...) { return false; }
    return out.is_object() && out.contains("nodes");
  }
}

// Inside Project::PrefabScaffolder, unqualified names resolve to ::Project::*
// via enclosing-namespace lookup. Writing the fully-qualified Project::
// would otherwise collide with the `class Project` symbol in the same
// namespace.
namespace Project::PrefabScaffolder
{
  std::span<const LifecycleEvent> lifecycleEvents() { return kLifecycle; }

  std::vector<std::string> seedLifecycleFunctions(
    const std::string &projectPath,
    const std::string &prefabFileName)
  {
    std::vector<std::string> added;
    if (prefabFileName.empty()) return added;

    // ensurePrefabUserSource is the standard scaffold path; addPrefabFunction
    // also calls it internally, but invoking it up front means a prefab with
    // no missing functions still gets a header pair (matches the GUI's
    // existing behavior on New Prefab).
    ensurePrefabUserSource(projectPath, prefabFileName);

    auto existing = scanPrefabFunctions(projectPath, prefabFileName);
    std::unordered_set<std::string> have;
    have.reserve(existing.size());
    for (const auto &f : existing) have.insert(f.name);

    for (const auto &ev : kLifecycle) {
      if (have.contains(ev.funcName)) continue;
      if (addPrefabFunction(projectPath, prefabFileName, ev.funcName)) {
        added.emplace_back(ev.funcName);
      }
    }
    return added;
  }

  bool seedDefaultEventGraph(Prefab &prefab)
  {
    // If the prefab already has any nodes, leave it alone — Tier 1 is a
    // first-author-time helper, not a re-templater.
    if (!prefab.eventGraphJSON.empty()) {
      nlohmann::json existing;
      if (parseGraphJSON(prefab.eventGraphJSON, existing)
          && existing["nodes"].is_array()
          && !existing["nodes"].empty()) {
        return false;
      }
    }

    nlohmann::json root;
    root["nodes"] = nlohmann::json::array();
    root["links"] = nlohmann::json::array();

    // Layout: events on the left column, function calls on the right column.
    // Y spacing matches the editor's original seed spacing (140px apart).
    constexpr float COL_EVENT_X = 40.0f;
    constexpr float COL_FUNC_X  = 280.0f;
    constexpr float ROW_Y0      = 40.0f;
    constexpr float ROW_DY      = 140.0f;

    for (size_t i = 0; i < kLifecycle.size(); ++i) {
      const auto &ev = kLifecycle[i];
      float y = ROW_Y0 + ROW_DY * static_cast<float>(i);

      uint64_t eventUUID = Utils::Hash::randomU64();
      uint64_t funcUUID  = Utils::Hash::randomU64();

      // PrefabEvent — NODE_TABLE index 13 (declared in graph.cpp).
      nlohmann::json eventNode;
      eventNode["uuid"] = eventUUID;
      eventNode["type"] = 13;
      eventNode["pos"]  = {COL_EVENT_X, y};
      eventNode["kind"] = static_cast<int>(ev.kind);
      root["nodes"].push_back(eventNode);

      // PrefabFunc — TYPE_PREFAB_FUNC = 14.
      nlohmann::json funcNode;
      funcNode["uuid"]     = funcUUID;
      funcNode["type"]     = Graph::TYPE_PREFAB_FUNC;
      funcNode["pos"]      = {COL_FUNC_X, y};
      funcNode["funcName"] = ev.funcName;
      root["nodes"].push_back(funcNode);

      // Exec wire from event[OUT 0] → func[IN 0].
      nlohmann::json link;
      link["src"]     = eventUUID;
      link["srcPort"] = 0;
      link["dst"]     = funcUUID;
      link["dstPort"] = 0;
      root["links"].push_back(link);
    }

    prefab.eventGraphJSON = root.dump(2);
    return true;
  }

  bool seedDefaultsForNewPrefab(
    const std::string &projectPath,
    Prefab &prefab,
    const std::string &prefabFileName)
  {
    seedLifecycleFunctions(projectPath, prefabFileName);
    return seedDefaultEventGraph(prefab);
  }

  std::vector<UnknownFuncRef> findUnknownFuncRefs(
    const std::string &projectPath,
    const std::string &prefabFileName,
    const Prefab &prefab)
  {
    std::vector<UnknownFuncRef> out;
    nlohmann::json graph;
    if (!parseGraphJSON(prefab.eventGraphJSON, graph)) return out;

    auto known = scanPrefabFunctions(projectPath, prefabFileName);
    std::unordered_set<std::string> knownNames;
    knownNames.reserve(known.size());
    for (const auto &f : known) knownNames.insert(f.name);

    for (const auto &n : graph["nodes"]) {
      uint32_t type = n.value("type", 0u);
      if (type != Graph::TYPE_PREFAB_FUNC) continue;
      std::string fn = n.value("funcName", "");
      if (fn.empty()) continue;
      if (knownNames.contains(fn)) continue;
      out.push_back({fn, n.value("uuid", uint64_t{0})});
    }
    return out;
  }

  std::vector<UnknownVarRef> findUnknownVarRefs(const Prefab &prefab)
  {
    std::vector<UnknownVarRef> out;
    nlohmann::json graph;
    if (!parseGraphJSON(prefab.eventGraphJSON, graph)) return out;

    std::unordered_set<uint64_t> knownUUIDs;
    for (const auto &v : prefab.variables) knownUUIDs.insert(v.uuid);

    for (const auto &n : graph["nodes"]) {
      uint32_t type = n.value("type", 0u);
      if (type != Graph::TYPE_PREFAB_VAR_GET) continue;
      uint64_t uuid = n.value("varUUID", uint64_t{0});
      // Unbound nodes (uuid == 0) are user-in-progress, not stale references.
      if (uuid == 0) continue;
      if (knownUUIDs.contains(uuid)) continue;
      UnknownVarRef r;
      r.varName  = n.value("varName", "");
      r.varKind  = n.value("varKind", uint8_t{0});
      r.varUUID  = uuid;
      r.nodeUUID = n.value("uuid", uint64_t{0});
      out.push_back(r);
    }
    return out;
  }

  std::vector<std::string> autofixFunctions(
    const std::string &projectPath,
    const std::string &prefabFileName,
    const std::vector<UnknownFuncRef> &refs)
  {
    std::vector<std::string> added;
    std::unordered_set<std::string> done;
    for (const auto &r : refs) {
      if (r.funcName.empty() || done.contains(r.funcName)) continue;
      if (addPrefabFunction(projectPath, prefabFileName, r.funcName)) {
        added.push_back(r.funcName);
        done.insert(r.funcName);
      }
    }
    return added;
  }

  std::vector<std::string> autofixVariables(
    Prefab &prefab,
    const std::vector<UnknownVarRef> &refs)
  {
    std::vector<std::string> added;
    std::unordered_set<uint64_t> done;
    for (const auto &r : refs) {
      if (r.varUUID == 0 || done.contains(r.varUUID)) continue;
      // Synthesise a placeholder name when the saved node didn't carry one.
      // The user can rename it later from the Variables panel; what matters
      // is that the UUID slot exists so codegen has something to bind.
      PrefabVarDef v{};
      v.uuid = r.varUUID;
      v.name = r.varName.empty()
        ? ("var_" + Utils::toHex64(r.varUUID))
        : r.varName;
      v.kind = static_cast<VarKind>(r.varKind);
      prefab.variables.push_back(v);
      added.push_back(v.name);
      done.insert(r.varUUID);
    }
    return added;
  }
}
