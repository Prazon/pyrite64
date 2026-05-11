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
#include "../project/graph/nodes/baseNode.h"
#include "../project/graph/nodes/nodePrefabEvent.h"

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

  // Generate the consolidated prefabVars.h header. One struct per prefab,
  // gated on the prefab having any class variables (we don't emit empty
  // structs — they would just clutter user-facing autocompletion).
  std::string buildPrefabVarsHeader(Project::Project &project)
  {
    auto &assets = project.getAssets().getTypeEntries(Project::FileType::PREFAB);

    std::string out;
    out += "// auto-generated by pyrite64. Do not edit.\n";
    out += "#pragma once\n";
    out += "#include <cstdint>\n";
    out += "#include <vector>\n"; // ARRAY var support
    out += "#include <libdragon.h>\n\n";
    out += "namespace P64::Prefab {\n\n";

    bool wroteAny = false;
    for (const auto &asset : assets) {
      if (asset.conf.exclude || !asset.prefab) continue;
      const auto &vars = asset.prefab->variables;
      if (vars.empty()) continue;

      const std::string ident = toIdent(asset.name);
      out += "struct Vars_" + ident + " {\n";
      for (const auto &v : vars) {
        out += "  " + kindToTypeFull(v)
             + " " + toIdent(v.name) + " = " + formatDefault(v) + ";\n";
      }
      out += "  // Stable variable uuids — kept in sync with the prefab asset.\n";
      out += "  // Use these when looking up overrides at scene-load time.\n";
      for (const auto &v : vars) {
        out += "  static constexpr uint64_t UUID_" + toIdent(v.name)
             + " = " + std::to_string(v.uuid) + "ull;\n";
      }
      out += "};\n\n";
      wroteAny = true;
    }

    if (!wroteAny) {
      out += "// (no prefabs declare class variables yet)\n";
    }
    out += "} // namespace P64::Prefab\n";
    return out;
  }

  // Compute outgoing-link maps for a graph, mirroring the topology pass
  // Graph::build() does. Returned map: node-uuid → vector of next-node-uuids
  // indexed by output-pin position. Index 0 is the primary "exec" out for
  // logic-flow nodes.
  std::unordered_map<uint64_t, std::vector<uint64_t>> collectOutgoing(
    Project::Graph::Graph &graph)
  {
    std::unordered_map<uint64_t, std::vector<uint64_t>> outgoing;
    for (const auto &weak : graph.graph.getLinks()) {
      auto link = weak.lock();
      if (!link) continue;
      auto leftPin = link->left();
      auto rightPin = link->right();
      if (!leftPin || !rightPin) continue;

      auto leftNode  = dynamic_cast<Project::Graph::Node::Base*>(leftPin->getParent());
      auto rightNode = dynamic_cast<Project::Graph::Node::Base*>(rightPin->getParent());
      if (!leftNode || !rightNode) continue;

      // Find pin index by scanning the parent's outs.
      uint32_t leftIdx = 0;
      auto &outs = leftNode->getOuts();
      for (size_t i = 0; i < outs.size(); ++i) {
        if (outs[i].get() == leftPin) { leftIdx = static_cast<uint32_t>(i); break; }
      }
      auto &slots = outgoing[leftNode->uuid];
      if (leftIdx >= slots.size()) slots.resize(leftIdx + 1, 0);
      slots[leftIdx] = rightNode->uuid;
    }
    return outgoing;
  }

  // Per-prefab dispatch generator. Walks the graph, emits a labeled block
  // per non-event node, plus a switch at the function head that gotos the
  // matching event entry. PrefabFunc nodes' {{PFX}} placeholder is
  // replaced with the prefab's sanitized identifier so calls land in
  // namespace User::<Ident>::*.
  std::string buildPrefabDispatchBody(
    Project::Graph::Graph &graph, const std::string &ident)
  {
    auto outgoing = collectOutgoing(graph);

    // Collect entry nodes (PrefabEvent) and other nodes.
    std::vector<std::pair<uint16_t, uint64_t>> entries; // (eventId, nodeUUID)
    std::vector<Project::Graph::Node::Base*> allNodes;
    allNodes.reserve(graph.graph.getNodes().size());
    for (auto &kv : graph.graph.getNodes()) {
      auto p64 = dynamic_cast<Project::Graph::Node::Base*>(kv.second.get());
      if (!p64) continue;
      allNodes.push_back(p64);
      if (auto evt = dynamic_cast<Project::Graph::Node::PrefabEvent*>(p64)) {
        entries.push_back({Project::Graph::Node::PrefabEvent::kindEventId(evt->kind), evt->uuid});
      }
    }

    std::string body;
    body += "  switch(eventType) {\n";
    for (const auto &[evtId, nodeUUID] : entries) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "    case 0x%04X: goto NODE_%016llX;\n",
        (unsigned)evtId, (unsigned long long)nodeUUID);
      body += buf;
    }
    body += "    default: return;\n";
    body += "  }\n";

    // Per-node code blocks.
    Project::Graph::BuildCtx nctx;
    nctx.source = "";
    for (auto *n : allNodes) {
      auto outIt = outgoing.find(n->uuid);
      static thread_local std::vector<uint64_t> empty;
      nctx.outUUIDs = outIt != outgoing.end() ? &outIt->second : &empty;

      char lbl[48];
      std::snprintf(lbl, sizeof(lbl), "NODE_%016llX",
        (unsigned long long)n->uuid);
      nctx.source += std::string{"  "} + lbl + ": // " + n->getName() + "\n";
      nctx.source += "  {\n";
      n->build(nctx);
      if (nctx.outUUIDs->empty()) {
        nctx.line("return;");
      } else {
        nctx.jump(0);
      }
      nctx.source += "  }\n";
    }

    body += nctx.source;

    // Replace PrefabFunc's namespace placeholder with the resolved ident.
    body = Utils::replaceAll(body, "{{PFX}}", ident);
    return body;
  }

  // Build the consolidated prefabEvents.{cpp,h} pair. cpp holds the per-
  // prefab dispatch functions plus a registry; h exposes a single entry
  // point used by the runtime.
  void buildPrefabEventGraphs(Project::Project &project)
  {
    auto p64Dir = fs::path{project.getPath()} / "src" / "p64";
    fs::create_directories(p64Dir);

    auto &assets = project.getAssets().getTypeEntries(Project::FileType::PREFAB);

    std::string cpp;
    cpp += "// auto-generated by pyrite64. Do not edit.\n";
    cpp += "#include \"prefabEvents.h\"\n";
    cpp += "#include \"prefabVars.h\"\n";
    cpp += "#include <scene/object.h>\n";
    cpp += "#include <cstdint>\n";
    cpp += "#include <math.h>\n";
    cpp += "#include <string>\n";
    cpp += "#include <cstdio>\n\n";

    // Surface the user-namespace headers for every prefab that has a user
    // .h on disk — needed so PrefabFunc-emitted calls into User::<X>::*
    // resolve. Missing files are fine (the codegen produces no calls).
    for (const auto &asset : assets) {
      if (asset.conf.exclude || !asset.prefab) continue;
      fs::path userH = fs::path{project.getPath()} / "src" / "user" / (asset.name + ".h");
      std::error_code ec;
      if (fs::exists(userH, ec)) {
        cpp += "#include \"../user/" + asset.name + ".h\"\n";
      }
    }
    cpp += "\nnamespace P64::PrefabEvents { namespace {\n\n";

    // Per-prefab dispatch functions.
    std::vector<std::pair<uint32_t, std::string>> registry; // (prefabUUID, fnName)
    for (const auto &asset : assets) {
      if (asset.conf.exclude || !asset.prefab) continue;
      if (asset.prefab->eventGraphJSON.empty()) continue;

      // Materialize a Graph from the saved JSON. Free-standing here — the
      // editor's live ImNodeFlow graphs are not reachable from the build
      // pass (which can run in CLI mode without UI).
      Project::Graph::Graph live;
      if (!live.deserialize(asset.prefab->eventGraphJSON)) continue;

      // Run the same structural validator the standalone NODE_GRAPH path uses
      // so prefab event graphs surface errors in the Compile Errors panel
      // identically. The asset UUID is the prefab's so double-click opens
      // the prefab event graph editor.
      live.validate(&ctx.compileErrors, asset.getUUID());

      const std::string ident = toIdent(asset.name);
      const uint32_t prefabUUID = asset.prefab->uuid.value;
      const std::string fnName = "dispatch_" + ident;
      registry.push_back({prefabUUID, fnName});

      cpp += "void " + fnName + "(P64::Object* self, uint16_t eventType, float deltaTime)\n{\n";
      cpp += "  (void)deltaTime; // referenced by OnTick paths only\n";
      cpp += buildPrefabDispatchBody(live, ident);
      cpp += "}\n\n";
    }

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
  auto &assets = sceneCtx.project->getAssets().getTypeEntries(Project::FileType::PREFAB);
  for (auto &asset : assets)
  {
    if(asset.conf.exclude)continue;

    auto projectPath = fs::path{project.getPath()};
    auto outPath = projectPath / asset.outPath;
    auto outDir = outPath.parent_path();
    fs::create_directories(outPath.parent_path());

    sceneCtx.files.push_back(Utils::FS::toUnixPath(asset.outPath));

    // @TODO: lazy-build again after refactoring the asset table building
    //if(!assetBuildNeeded(asset, outPath))continue;

    sceneCtx.fileObj = {};
    writeObject(sceneCtx, asset.prefab->obj, true);
    sceneCtx.fileObj.writeToFile(outPath);
    sceneCtx.fileObj = {};
  }

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