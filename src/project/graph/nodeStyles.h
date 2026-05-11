/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <memory>
#include "ImNodeFlow.h"

// Single source of truth for the visual look of every node-graph in the
// editor (script graphs, prefab event graphs, material graphs). Colors
// follow Unreal Engine 5's canonical palette as extracted from
// blueprintue.com's renderer (CSS .node-color.* and pin-type rules).
//
// Pin styles are shared singletons. Node styles are returned as fresh
// shared_ptrs because some callers (e.g. PrefabVarGet) mutate fields
// per-instance.
namespace Project::Graph
{
  enum class PinDataType : uint8_t {
    Exec,       // white triangle, used for control flow
    Bool,       // dark red
    Byte,       // teal
    Int,        // cyan-mint
    Int64,      // pale green
    Float,      // lime
    Double,     // saturated green
    String,     // magenta
    Name,       // violet
    Text,       // pink
    Object,     // sky blue
    Class,      // deep purple
    Interface,  // pale yellow
    Struct,     // royal blue
    Delegate,   // bright red
    Rotator,    // periwinkle
    Wildcard,   // grey
    MatProp,    // material-graph: re-uses Struct color
  };

  enum class NodeCategory : uint8_t {
    Event,                // pill, red
    FunctionCall,         // sky blue
    PureFunctionCall,     // pill, pale green
    ParentFunctionCall,   // orange
    FunctionTerminator,   // purple
    ExecBranch,           // white  (Branch / IsValid / Compare)
    ExecSequence,         // dusty rose (Sequence / Repeat)
    Macro,                // white
    Cast,                 // dark teal
    Switch,               // yellow
    Timeline,             // orange
    BreakStruct,          // royal blue
    Result,               // peach
    MaterialGraphRoot,    // peach (material output sink)
    MaterialConstant,     // olive  (material providers)
    Comment,              // translucent grey
    Variable,             // teal (title bar fallback for Get/Set nodes)
  };

  // Idempotent. Call once at editor startup. Populates pin-style
  // singletons. Safe to call from multiple editor windows.
  void initNodeStyles();

  std::shared_ptr<ImFlow::PinStyle> pinStyle(PinDataType t);
  ImU32 pinColor(PinDataType t);

  // Returns a freshly-allocated NodeStyle so callers can mutate fields
  // (bg / border_color / padding) per-instance without tainting siblings.
  std::shared_ptr<ImFlow::NodeStyle> makeNodeStyle(NodeCategory cat);
  ImU32 categoryColor(NodeCategory cat);
}
