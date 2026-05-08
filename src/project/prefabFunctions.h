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

  // Map a prefab/file name to a C++ identifier suffix (used as the user
  // namespace name and as the basis for sanitized variable struct names).
  // Replaces non-[A-Za-z0-9_] with '_'; prefixes a leading digit with '_'.
  std::string sanitizePrefabIdent(std::string s);

  // Ensure the per-prefab user source pair exists at
  // <projectPath>/src/user/<prefabName>.{h,cpp}. Idempotent — creates the
  // files with the standard scaffold (matching what +Add Function uses) only
  // when missing. Called at prefab-creation time so the Code panel in the
  // prefab editor always has files to list.
  void ensurePrefabUserSource(
    const std::string &projectPath,
    const std::string &prefabName
  );

  // Append a new P64_NODE function declaration to the prefab's user header
  // and a matching empty implementation to the .cpp. Creates the file pair
  // if it doesn't exist yet (matches the scaffold AssetsBrowser uses for
  // freshly-created prefabs). Returns true on success.
  //
  // The name passed in is the bare function name; the inserted signature is
  // `P64_NODE void <name>(P64::Object* self)` — a sensible default for
  // event-style entry points. Users edit the signature later in their own
  // text editor; the scanner picks up whatever they leave behind.
  bool addPrefabFunction(
    const std::string &projectPath,
    const std::string &prefabName,
    const std::string &functionName
  );

  // Rename a P64_NODE function in src/user/<prefabName>.{h,cpp}: replaces
  // every occurrence of the old function-name token (followed by `(`) with
  // the new name. The match deliberately requires a `(` after the name so
  // we don't touch unrelated identifier prefixes. Returns true on success.
  bool renamePrefabFunction(
    const std::string &projectPath,
    const std::string &prefabName,
    const std::string &oldName,
    const std::string &newName
  );

  // Remove a P64_NODE function from both files. Strips the line containing
  // `P64_NODE ... <name>(...)` from the .h and the matching definition
  // (function body up to the matching `}`) from the .cpp. Conservative:
  // only acts when the .cpp body is well-bracketed; otherwise leaves the
  // file untouched and returns false.
  bool removePrefabFunction(
    const std::string &projectPath,
    const std::string &prefabName,
    const std::string &functionName
  );
}
