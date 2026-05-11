# Graph palette gaps surfaced by Pixic port

Working notes captured while landing the Pixic-driven graph palette
expansion. Most B / D / pure-eval items below originally shipped as
deferred follow-ups; the second pass (this commit batch) closed the
ones noted as **landed**. Anything still marked **deferred** is a real
follow-on requirement.

## B1. Structured loops with break / continue — landed

`ForRange`, `While`, `ForEach`, `Break`, `Continue` ship as nodes
NODE_TABLE 62-66 ("Flow Control"). The build pass identifies each
loop's body subgraph by exec-BFS from `outUUIDs[0]` (Body), stopping
at nested loops, and inlines body nodes between
`buildLoopHeader` / `buildLoopFooter` into a real C++ for/while
block. Break/Continue map onto the C++ keywords directly. Falling
off a body chain inside a loop becomes `continue;` instead of
`return;` via `BuildCtx::insideLoopBody`. Both standalone NodeGraph
script assets (`graph.cpp`) and prefab event graphs
(`prefabBuilder.cpp`) share the same emitter pattern.

## D-related ARRAY work — landed

**Status:** complete end-to-end.

- **D1. PrefabVarGet for ARRAY kind:** `nodePrefabVarGet.h` carries
  an `elemKind` field, syncs it from `PrefabVarDef.typeArg` on
  selection, and emits
  `auto& res_<id> = *self->getPrefabVarRef<std::vector<E>>(uuid);`
  so mutations land on the actual storage. Persists across OnTick
  dispatches because the Object owns the vector.
- **D2. CLI `--element-kind`:** `prefab-add-variable --type=array
  --element-kind={int|float|bool}` adds ARRAY vars from the CLI;
  validated in `cmdPrefabAddVariable`.
- **D3. Inspector ARRAY UI:** the prefab Variables panel offers
  `Array` in the kind dropdown and surfaces an `Element` row
  (Int / Float / Bool) when ARRAY is selected. Per-instance overrides
  show `(empty — runtime-populated)` since arrays are not authored
  per object.
- **D4. Element types beyond float:** array nodes use
  `(typename std::remove_reference_t<decltype(arr)>::value_type)(v)`
  so Push/Set/Insert/Find/Contains adapt to whatever element type
  the source array exposes. ArrayMake declares the vector type from
  its own `elemKind` dropdown; ArrayGet/Pop still cast to float for
  the cross-node `res_<uuid>` lingua franca (lossy for non-numeric
  kinds — see D7 follow-on).
- **D5. ForEach<Array>:** part of the structured-loops batch above.
- **D6. ArrayMake table widget:** `useLiterals` toggle switches
  ArrayMake into inline DragFloat-per-row mode for static lookup
  tables, with `+ Element` and `-` buttons to size the array.
- **D7. Array persistence across dispatches:** the runtime
  placement-news `std::vector<E>` into the var blob at scene load
  (`sceneLoader.cpp`) and `~Object` runs `~vector` on teardown so
  heap allocations unwind. Element kind travels in record byte 9
  (was a pad byte, written by `sceneBuilder.cpp`).

**Open follow-ons:**
  - String / struct element kinds. Pin colors and ArrayGet's float
    cast assume numeric element types. Adding string would need
    typed value pins (PinDataType::String) routed through the
    `res_<uuid>` mechanism, plus a non-lossy ArrayGet path.
  - `getPrefabVarRef<T>` does no kind check — passing the wrong T
    silently reinterprets the bytes. A debug-build assertion that
    cross-checks the record's kind/elemKind would catch user errors.

## DeltaTime in standalone NodeGraph script assets — landed

`P64::NodeGraph::Instance::lastDeltaTime` is refreshed by `update()`
before each `coro_resume`; the generated `run()` binds
`float& deltaTime = inst->lastDeltaTime;` at function entry so the
DeltaTime node resolves the same identifier in both contexts. v1
codegen still snapshots into a `res_<uuid>` globalVar at function
entry, so values read after a Wait yield are stale until pure-eval
also handles re-entrant value re-evaluation (deferred — see below).

## Pure-evaluation math nodes — landed (with caveats)

All Group A math (Add/Sub/Mul/Div/Mod/Min/Max/Clamp/Abs/Floor/Ceil/
Round/Sign/Sqrt/Pow), boolean (And/Or/Not/Xor), and comparison
(Eq/Ne/Lt/Le/Gt/Ge) nodes opt in via `canBePure()` and supply a
`buildAsPure()` that inlines the expression into the globalVar
initializer. The build pass (graph.cpp + prefabBuilder.cpp) detects
nodes with no incoming exec edge, topo-sorts them by value-input
dependencies, and emits their inits at function-top in dep order.
Result: a `Value -> Add -> SetScore` chain no longer needs an exec
wire through Add — it just works.

**Open follow-ons:**
  - Re-entrant evaluation. globalVars are still init-once; if a pure
    Add reads a Wait-mutated input after a coro yield, the Add's
    cached value is stale. Real pure-eval needs to inline the
    expression at consumer sites (or wrap each value pin in an
    inline lambda) instead of caching once at function entry.
  - Cycle detection. Today, a cyclic value-dependency between pure
    nodes would recurse forever in `emitPure`. Add a depth-limited
    visit set with an error when a cycle is hit.

## String memory pressure — deferred

**Status:** noted, no observed issue yet. Pixic's per-frame string
ops (score formatting, label updates) heap-allocate via
`std::string`. Likely fine at Pixic's scale; flag if profiling shows
allocator pressure.

**Mitigation:** a fixed-buffer string variant for hot paths, or
arena-allocated strings reset per frame.
