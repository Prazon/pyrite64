# Graph palette gaps surfaced by Pixic port

Working notes captured while landing the Pixic-driven graph palette
expansion (Pyrite plan steps 1-6). Each entry has: what is missing,
why Pixic surfaces it, and a sketched fix. Implementer who picks any
entry up should expand it into a real plan first.

## B1. Structured loops with break / continue

**Status:** deferred. Group B (ForRange / While / Break / Continue)
not implemented in the initial Pixic port.

**What exists:** `Repeat` node, which is a per-exec counter, not a
structured loop. Each exec increments; on threshold it fires Exit,
else it falls through to Loop. To "iterate" the user has to wire the
body's terminal back to Repeat's exec input.

**Why it matters for Pixic:** every per-frame integration loop
(particles, falling blocks) wants `ForEach<Array>` (Group D) which is
the natural fit. Pure counter loops are rarely needed since most
gameplay loops are over arrays. Hence the priority lands on Group D
first.

**Why Group B was hard:** the current goto-based codegen visits all
nodes flatly. A real loop needs to identify the body subgraph (the
exec descendants of the loop's Body output up to the next loop or
terminator) and either:
  - emit it inline inside a real C++ for/while at codegen time, or
  - emit a label-stack pattern where Break / Continue resolve via
    BuildCtx's `loopStack` (push / pop around body emission).

Either path requires the build pass to know which nodes belong to
which loop body. That's a pre-walk over the exec graph plus a
"consumed-by-loop" marker so the main flat build loop skips them
(else they'd emit twice). Manageable but a real refactor.

**Sketch for whoever picks this up:**
1. In `Project::Graph::Graph::build`, do a pre-pass: for every loop
   node, BFS the exec graph from `outUUIDs[0]` (Body), stopping at
   nodes that already belong to a parent loop or that have no exec
   in. Mark those nodes' `loopOwner = loopUUID` on a side map.
2. Sort nodeVec so loops come before their body nodes.
3. Loop nodes' `build()` emits a header label `LOOP_TOP_<uuid>:`,
   condition / increment, falls through to body; body nodes emit
   normally; ForRange's exec block ends with `goto LOOP_TOP_<uuid>;`
   and an `EXIT_<uuid>:` label.
4. `loopStack` on BuildCtx tracks `{topLabel, exitLabel}` for the
   currently-open loop; Break / Continue read the top.
5. For ForEach<Array>: same machinery, with the iteration variable
   being a `for(auto &elem : arr)` if codegen wants to emit a real
   range-for, or an index counter otherwise.

## D-related gaps (Step 6 partial-landing)

**Status:** array NODES landed (ArrayMake / Length / Get / Set /
Push / Pop / Insert / RemoveAt / Clear / Find / Contains, all on
`std::vector<float>` only). Engine-side ARRAY var support is mostly
in place: `VarKind::ARRAY`, `kindToTypeFull` emitting
`std::vector<E>`, and `Object::getPrefabVarRef<T>` for non-trivial
T. The remaining wiring lives in files another Claude session is
actively editing, listed below.

**D1. PrefabVarGet for ARRAY kind.** `nodePrefabVarGet.h` needs:
  - An `elemKind` field (uint8_t) so the codegen knows the std::vector's
    template parameter without re-walking the prefab variables list.
  - `kindToCType` extended for kind 8 (ARRAY) to emit
    `std::vector<E>` based on the cached elemKind.
  - `kindToPinType` mapping ARRAY to `PinDataType::Wildcard` (or a
    new `PinDataType::Array` if a distinct color is wanted).
  - `build()`: emit by reference. `auto &val_X =
    *self->getPrefabVarRef<std::vector<E>>(uuid);` so mutations land
    on the actual storage.
  - `draw()`: surface the elemKind dropdown when the bound var is
    ARRAY.
  - `serialize`/`deserialize`: persist elemKind alongside the
    existing varUUID / varName / varKind.

**D2. CLI `--element-kind` flag.** `prefab-add-variable` needs a new
flag to declare ARRAY vars from the CLI:
```
prefab-add-variable --asset Foo --name parts --kind ARRAY --element-kind FLOAT
```
One-flag addition to an existing command in
`src/cli/cliCommands.cpp`.

**D3. Editor inspector UI for ARRAY var declaration.** The prefab
Variables panel needs an element-kind picker that surfaces only when
kind = ARRAY. Lives wherever `PrefabVarDef` is added/edited in the
inspector.

**D4. Array element types beyond float.** v1 arrays are
`std::vector<float>` only. Pixic would benefit from
`std::vector<int32_t>` (grid coordinates, block colors),
`std::vector<std::string>` (highscore names), and ideally
`std::vector<struct>` (highscores with name + score). Array nodes'
codegen is currently hard-coded to float; would need either a per-
node element-type selector or a Wildcard-typed pass-through where
the element type is inferred from the connected source.

**D5. ForEach<Array>.** Not implemented in v1 because it has the
same loop-codegen problem as Group B (see B1). ForEach is the
highest-value loop variant for Pixic since all Pixic iteration is
over arrays; tackling B1's structured-loop codegen would unblock
both at once.

**D6. Array literal UX.** `ArrayMake` uses SwitchCase's dynamic-pin
pattern (Add Element button). For long literals an inline "table of
values" widget would be more ergonomic; current pattern requires one
click per element plus wiring a Value node to each pin.

**D7. Array persistence within a single graph dispatch only.**
Currently arrays declared via `ArrayMake` live as function-top
`std::vector<float>` globalVars that re-initialize on every
dispatch. For per-frame mutation (Pixic particle list, falling
block list) the array MUST be a prefab var so its storage persists
across OnTick dispatches. D1 is the unblocker.

## DeltaTime in standalone NodeGraph script assets

**Status:** v1 fail-loud. Using the `DeltaTime` node inside a
standalone `.ngraph` script asset will emit `auto res = deltaTime;`
which fails to compile because the generated `run(void* arg)`
function has no `deltaTime` parameter (only the prefab event-graph
dispatch path does).

**Fix:** add a `lastDeltaTime` field to `P64::NodeGraph::Instance`,
set it in `nodeGraph.cpp` before each `coro_resume`, and have
DeltaTime's codegen detect script-graph vs prefab-event-graph
context (e.g., via a `BuildCtx::contextKind` field) and emit
`inst->lastDeltaTime` vs `deltaTime` accordingly.

## Pure-evaluation math nodes (no exec wires)

**Status:** v1 design choice. All Group A math / logic / cmp nodes
require exec wiring (matches existing Compare / CompBool pattern).
This means a chain like `Value -> Add -> SetScore` has to be wired
both via exec and via value pins.

**Why:** pure-evaluation nodes need topological sort of function-top
globalVar declarations so a downstream initializer references an
already-declared upstream one. Doable but the v1 path was kept
simple to land Group A quickly.

**Fix:** topological sort of value-input dependencies in the build
pass. Emit pure-value nodes' globalVars at function top in dep
order. UE-style pure nodes (no exec pins) become possible.

## String memory pressure

**Status:** noted, no observed issue yet. Pixic's per-frame string
ops (score formatting, label updates) heap-allocate via
`std::string`. Likely fine at Pixic's scale; flag if profiling shows
allocator pressure.

**Mitigation:** a fixed-buffer string variant for hot paths, or
arena-allocated strings reset per frame.
