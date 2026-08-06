/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "projectBuilder.h"
#include "../utils/string.h"
#include "../utils/fs.h"
#include "../utils/logger.h"
#include "../utils/proc.h"
#include "../context.h"
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "../project/graph/graph.h"
#include "../project/graph/nodeStyles.h"
#include "../project/graph/nodes/baseNode.h"
#include "../project/graph/nodes/nodePrefabEvent.h"
#include <functional>
#include <unordered_set>
#include "../project/prefabFunctions.h"

namespace fs = std::filesystem;

namespace
{
  // Sanitize a prefab name into a C identifier suffix used in struct names
  // and field names. Replaces non-[A-Za-z0-9_] with '_' and prefixes a digit
  // with an underscore so the result is always a legal identifier.
  std::string toIdent(std::string_view in)
  {
    std::string out;
    out.reserve(in.size() + 1);
    for (char c : in) {
      bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
             || (c >= '0' && c <= '9') || c == '_';
      out.push_back(ok ? c : '_');
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9')) {
      out.insert(out.begin(), '_');
    }
    return out;
  }

  std::string fmtFloat(float f) {
    char buf[64];
    if (std::isnan(f) || std::isinf(f)) std::snprintf(buf, sizeof(buf), "0.0f");
    else std::snprintf(buf, sizeof(buf), "%.7gf", static_cast<double>(f));
    return buf;
  }

  // Widgets share Prefab's on-disk structure (Project::Prefab + Project::Object)
  // and Asset->prefab is populated for both. Anything the build does per prefab
  // (vars header, event-graph dispatch, .pf binary write) applies identically
  // to widgets, so the loops below iterate this combined view.
  template<typename F>
  void forEachPrefabOrWidget(Project::Project &project, F&& f) {
    for (auto &e : project.getAssets().getTypeEntries(Project::FileType::PREFAB)) f(e);
    for (auto &e : project.getAssets().getTypeEntries(Project::FileType::WIDGET_BLUEPRINT)) f(e);
  }

  // Map a PrefabVarKind to the C++ type written into the generated POD struct.
  // VEC3/QUAT use libdragon's fm_* types so the runtime can pass the field
  // straight to math helpers without copying. Object/prefab/asset refs all
  // collapse to uint64_t — the runtime resolves the uuid at scene-load time.
  // ARRAY uses std::vector<E> where E is decoded from the def's typeArg
  // field (Pixic Step 6 path). The vector default-constructs empty.
  const char* kindToType(Project::PrefabVarKind k)
  {
    switch (k) {
      case Project::PrefabVarKind::INT:        return "int32_t";
      case Project::PrefabVarKind::FLOAT:      return "float";
      case Project::PrefabVarKind::BOOL:       return "bool";
      case Project::PrefabVarKind::VEC3:       return "fm_vec3_t";
      case Project::PrefabVarKind::QUAT:       return "fm_quat_t";
      case Project::PrefabVarKind::OBJECT_REF:
      case Project::PrefabVarKind::PREFAB_REF:
      case Project::PrefabVarKind::ASSET_REF:  return "uint64_t";
      case Project::PrefabVarKind::ARRAY:      return "void"; // see kindToTypeFull
    }
    return "uint32_t";
  }

  // Full type name resolver that handles ARRAY's element-kind encoded
  // in def.typeArg. Use this in places where the var def is in scope;
  // kindToType (kind only) is kept for back-compat with callers that
  // don't have the def handy. ARRAY in v1 supports scalar element kinds
  // only; nested arrays / structs are flagged in graph-gaps.md.
  std::string kindToTypeFull(const Project::PrefabVarDef &v)
  {
    if (v.kind != Project::PrefabVarKind::ARRAY) return kindToType(v.kind);
    auto elem = static_cast<Project::PrefabVarKind>(v.typeArg);
    return std::string{"std::vector<"} + kindToType(elem) + ">";
  }

  // Format the default-value initializer for a variable. Mirrors kindToType
  // exactly — must stay in sync. Pulls values out of GenericValue with the
  // matching get<T>() overload; missing/wrong-typed values fall back to zero.
  std::string formatDefault(const Project::PrefabVarDef &v)
  {
    switch (v.kind) {
      case Project::PrefabVarKind::INT:
        return std::to_string(v.defaultValue.get<int32_t>());
      case Project::PrefabVarKind::FLOAT:
        return fmtFloat(v.defaultValue.get<float>());
      case Project::PrefabVarKind::BOOL:
        return v.defaultValue.get<bool>() ? "true" : "false";
      case Project::PrefabVarKind::VEC3: {
        auto vec = v.defaultValue.get<glm::vec3>();
        return "{{" + fmtFloat(vec.x) + ", " + fmtFloat(vec.y) + ", " + fmtFloat(vec.z) + "}}";
      }
      case Project::PrefabVarKind::QUAT: {
        auto q = v.defaultValue.get<glm::quat>();
        return "{{" + fmtFloat(q.x) + ", " + fmtFloat(q.y)
             + ", " + fmtFloat(q.z) + ", " + fmtFloat(q.w) + "}}";
      }
      case Project::PrefabVarKind::OBJECT_REF:
      case Project::PrefabVarKind::PREFAB_REF:
      case Project::PrefabVarKind::ASSET_REF:
        return std::to_string(v.defaultValue.get<uint64_t>()) + "ull";
      case Project::PrefabVarKind::ARRAY:
        // Default-construct an empty vector. The element type is
        // already encoded in the field's declared type via
        // kindToTypeFull, so {} suffices for value-init.
        return "{}";
    }
    return "0";
  }

  // Generate the consolidated prefabVars.h header. One struct per prefab
  // or widget blueprint, gated on the asset having any class variables
  // (we don't emit empty structs — they would just clutter user-facing
  // autocompletion).
  std::string buildPrefabVarsHeader(Project::Project &project)
  {
    std::string out;
    out += "// auto-generated by pyrite64. Do not edit.\n";
    out += "#pragma once\n";
    out += "#include <cstdint>\n";
    out += "#include <vector>\n"; // ARRAY var support
    out += "#include <libdragon.h>\n\n";
    out += "namespace P64::Prefab {\n\n";

    // Walk parent chain to find an emittable Vars_<Parent> base — only
    // present when the parent prefab itself declared at least one var.
    // We need this lookup because a variant inherits its parent's struct
    // even when the variant added no new variables of its own.
    auto parentVarsBase = [&](const Project::AssetManagerEntry &a) -> std::string {
      if (!a.prefab || !a.prefab->isVariant()) return "";
      auto pp = project.getAssets().getPrefabByUUID(a.prefab->uuidParentPrefab.value);
      if (!pp || pp->variables.empty()) return "";
      if (auto *e = project.getAssets().getEntryByUUID(a.prefab->uuidParentPrefab.value)) {
        return "Vars_" + toIdent(e->name);
      }
      return "";
    };

    bool wroteAny = false;
    forEachPrefabOrWidget(project, [&](const Project::AssetManagerEntry &asset) {
      if (asset.conf.exclude || !asset.prefab) return;
      const auto &vars = asset.prefab->variables;
      const std::string baseStruct = parentVarsBase(asset);
      const bool emitForInheritance = !baseStruct.empty();
      if (vars.empty() && !emitForInheritance) return;

      const std::string ident = toIdent(asset.name);
      // Variants subclass their parent's struct so the child sees inherited
      // fields via member access — UE Blueprint's "variables visible from
      // parent" model implemented as plain C++ inheritance.
      if (!baseStruct.empty()) {
        out += "struct Vars_" + ident + " : public " + baseStruct + " {\n";
      } else {
        out += "struct Vars_" + ident + " {\n";
      }
      // Only the *newly-introduced* variables emit as struct members on a
      // variant; inherited fields come from the base. rebuildOverridesFromCurrent
      // (Phase 1) ensures the variant's vars list is the union — we filter
      // back down to the additions here for the struct shape.
      if (!baseStruct.empty()) {
        auto pp = project.getAssets().getPrefabByUUID(asset.prefab->uuidParentPrefab.value);
        std::unordered_set<uint64_t> parentUuids;
        if (pp) for (const auto &pv : pp->variables) parentUuids.insert(pv.uuid);
        for (const auto &v : vars) {
          if (parentUuids.contains(v.uuid)) continue;
          out += "  " + kindToTypeFull(v)
               + " " + toIdent(v.name) + " = " + formatDefault(v) + ";\n";
        }
      } else {
        for (const auto &v : vars) {
          out += "  " + kindToTypeFull(v)
               + " " + toIdent(v.name) + " = " + formatDefault(v) + ";\n";
        }
      }
      out += "  // Stable variable uuids — kept in sync with the prefab asset.\n";
      out += "  // Use these when looking up overrides at scene-load time.\n";
      for (const auto &v : vars) {
        out += "  static constexpr uint64_t UUID_" + toIdent(v.name)
             + " = " + std::to_string(v.uuid) + "ull;\n";
      }
      out += "};\n\n";
      wroteAny = true;
    });

    if (!wroteAny) {
      out += "// (no prefabs declare class variables yet)\n";
    }
    out += "} // namespace P64::Prefab\n";
    return out;
  }

  // Compute outgoing and ingoing-value link maps for a graph, mirroring
  // the topology pass Graph::build() does. Outgoing: node-uuid → vector
  // of next-node-uuids indexed by output-pin position; index 0 is the
  // primary exec out. Ingoing-vals: node-uuid → vector of source-node-
  // uuids indexed by input-pin position, then filtered through
  // valInputTypes so consumer nodes see one entry per declared value
  // input (matching graph.cpp's resolution rules for res_<uuid> lookups).
  struct LinkMaps {
    std::unordered_map<uint64_t, std::vector<uint64_t>> outgoing;
    std::unordered_map<uint64_t, std::vector<uint64_t>> ingoingVals;
  };

  LinkMaps collectLinks(Project::Graph::Graph &graph,
                        const std::unordered_map<uint64_t, Project::Graph::Node::Base*> &nodeMap)
  {
    LinkMaps maps;
    for (const auto &weak : graph.graph.getLinks()) {
      auto link = weak.lock();
      if (!link) continue;
      auto leftPin = link->left();
      auto rightPin = link->right();
      if (!leftPin || !rightPin) continue;

      auto leftNode  = dynamic_cast<Project::Graph::Node::Base*>(leftPin->getParent());
      auto rightNode = dynamic_cast<Project::Graph::Node::Base*>(rightPin->getParent());
      if (!leftNode || !rightNode) continue;

      uint32_t leftIdx = 0;
      auto &outs = leftNode->getOuts();
      for (size_t i = 0; i < outs.size(); ++i) {
        if (outs[i].get() == leftPin) { leftIdx = static_cast<uint32_t>(i); break; }
      }
      auto &slots = maps.outgoing[leftNode->uuid];
      if (leftIdx >= slots.size()) slots.resize(leftIdx + 1, 0);
      slots[leftIdx] = rightNode->uuid;

      uint32_t rightIdx = 0;
      auto &ins = rightNode->getIns();
      for (size_t i = 0; i < ins.size(); ++i) {
        if (ins[i].get() == rightPin) { rightIdx = static_cast<uint32_t>(i); break; }
      }
      auto &inSlots = maps.ingoingVals[rightNode->uuid];
      if (rightIdx >= inSlots.size()) inSlots.resize(rightIdx + 1, 0);
      inSlots[rightIdx] = leftNode->uuid;
    }

    // Filter ingoingVals through each consumer's valInputTypes so the
    // resulting vector indexes by *value-input slot*, not by raw pin
    // position. This mirrors graph.cpp:595-609.
    for (auto &[nodeUUID, ingoing] : maps.ingoingVals) {
      if (ingoing.empty()) continue;
      auto it = nodeMap.find(nodeUUID);
      if (it == nodeMap.end()) continue;
      auto* p64 = it->second;
      if (p64->valInputTypes.empty()) continue;
      std::vector<uint64_t> filtered;
      for (size_t i = 0; i < p64->valInputTypes.size(); ++i) {
        if (p64->valInputTypes[i] == 1 && i < ingoing.size()) {
          filtered.push_back(ingoing[i]);
        }
      }
      ingoing = filtered;
    }
    return maps;
  }

  // Per-prefab dispatch generator. Walks the graph, emits a labeled block
  // per non-event node, plus a switch at the function head that gotos the
  // matching event entry. PrefabFunc nodes' {{PFX}} placeholder is
  // replaced with the prefab's sanitized identifier so calls land in
  // namespace User::<Ident>::*. Loop-shaped nodes (ForRange/While/ForEach)
  // are inlined: their body subgraph emits inside a real C++ for/while
  // block via buildLoopHeader/buildLoopFooter, and Break/Continue map
  // directly onto the C++ keywords. See graph.cpp's parallel pass.
  std::string buildPrefabDispatchBody(
    Project::Graph::Graph &graph, const std::string &ident,
    const std::string &parentIdent = "")
  {
    // Collect nodes + entries, build nodeMap for link filtering.
    std::vector<std::pair<uint16_t, uint64_t>> entries;
    std::vector<Project::Graph::Node::Base*> allNodes;
    std::unordered_map<uint64_t, Project::Graph::Node::Base*> nodeMap;
    allNodes.reserve(graph.graph.getNodes().size());
    for (auto &kv : graph.graph.getNodes()) {
      auto p64 = dynamic_cast<Project::Graph::Node::Base*>(kv.second.get());
      if (!p64) continue;
      allNodes.push_back(p64);
      nodeMap[p64->uuid] = p64;
      if (auto evt = dynamic_cast<Project::Graph::Node::PrefabEvent*>(p64)) {
        entries.push_back({Project::Graph::Node::PrefabEvent::kindEventId(evt->kind), evt->uuid});
      }
    }

    auto links = collectLinks(graph, nodeMap);
    auto &outgoing = links.outgoing;
    auto &ingoingVals = links.ingoingVals;

    // The dispatch switch goes after the global var declarations
    // because the case-arm gotos jump into NODE_<uuid> labels and
    // C++ forbids transferring control past a non-trivially-init'd
    // variable (e.g. std::vector). Build the switch text up front
    // but splice it in after the global-var preamble.
    std::string switchText;
    switchText += "  switch(eventType) {\n";
    for (const auto &[evtId, nodeUUID] : entries) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "    case 0x%04X: goto NODE_%016llX;\n",
        (unsigned)evtId, (unsigned long long)nodeUUID);
      switchText += buf;
    }
    // Blueprint-style "inherit unhandled events": if this prefab is a
    // variant and the parent has its own dispatcher, route any event not
    // explicitly handled here to the parent's dispatch.
    if (!parentIdent.empty()) {
      switchText += "    default: dispatch_" + parentIdent
                  + "(self, eventType, deltaTime); return;\n";
    } else {
      switchText += "    default: return;\n";
    }
    switchText += "  }\n";

    // Count exec outputs by leading-style match. Exec pins always come
    // before value pins in the node ctors, so as soon as a non-exec
    // output appears the count is final.
    auto execOutCount = [&](Project::Graph::Node::Base* n) -> uint32_t {
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
    // outUUIDs[0] (Body), stopping at nested loop nodes which keep
    // their own bodies. First-come-wins so the outermost loop owns
    // shared nodes.
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
        auto outIt = outgoing.find(n->uuid);
        if (outIt == outgoing.end()) return;
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
      for (auto* loop : allNodes) {
        if (!loop->isLoop()) continue;
        auto outIt = outgoing.find(loop->uuid);
        if (outIt == outgoing.end() || outIt->second.empty()) continue;
        std::unordered_set<uint64_t> visited;
        visited.insert(loop->uuid);
        markBody(outIt->second[0], loop->uuid, visited);
      }
    }

    Project::Graph::BuildCtx nctx;
    nctx.source = "";
    static thread_local std::vector<uint64_t> emptyVec;

    std::function<void(Project::Graph::Node::Base*, bool)> emitNode;
    emitNode = [&](Project::Graph::Node::Base* node, bool insideLoop) {
      auto savedOut  = nctx.outUUIDs;
      auto savedIn   = nctx.inValUUIDs;
      auto savedFlag = nctx.insideLoopBody;
      auto outIt = outgoing.find(node->uuid);
      auto inIt  = ingoingVals.find(node->uuid);
      nctx.outUUIDs    = outIt != outgoing.end() ? &outIt->second : &emptyVec;
      nctx.inValUUIDs  = inIt  != ingoingVals.end() ? &inIt->second : &emptyVec;
      nctx.insideLoopBody = insideLoop;

      char lbl[48];
      std::snprintf(lbl, sizeof(lbl), "NODE_%016llX",
        (unsigned long long)node->uuid);
      nctx.source += std::string{"  "} + lbl + ": // " + node->getName() + "\n";
      nctx.source += "  {\n";

      if (node->isLoop()) {
        node->build(nctx);
        node->buildLoopHeader(nctx);
        for (auto* body : allNodes) {
          auto it = loopOwner.find(body->uuid);
          if (it == loopOwner.end() || it->second != node->uuid) continue;
          emitNode(body, true);
        }
        node->buildLoopFooter(nctx);
        nctx.outUUIDs = outIt != outgoing.end() ? &outIt->second : &emptyVec;
        if (nctx.outUUIDs->size() > 1 && nctx.outUUIDs->at(1)) {
          char gbuf[48];
          std::snprintf(gbuf, sizeof(gbuf), "goto NODE_%016llX;",
            (unsigned long long)nctx.outUUIDs->at(1));
          nctx.line(gbuf);
        } else if (insideLoop) {
          nctx.line("continue;");
        } else {
          nctx.line("return;");
        }
      } else {
        node->build(nctx);
        if (nctx.outUUIDs->empty() || nctx.outUUIDs->at(0) == 0) {
          nctx.line(insideLoop ? "continue;" : "return;");
        } else {
          nctx.jump(0);
        }
      }

      nctx.source += "  }\n";
      nctx.outUUIDs       = savedOut;
      nctx.inValUUIDs     = savedIn;
      nctx.insideLoopBody = savedFlag;
    };

    // Pure-eval pre-pass: nodes opted in via canBePure() with no
    // incoming exec edge are emitted at function-top in topological-
    // dependency order via buildAsPure(). Their globalVar inits
    // inline the expression, so downstream consumers see a valid
    // value without needing an explicit exec wire through the math.
    // Mirrors graph.cpp's parallel pass.
    std::unordered_set<uint64_t> emittedPure;
    {
      auto execStyle2 = ::Project::Graph::pinStyle(::Project::Graph::PinDataType::Exec).get();
      std::unordered_set<uint64_t> hasIncomingExec;
      for (const auto &weak : graph.graph.getLinks()) {
        auto link = weak.lock();
        if (!link) continue;
        auto rightPin = link->right();
        if (!rightPin) continue;
        if (rightPin->getStyle().get() != execStyle2) continue;
        auto rightNode = dynamic_cast<Project::Graph::Node::Base*>(rightPin->getParent());
        if (rightNode) hasIncomingExec.insert(rightNode->uuid);
      }

      // inProgress catches cyclic value-dependency chains. On
      // detection, push a static_assert-bearing globalVar that
      // names the cycle's node so the host compiler nukes the
      // dispatch function with a clear diagnostic.
      std::unordered_set<uint64_t> inProgress;
      std::function<void(Project::Graph::Node::Base*)> emitPure;
      emitPure = [&](Project::Graph::Node::Base *n) {
        if (emittedPure.count(n->uuid)) return;
        if (inProgress.count(n->uuid)) {
          nctx.vars.push_back({"[[maybe_unused]] static constexpr bool",
            "p64_cyclic_pure_" + Utils::toHex64(n->uuid),
            "([] { static_assert(false, \"Cyclic pure-value dependency at node "
              + n->getName() + " (" + Utils::toHex64(n->uuid)
              + ")\"); return false; }())"});
          emittedPure.insert(n->uuid);
          return;
        }
        inProgress.insert(n->uuid);
        auto inIt = ingoingVals.find(n->uuid);
        if (inIt != ingoingVals.end()) {
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
        auto savedOut = nctx.outUUIDs;
        auto savedIn  = nctx.inValUUIDs;
        auto outIt = outgoing.find(n->uuid);
        auto inIt2 = ingoingVals.find(n->uuid);
        nctx.outUUIDs   = outIt != outgoing.end() ? &outIt->second : &emptyVec;
        nctx.inValUUIDs = inIt2 != ingoingVals.end() ? &inIt2->second : &emptyVec;
        n->buildAsPure(nctx);
        nctx.outUUIDs   = savedOut;
        nctx.inValUUIDs = savedIn;
        inProgress.erase(n->uuid);
        emittedPure.insert(n->uuid);
      };

      for (auto* n : allNodes) {
        if (!n->canBePure()) continue;
        if (hasIncomingExec.count(n->uuid)) continue;
        emitPure(n);
      }
    }

    // Walk every top-level node (loop bodies are inlined recursively;
    // pure-emitted nodes are skipped — they live entirely in vars).
    for (auto* n : allNodes) {
      if (loopOwner.count(n->uuid)) continue;
      if (emittedPure.count(n->uuid)) continue;
      emitNode(n, false);
    }

    // Layout: globalVars → switch → NODE labels. Globalvars must be
    // declared before the switch so the case-arm gotos do not jump
    // past their initialisers (illegal for non-trivial inits like
    // std::vector).
    std::string body;
    for (auto &gv : nctx.vars) {
      body += "  " + gv.type + " " + gv.name + " = " + gv.value + ";\n";
    }
    body += switchText;
    body += nctx.source;

    body = Utils::replaceAll(body, "{{PFX}}", ident);
    // Substitute the PrefabSuper node's parent-dispatch placeholder. When
    // the prefab has no parent, drop the line entirely — leaving the call
    // expression in place would refer to a non-existent function.
    if (!parentIdent.empty()) {
      body = Utils::replaceAll(body, "{{PARENT_DISPATCH}}", "dispatch_" + parentIdent);
    } else {
      // Strip the whole line so Super:: nodes on a non-variant compile out.
      std::string filtered;
      filtered.reserve(body.size());
      size_t i = 0;
      while (i < body.size()) {
        size_t nl = body.find('\n', i);
        std::string line = body.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        if (line.find("{{PARENT_DISPATCH}}") == std::string::npos) {
          filtered += line;
          if (nl != std::string::npos) filtered += '\n';
        }
        if (nl == std::string::npos) break;
        i = nl + 1;
      }
      body = std::move(filtered);
    }
    return body;
  }

  // Build the consolidated prefabEvents.{cpp,h} pair. cpp holds the per-
  // prefab dispatch functions plus a registry; h exposes a single entry
  // point used by the runtime.
  void buildPrefabEventGraphs(Project::Project &project)
  {
    auto p64Dir = fs::path{project.getPath()} / "src" / "p64";
    fs::create_directories(p64Dir);

    std::string cpp;
    cpp += "// auto-generated by pyrite64. Do not edit.\n";
    cpp += "#include \"prefabEvents.h\"\n";
    cpp += "#include \"prefabVars.h\"\n";
    cpp += "#include <scene/object.h>\n";
    cpp += "#include <cstdint>\n";
    cpp += "#include <math.h>\n";
    cpp += "#include <string>\n";
    cpp += "#include <cstdio>\n";
    cpp += "#include <vector>\n";
    cpp += "#include <type_traits>\n\n"; // array element-kind inference via decltype

    // Surface the user-namespace headers for every prefab/widget that has
    // a user .h on disk — needed so PrefabFunc-emitted calls into
    // User::<X>::* resolve. Missing files are fine (the codegen produces
    // no calls).
    forEachPrefabOrWidget(project, [&](const Project::AssetManagerEntry &asset) {
      if (asset.conf.exclude || !asset.prefab) return;
      fs::path userH = fs::path{project.getPath()} / "src" / "user" / (asset.name + ".h");
      std::error_code ec;
      if (fs::exists(userH, ec)) {
        cpp += "#include \"../user/" + asset.name + ".h\"\n";
      }
    });
    cpp += "\nnamespace P64::PrefabEvents { namespace {\n\n";

    // Look up the parent prefab's asset name for variants so we can emit
    // qualified `dispatch_<ParentIdent>` calls. Empty when the prefab is
    // standalone or its parent has been deleted.
    auto parentIdentOf = [&](const Project::AssetManagerEntry &a) -> std::string {
      if (!a.prefab || !a.prefab->isVariant()) return "";
      auto pp = project.getAssets().getPrefabByUUID(a.prefab->uuidParentPrefab.value);
      if (!pp) return "";
      // Look up the asset entry that owns the resolved parent prefab so we
      // can emit qualified dispatch_<ParentIdent> calls.
      if (auto *e = project.getAssets().getEntryByUUID(a.prefab->uuidParentPrefab.value)) {
        return toIdent(e->name);
      }
      return "";
    };

    // Forward-declare every prefab dispatcher up front. Variant dispatchers
    // call into their parent's, and prefabs aren't necessarily emitted in
    // dependency order; a forward decl keeps the linker happy without a
    // topological sort.
    forEachPrefabOrWidget(project, [&](const Project::AssetManagerEntry &asset) {
      if (asset.conf.exclude || !asset.prefab) return;
      cpp += "void dispatch_" + toIdent(asset.name)
           + "(P64::Object* self, uint16_t eventType, float deltaTime);\n";
    });
    cpp += "\n";

    // Per-prefab/widget dispatch functions.
    std::vector<std::pair<uint32_t, std::string>> registry; // (prefabUUID, fnName)
    forEachPrefabOrWidget(project, [&](const Project::AssetManagerEntry &asset) {
      if (asset.conf.exclude || !asset.prefab) return;

      const std::string ident = toIdent(asset.name);
      const uint32_t prefabUUID = asset.prefab->uuid.value;
      const std::string fnName = "dispatch_" + ident;
      const std::string parentIdent = parentIdentOf(asset);

      // Variants with no event graph of their own still need a dispatcher
      // so the runtime can route to them; that dispatcher unconditionally
      // forwards to the parent.
      if (asset.prefab->eventGraphJSON.empty()) {
        if (parentIdent.empty()) return;
        registry.push_back({prefabUUID, fnName});
        cpp += "void " + fnName + "(P64::Object* self, uint16_t eventType, float deltaTime)\n{\n";
        cpp += "  dispatch_" + parentIdent + "(self, eventType, deltaTime);\n";
        cpp += "}\n\n";
        return;
      }

      // Materialize a Graph from the saved JSON. Free-standing here — the
      // editor's live ImNodeFlow graphs are not reachable from the build
      // pass (which can run in CLI mode without UI).
      Project::Graph::Graph live;
      if (!live.deserialize(asset.prefab->eventGraphJSON)) return;

      // Run the same structural validator the standalone NODE_GRAPH path uses
      // so prefab event graphs surface errors in the Compile Errors panel
      // identically. The asset UUID is the prefab's so double-click opens
      // the prefab event graph editor.
      live.validate(&ctx.compileErrors, asset.getUUID());

      registry.push_back({prefabUUID, fnName});

      cpp += "void " + fnName + "(P64::Object* self, uint16_t eventType, float deltaTime)\n{\n";
      cpp += "  (void)deltaTime; // referenced by OnTick paths only\n";
      std::string body = buildPrefabDispatchBody(live, ident, parentIdent);

      // PrefabFunc nodes emit `User::<Ident>::funcName(self);` by default
      // (see nodePrefabFunc.h). For functions whose declared signature also
      // takes a `float` (the OnTick deltaSeconds convention), forward
      // deltaTime so the generated call type-checks.
      auto descs = ::Project::scanPrefabFunctions(project.getPath(), asset.name);
      for (const auto &d : descs) {
        if (d.params.find("float") == std::string::npos) continue;
        const std::string from = "User::" + ident + "::" + d.name + "(self);";
        const std::string to   = "User::" + ident + "::" + d.name + "(self, deltaTime);";
        body = Utils::replaceAll(body, from, to);
      }
      cpp += body;
      cpp += "}\n\n";
    });

    cpp += "} // anon\n\n";
    cpp += "void dispatch(P64::Object* self, uint32_t prefabUUID, uint16_t eventType, float deltaTime)\n{\n";
    cpp += "  if (!self) return;\n";
    cpp += "  switch(prefabUUID) {\n";
    for (const auto &[uuid, fn] : registry) {
      char buf[112];
      std::snprintf(buf, sizeof(buf), "    case 0x%08Xu: %s(self, eventType, deltaTime); return;\n",
        (unsigned)uuid, fn.c_str());
      cpp += buf;
    }
    cpp += "    default: return;\n";
    cpp += "  }\n";
    cpp += "}\n\n";
    cpp += "} // namespace P64::PrefabEvents\n";

    Utils::FS::saveTextFile(p64Dir / "prefabEvents.cpp", cpp);

    std::string h;
    h += "// auto-generated by pyrite64. Do not edit.\n";
    h += "#pragma once\n";
    h += "#include <cstdint>\n\n";
    h += "namespace P64 { class Object; }\n\n";
    h += "namespace P64::PrefabEvents {\n";
    h += "  void dispatch(P64::Object* self, uint32_t prefabUUID, uint16_t eventType, float deltaTime);\n";
    h += "}\n";
    Utils::FS::saveTextFile(p64Dir / "prefabEvents.h", h);
  }
}

bool Build::buildPrefabAssets(Project::Project &project, SceneCtx &sceneCtx)
{
  forEachPrefabOrWidget(*sceneCtx.project, [&](const Project::AssetManagerEntry &asset) {
    if(asset.conf.exclude) return;

    auto projectPath = fs::path{project.getPath()};
    auto outPath = projectPath / asset.outPath;
    auto outDir = outPath.parent_path();
    fs::create_directories(outPath.parent_path());

    sceneCtx.files.push_back(Utils::FS::toUnixPath(asset.outPath));

    // @TODO: lazy-build again after refactoring the asset table building
    //if(!assetBuildNeeded(asset, outPath))continue;

    // A prefab asset is spawned at runtime, which has no prefab resolution or transform
    // hierarchy. So bake the whole tree flat and world-relative to the root, expanding any
    // nested prefabs, and prefix the object count so the runtime knows how many to load.
    sceneCtx.fileObj = {};
    sceneCtx.nextRuntimeId = 1; // the root is 0, expanded children continue from here
    sceneCtx.fileObj.write<uint32_t>(0); // object count, patched below
    uint32_t count = writeObject(sceneCtx, asset.prefab->obj, false, 0, 0, true, {}, true);
    sceneCtx.fileObj.atPos(0, [&]{ sceneCtx.fileObj.write<uint32_t>(count); });
    sceneCtx.fileObj.writeToFile(outPath);
    sceneCtx.fileObj = {};
  });

  // Emit the consolidated prefab-variables header. User src/user/<name>.cpp
  // files include this to read class-variable values via the typed structs.
  // The whole file regenerates every build — prefab variable lists are tiny,
  // so the cost of regenerating dominates the cost of dependency tracking.
  auto p64Dir = fs::path{project.getPath()} / "src" / "p64";
  fs::create_directories(p64Dir);
  Utils::FS::saveTextFile(p64Dir / "prefabVars.h", buildPrefabVarsHeader(project));

  // Generate per-prefab event-graph dispatch (Phase 3.3). Walks each
  // prefab's stored eventGraphJSON, emits a goto-based dispatch C++ file,
  // and registers each prefab UUID against its dispatch in a switch.
  buildPrefabEventGraphs(project);

  return true;
}