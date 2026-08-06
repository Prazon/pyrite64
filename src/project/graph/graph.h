/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <string>
#include <span>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "ImNodeFlow.h"
#pragma GCC diagnostic pop

#include "../../utils/binaryFile.h"
#include "nodes/baseNode.h"
#include "../../editor/nodePalette.h"

namespace Project::Compile { class ErrorList; }

namespace Project::Graph
{
  // Stable indices into NODE_TABLE in graph.cpp. Persisted in saved graphs
  // as the "type" field — never renumber, only append. Exposed so direct-
  // -spawn callers (e.g. PrefabEditor's drag-drop target on the event graph
  // canvas) don't have to brittly string-match against NAME constants.
  inline constexpr uint32_t TYPE_PREFAB_FUNC    = 14;
  inline constexpr uint32_t TYPE_PREFAB_VAR_GET = 15;
  // Sentinel numeric type for spec-driven ScriptNodes; their real identity is
  // the spec's string id (Base::typeId()). Never persisted as "type".
  inline constexpr uint32_t TYPE_SCRIPT_NODE    = 0xFFFFFFFF;
  // Palette-entry encoding: this bit plus an index into getNodeSpecs() spawns
  // a spec-driven node through addNode(uint32_t). Table indices never come
  // near this range.
  inline constexpr uint32_t SPEC_ENTRY_FLAG     = 0x40000000;

  // A graph-level variable declaration (name + value-type id, see
  // valueTypes.h). Stored per graph asset; the runtime keeps the values in a
  // per-instance blob (inst->vars) laid out by layoutVariables below.
  struct GraphVar
  {
    std::string name{};
    std::string type{"i32"};
  };

  // A single object reference ("Object" node) declared by a graph.
  // 'slot' indexes into the runtime objRefs array, 'name' is the editor label.
  struct ObjRefParam
  {
    uint16_t slot{};
    std::string name{};
  };

  // A variable placed in the per-instance blob: its byte offset and storage size.
  struct VarLayoutEntry
  {
    std::string name{};
    std::string type{};
    uint32_t offset{};
    uint32_t size{};
  };

  // Assigns each variable a 4-byte-aligned offset in declaration order.
  std::vector<VarLayoutEntry> layoutVariables(const std::vector<GraphVar> &vars);
  // Total blob size (bytes) for the given variables (end of the last entry).
  uint32_t varBlobBytes(const std::vector<GraphVar> &vars);

  class Graph
  {
    public:
      ImFlow::ImNodeFlow graph{};
      std::vector<GraphVar> variables{}; // graph-level variable declarations

      static const std::vector<std::string>& getNodeNames();
      // Categorised entries for the Add-Node palette. Indices line up
      // with NODE_TABLE; the metadata (category + pin type masks) is
      // hand-curated in graph.cpp alongside the table itself.
      static std::span<const ::Editor::NodePalette::Entry> getPaletteEntries();
      std::shared_ptr<Node::Base> addNode(uint32_t type, const ImVec2& pos);
      // Creates a node by stable id: NODE_TYPE_IDS alias of a table-based
      // C++ node, or a spec id from the registry (native/JS-defined).
      // Returns nullptr for unknown ids.
      std::shared_ptr<Node::Base> addNode(const std::string &typeId, const ImVec2& pos);

      // Stable string alias for a NODE_TABLE index ("" when out of range).
      // Saved graphs carry it as "typeId" beside the legacy numeric "type";
      // the loader prefers the string, so table indices stop being the only
      // durable identity of a node type.
      static const char* typeIdOf(uint32_t type);
      // Reverse lookup; returns false for unknown ids.
      static bool typeFromTypeId(const std::string &typeId, uint32_t &outType);

      bool deserialize(const std::string &jsonData);
      std::string serialize();

      // Read object refs / variables from a serialized graph without building it.
      static std::vector<ObjRefParam> getObjectRefs(const std::string &jsonData);
      static std::vector<GraphVar> getVariables(const std::string &jsonData);

      // Run structural validation rules over the graph and push diagnostics
      // into `errs` (also mirrored to Logger). Safe to call standalone — both
      // node-graph assets and prefab event graphs share this validator so the
      // Compile Errors panel sees both paths uniformly. `errs` may be null,
      // in which case the call is a no-op.
      void validate(
        ::Project::Compile::ErrorList *errs,
        uint64_t assetUUID
      );

      // `errs` (optional): receives structured compile diagnostics keyed by
      // assetUUID — one Error per validation failure. nullptr keeps any
      // non-editor caller's behavior unchanged. Code emission still runs
      // after validation; the build driver decides whether errorCount() > 0
      // should fail the project build.
      void build(
        Utils::BinaryFile &binFile,
        std::string &source,
        uint64_t uuid,
        ::Project::Compile::ErrorList *errs = nullptr,
        uint64_t assetUUID = 0
      );
  };
}
