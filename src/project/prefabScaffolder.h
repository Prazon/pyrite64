#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "graph/nodes/nodePrefabEvent.h"

namespace Project {
  class Prefab;
}

namespace Project::PrefabScaffolder
{
  // Unqualified `Prefab` etc. inside this nested namespace resolves to
  // ::Project::* via enclosing-namespace lookup. We deliberately avoid the
  // fully-qualified `Project::Prefab` spelling — `Project` would otherwise
  // be ambiguous with the `class Project` symbol that lives in the same
  // namespace.

  // The canonical set of lifecycle events scaffolded into a fresh prefab.
  // Matches the seeds previously hardcoded in prefabEventGraphEditor's ctor,
  // plus the matching User::<Ident>::<funcName> stub names emitted into
  // src/user/<Prefab>.{h,cpp}. Indices are intentionally aligned: the i'th
  // PrefabEvent node is wired to the i'th PrefabFunc node.
  struct LifecycleEvent {
    Graph::Node::PrefabEvent::Kind kind;
    const char* funcName;   // bare C++ identifier (no namespace, no parens)
  };
  std::span<const LifecycleEvent> lifecycleEvents();

  // Append a P64_NODE stub to <projectPath>/src/user/<prefabFileName>.{h,cpp}
  // for each lifecycle function that isn't already declared. The .h/.cpp
  // pair is created by ensurePrefabUserSource() first if missing. Idempotent:
  // names already present in scanPrefabFunctions() output are skipped.
  // Returns the function names that were actually added.
  std::vector<std::string> seedLifecycleFunctions(
    const std::string &projectPath,
    const std::string &prefabFileName
  );

  // If `prefab.eventGraphJSON` is empty (or has no nodes), install the
  // default lifecycle PrefabEvent nodes plus one PrefabFunc per lifecycle
  // entry, exec-wired together. Writes the new JSON back into the field.
  // Returns true if it mutated the prefab.
  bool seedDefaultEventGraph(Prefab &prefab);

  // Convenience wrapper that runs both seedLifecycleFunctions() and
  // seedDefaultEventGraph() against a freshly created prefab. Caller is
  // responsible for persisting the prefab afterwards if the event graph
  // changed (use the boolean return).
  bool seedDefaultsForNewPrefab(
    const std::string &projectPath,
    Prefab &prefab,
    const std::string &prefabFileName
  );

  // ── Tier 2: author-time backfill ────────────────────────────────────

  // A PrefabFunc node whose funcName isn't backed by any P64_NODE stub.
  struct UnknownFuncRef {
    std::string funcName;
    uint64_t    nodeUUID;
  };
  // Scan the prefab's eventGraphJSON for PrefabFunc nodes that reference
  // a function name not present in scanPrefabFunctions(). Empty funcNames
  // are skipped — those are unbound nodes the user hasn't filled in yet.
  std::vector<UnknownFuncRef> findUnknownFuncRefs(
    const std::string &projectPath,
    const std::string &prefabFileName,
    const Prefab &prefab
  );

  // A PrefabVarGet node whose varUUID isn't backed by any PrefabVarDef.
  struct UnknownVarRef {
    std::string varName;
    uint8_t     varKind{0};
    uint64_t    varUUID{0};
    uint64_t    nodeUUID{0};
  };
  std::vector<UnknownVarRef> findUnknownVarRefs(const Prefab &prefab);

  // Append a stub for every UnknownFuncRef returned by findUnknownFuncRefs.
  // Names with empty strings are skipped. Returns the names that were
  // actually written.
  std::vector<std::string> autofixFunctions(
    const std::string &projectPath,
    const std::string &prefabFileName,
    const std::vector<UnknownFuncRef> &refs
  );

  // Append a PrefabVarDef for every UnknownVarRef. The default value is
  // left zero-initialised; the user can edit it from the Variables panel
  // later. Returns the names that were actually written.
  std::vector<std::string> autofixVariables(
    Prefab &prefab,
    const std::vector<UnknownVarRef> &refs
  );
}
