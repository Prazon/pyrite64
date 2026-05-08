#pragma once
#include <string>
#include <vector>

namespace Project
{
  // One P64_NODE-tagged function discovered in a per-prefab user header.
  // The fields are raw strings rather than parsed types because the editor
  // displays them and the runtime dispatch table is generated downstream
  // (Phase 3) — the scanner doesn't need a typed AST yet.
  struct PrefabFunctionDesc
  {
    std::string name{};        // function identifier
    std::string returnType{};  // raw return type text
    std::string params{};      // raw parameter list text (may be empty)
    int line{0};               // source line for jump-to-source
  };

  // Locate <project>/src/user/<prefabName>.h and return every function in
  // it that's tagged with P64_NODE. The match is intentionally lenient — a
  // P64_NODE-prefixed declaration anywhere in the file is picked up — so the
  // user can lay out their header however they like. Comments are stripped
  // from the search to avoid false positives on `// P64_NODE ...` examples.
  std::vector<PrefabFunctionDesc> scanPrefabFunctions(
    const std::string &projectPath,
    const std::string &prefabName
  );
}
