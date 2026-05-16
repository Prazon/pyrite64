# CLI vs GUI Audit - 2026-05-10

Scope: every GUI mutation discoverable in `src/editor/pages/parts/*.cpp`, `src/editor/pages/editorScene.cpp`, and `src/editor/window.cpp`, including node-level operations in the three ImNodeFlow-backed editors (NodeEditor, PrefabEventGraphEditor, MaterialEditor). Pure view/navigation actions (zoom, layout reset, splitters, tooltips, theme, breadcrumb nav, drag-to-pan canvas) are excluded; build/run/clean are excluded (already covered by `--cmd build/clean`).

## Summary

- Total GUI actions enumerated: 92
- Covered by CLI: 59
- Gaps: 33 (P0: 9, P1: 14, P2: 10)

The dominant gap surface is **graph/node editing** (material, event, node-graph) — 11 P0+P1 entries by itself — followed by **prefab functions** (5 entries) and **scene/widget node-level UI hookups** (event binding, canvas2D fields).

## Per-surface gap tables

### Asset browser (`src/editor/pages/parts/assetsBrowser.cpp`)

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| New Folder (`:353`) | `folder-create` | OK | - |
| New Scene (`:378`) | `scene-create` | OK | - |
| New Prefab (`:386`) | `prefab-create` | OK | - |
| New Resource Type (`:393`) | `restype-create` | OK | - |
| New Widget Blueprint (`:412`) | `widget-create` | OK | - |
| New Material (`:430`) | `material-create` | OK | - |
| New Particle System (`assetsBrowser.cpp` New menu) | `particle-system-create` | OK | - |
| New Save File (`assetsBrowser.cpp` New menu) | `save-file-create` | OK | - |
| Save File: add field (saveFileEditor) | `save-file-add-field` | OK | - |
| Save File: remove field (saveFileEditor) | `save-file-remove-field` | OK | - |
| Save File: rename / retype / set default (saveFileEditor) | `save-file-set-field` | OK | - |
| Save File: list / describe (no GUI equiv; for tooling) | `save-file-list` / `save-file-describe` | OK | - |
| New Object Script / Global Script (`:482-483`) | `script-create` | OK | - |
| New Node Graph (`:484`) | `graph-create` | OK | - |
| New Resource Instance (`:498`) | `resource-create` | OK | - |
| Import Asset / Import Script (`:521,532`) | `asset-import` | OK | - |
| Folder rename (`:1507`) | `folder-rename` | OK | - |
| Folder delete (`:1515`) | `folder-delete` | OK | - |
| Scene rename (`:1617`) | `scene-rename` | OK | - |
| Scene duplicate (`:1626`) | `scene-duplicate` | OK | - |
| Scene move to root (`:1629`) | `scene-set-relpath` | OK | - |
| Scene delete (`:1633`) | `scene-delete` | OK | - |
| Asset rename (`:2041`) | `asset-rename` | OK | - |
| Asset delete (`:2048`) | `asset-delete` | OK | - |
| **Create Child Prefab Class** (`:1787`) | `prefab-variant` | OK | - |
| Override inherited property on child prefab | `prefab-override-prop` | OK | - |
| Reset inherited property to parent default | `prefab-reset-prop` | OK | - |
| Override inherited variable default on child prefab | `prefab-override-var-default` | OK | - |
| Reset inherited variable default to parent | `prefab-reset-var-default` | OK | - |
| Remove inherited Object from child prefab | `prefab-remove-inherited-object` | OK | - |
| Remove inherited Component from child prefab | `prefab-remove-inherited-component` | OK | - |
| Describe child prefab inheritance (override counts, lists) | `prefab-describe-inheritance` | OK | - |
| **Drag-drop scene into folder** (`:1478-1483`) | `scene-set-relpath` | OK | - |
| **Drag-drop asset to reparent (folder)** | `asset-move` | OK | - |
| Scene browser settings (view mode, thumb scale, chips) (`:889-930,1995-2007`) | (editor pref) | n/a P2 | P2 |
| **Show in Explorer / Open / Copy Path** (`:2025-2039`) | `asset-describe` returns path | partial | P2 |

No P0/P1 gaps in the asset browser surface itself.

### Scene graph (`src/editor/pages/parts/sceneGraph.cpp`)

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Add Object (`:387`) | `scene-add-object` | OK | - |
| **Add Canvas (2D)** (`:400`) - sets `isCanvas2D=true` on new object | none (no `--canvas` flag) | missing | P0 |
| Make Root (prefab mode) (`:415`) | `prefab-promote-root` | OK | - |
| To Prefab (extract subtree to .prefab) (`:421`) | none | missing | P0 |
| Delete Object (`:425`) | `scene-remove-object` | OK | - |
| Reparent via drag-drop (`:298-313`) | `scene-move-object` | OK | - |
| Drop prefab/widget asset onto object => add instance (`:318-331`) | `scene-add-prefab-instance` | OK | - |
| Rename (F2 / dbl-click) (`:487`) | `scene-set-prop` field=name | partial (works via set-prop) | P2 |
| **Toggle Object `selectable`** (`:354`) | `scene-set-prop` field=selectable | partial (works via set-prop) | P2 |
| **Toggle Object `enabled`** (`:357`) | `scene-set-prop` field=enabled | partial (works via set-prop) | P2 |
| Multi-select via Ctrl-click | n/a (CLI is per-call) | n/a | - |
| Delete-key / Backspace shortcut (`:485`) | `scene-remove-object` | OK | - |

P0: `Add Canvas (2D)` — the 2D pipeline's entry point; agents adding 2D content need this. The flag exists on Object but no CLI field is parsed for it.
P0: `To Prefab` — extracts a subtree to a new .prefab asset and re-instantiates. Distinct from `prefab-create` (which makes an empty one).

Proposed CLI:
- `scene-add-object --canvas` (extends existing `scene-add-object`) OR `scene-set-prop --field isCanvas2D`
- `scene-extract-prefab --asset <scene> --path <obj> [--name Foo]`

### Object inspector (`src/editor/pages/parts/objectInspector.cpp`)

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Rename / set id (`:294-296,292`) | `scene-set-prop` | OK | - |
| Edit Transform pos/scale/rot (`:323,350,367`) | `scene-set-transform` | OK | - |
| Toggle proportional scale (`:360`) | `scene-set-prop` field=proportionalScale | partial | P2 |
| **Toggle "Edit Prefab" instance-vs-source view** (`:307-312`) | none (editor-local view toggle) | n/a | - |
| **2D / Canvas: Canvas Root checkbox** (`:401`) | `scene-set-prop` field=isCanvas2D | partial | P1 |
| **2D / Canvas: Anchor 3x3 grid** (`:420`) - `obj->anchor2D` | `scene-set-prop` field=anchor2D | partial | P1 |
| **2D / Canvas: Layer Idx (2D) drag** (`:433`) - `obj->layerIndex2D` | `scene-set-prop` field=layerIndex2D | partial | P1 |
| **Prefab variable override (per instance)** (`:471-552`) | `scene-set-var-override` / `scene-clear-var-override` | OK | - |
| Add Component (`:631`) | `scene-add-component` | OK | - |
| Duplicate Component (`:587`) | `scene-add-component` + `scene-set-prop` x N | partial (no atomic dup) | P1 |
| Delete Component (`:589`) | `scene-remove-component` | OK | - |
| Edit component prop / drag asset into prop slot | `scene-set-prop` | OK | - |

P1: `scene-duplicate-component` (or `scene-add-component --copy-from <uuid>`) — current workaround requires `component-describe` + N `scene-set-prop` calls to mirror the source.
P1: explicit `--field anchor2D|isCanvas2D|layerIndex2D` validation in `scene-set-prop` / `prefab-set-prop` so an agent can discover them via `component-describe`-style metadata (today they only work because set-prop is generic).

### Asset inspector (`src/editor/pages/parts/assetInspector.cpp`)

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Edit format / baseScale / charset / wav opts / compression / exclude (`:55-106`) | `asset-set-conf` | OK - generic | - |

No gaps.

### Scene inspector (`src/editor/pages/parts/sceneInspector.cpp`)

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Edit name / renderPipeline / frameLimit (`:20-35`) | `scene-set-conf` | OK | - |
| Edit framebuffer w/h/format/color/clear/filter (`:55-79`) | `scene-set-conf` | OK | - |
| Edit audio mixer freq (`:88`) | `scene-set-conf` | OK | - |
| Edit physics tick/grav/iter/units (`:105-110`) | `scene-set-conf` | OK | - |

No gaps (set-conf is generic JSON-keyed).

### Layer inspector (`src/editor/pages/parts/layerInspector.cpp`)

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Add layer (3D/Ptx/2D) (`:56`) | `scene-add-layer` | OK | - |
| Edit layer props (name/Z/blender/light/fog) (`:76-108`) | `scene-set-layer` | OK | - |
| Duplicate layer (`:118`) | `scene-add-layer` + `scene-set-layer` | partial | P2 |
| Delete layer (`:127`) | `scene-remove-layer` | OK | - |
| **Reset all layers** (`:163`) - shrinks tables to defaults and remaps refs | none | missing | P2 |

P2: `scene-reset-layers` — niche; agents can rebuild via add/remove. Includes ref-remapping semantics.

### Project settings (`src/editor/pages/parts/projectSettings.cpp`)

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Edit name/romName/author/version/description (`:22-31`) | `project-set-conf` | OK | - |
| Pick gameImageUUID (`:39`) | `project-set-conf` | OK | - |
| Pick sceneIdOnBoot / OnReset (`:69-73`) | `project-set-conf` | OK | - |
| Cart Size (`:88`) | `project-set-conf` | OK | - |
| ROM title / save type / region-free / RTC (`:101-123`) | `project-set-conf` | OK | - |
| Collision layer names (`:131-135`) | `project-set-conf` field=collLayerNames | partial (array set works, no per-index helper) | P2 |
| Emulator path / N64_INST path (`:141-142`) | `project-set-conf` | OK | - |
| Per-property revert-to-default arrow (UE5 shell) | `project-reset-conf --field <key>` (or `--field all`) | OK | - |
| Search filter / category tree (UE5 shell) | presentational; no data model | n/a | - |

No P0/P1 gaps (set-conf is generic; reset-conf mirrors the revert arrows).

### Editor preferences (`src/editor/pages/parts/preferenceOverlay.cpp`)

Global user-level prefs (`preferences.json`); the PROJ arg is ignored.

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Inspect all preferences | `prefs-describe` | OK | - |
| Edit any pref (speeds, AA, VSync, FPS, euler, content browser, keymap) | `prefs-set --field <k> --value <v>` | OK | - |
| Per-property revert-to-default arrow | `prefs-reset --field <k>` (or `--field all`) | OK | - |
| Keymap preset / per-action rebind | `prefs-set --field keymapPreset` / `--field keymap` | OK (raw JSON) | - |
| Search filter / category tree | presentational; no data model | n/a | - |

### Top menu / global hotkeys (`editorScene.cpp:932-1153`, `window.cpp`)

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Project > Save (`:934`) | n/a (CLI saves on each mutation) | n/a | - |
| Project > Close, Edit > Prefs, View > Zoom/Reset Layout | n/a (editor-local) | n/a | - |
| Build / Build & Run / Clean (`:971-973`) | `--cmd build` / `--cmd clean` | OK | - |
| **Run (without build)** (F12 plays last built ROM if any) | none | missing | P2 |
| Undo / Redo (Ctrl+Z/Y) (`:952,960`) | n/a (each CLI op is atomic) | n/a | - |
| Window menu (focus editor by asset) | n/a | n/a | - |
| Align focused object to camera (Ctrl+Shift+F, `:1148`) | n/a (viewport gizmo) | n/a | - |

No meaningful CLI gaps. P2: a pure `--cmd run` (skip build) is a small ergonomic win.

### NodeEditor (standalone Node Graph asset) - `src/editor/pages/parts/nodeEditor.cpp`

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Add node from right-click palette (`:64`) | none | missing | P0 |
| Add node by dropping link onto canvas (`:64`, `droppedLinkPopUpContent`) | none | missing | P1 |
| Connect pin A->B (`createLink`, `:67`) | none | missing | P0 |
| Duplicate node (right-click, `:83-93`) | none | missing | P1 |
| Remove/destroy node (`:95`) | none | missing | P0 |
| Save graph (Ctrl+S / button, `:155,193`) | n/a (CLI mutations would persist) | n/a | - |
| Compile/validate graph (`:170`) | none | missing | P1 |
| Move node (pos field on node) | none | missing | P2 |
| Edit node-internal property (per-node, e.g. constant value) | none | missing | P1 |

Proposed CLI (all asset-keyed by node-graph .p64graph UUID/name):
- `graph-node-list --asset Foo` (P0 - discovery)
- `graph-add-node --asset Foo --type <typeName> [--pos x,y]` -> echoes nodeUUID (P0)
- `graph-remove-node --asset Foo --node <uuid>` (P0)
- `graph-duplicate-node --asset Foo --node <uuid>` (P1)
- `graph-connect --asset Foo --from <nodeUuid>:<pinName|idx> --to <nodeUuid>:<pinName|idx>` (P0)
- `graph-disconnect --asset Foo --from ... --to ...` (P0)
- `graph-set-node-pos --asset Foo --node <uuid> --pos x,y` (P2)
- `graph-set-node-prop --asset Foo --node <uuid> --field <name> --value <json>` (P1) - for the inline property knobs on a node
- `graph-compile --asset Foo` (P1) - mirrors the Compile button; runs Graph::validate and dumps diagnostics

### PrefabEventGraphEditor - `src/editor/pages/parts/assets/prefabEventGraphEditor.cpp`

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Add node from palette (`:38`) | none | missing | P0 |
| Connect pins | none | missing | P0 |
| Duplicate node (`:130`) | none | missing | P1 |
| Remove node (`:135`) | none | missing | P0 |
| Drop variable from My-Prefab to canvas (`:359-365` -> creates PrefabVarGet) | none | missing | P1 |
| Drop function from My-Prefab to canvas (`:371-376` -> creates PrefabFunc call) | none | missing | P1 |
| Compile / validate (`:215`) | none | missing | P1 |
| **Tier 1 auto-scaffold on first open** (prefabScaffolder seeds lifecycle events on first open of empty graph) | `prefab-scaffold-defaults` | OK | - |
| **"Create stub" inline button on PrefabFunc node with missing target** (`nodePrefabFunc.h`) | `prefab-graph-validate --autofix` | OK | - |
| **"Create variable" inline button on PrefabVarGet node with stale uuid** (`nodePrefabVarGet.h`) | `prefab-graph-validate --autofix` | OK | - |

Proposed CLI (all asset-keyed by prefab name/UUID — event graph is owned by the .prefab):
- `event-graph-list-nodes --asset Foo` (P0)
- `event-graph-add-node --asset Foo --type <typeName> [--pos x,y] [--kind Ready|Tick|...]` (P0)
- `event-graph-add-var-get --asset Foo --var <varName>` (P1) - convenience wrapper that matches the drag-drop UX
- `event-graph-add-func-call --asset Foo --func <funcName>` (P1)
- `event-graph-remove-node --asset Foo --node <uuid>` (P0)
- `event-graph-duplicate-node --asset Foo --node <uuid>` (P1)
- `event-graph-connect --asset Foo --from <nuuid>:<pin> --to <nuuid>:<pin>` (P0)
- `event-graph-disconnect --asset Foo --from ... --to ...` (P0)
- `event-graph-set-node-prop --asset Foo --node <uuid> --field <name> --value <json>` (P1)
- `event-graph-compile --asset Foo` (P1)

### MaterialEditor - `src/editor/pages/parts/assets/materialEditor.cpp`

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Add material-graph node from palette (`:35`) | none | missing | P0 |
| Connect pins | none | missing | P0 |
| Duplicate node (`:83`) | none | missing | P1 |
| Remove node (`:95`) | none | missing | P0 |
| Recompile cache (`:180`) | none | missing | P2 (auto on save) |
| Edit per-node knobs (inline props on a node) | `material-set-prop` (covers compiled-cache scalars only) | partial | P1 |

Proposed CLI:
- `material-graph-list-nodes --asset Foo` (P0)
- `material-graph-add-node --asset Foo --type <typeName> [--pos x,y]` (P0)
- `material-graph-remove-node --asset Foo --node <uuid>` (P0)
- `material-graph-duplicate-node --asset Foo --node <uuid>` (P1)
- `material-graph-connect --asset Foo --from <nuuid>:<pin> --to <nuuid>:<pin>` (P0)
- `material-graph-disconnect --asset Foo --from ... --to ...` (P0)
- `material-graph-set-node-prop --asset Foo --node <uuid> --field <name> --value <json>` (P1)

### Prefab editor My-Prefab panel - `src/editor/pages/parts/assets/prefabEditor.cpp`

Variables: existing `prefab-add/remove/rename-variable` + `prefab-set-variable-default` cover the GUI. Variable duplicate (`:561`) is a partial — agent can replicate via add+set, P2.

Functions:

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Add function (`:727`) | `code-add-function` | OK - existing | - |
| Rename function (`:826`) | `code-rename-function` | OK | - |
| Delete function (`:831`) | `code-remove-function` | OK | - |
| Open source / "Create source files" (`:688`) | n/a (filesystem op) | n/a | - |
| **List functions for a prefab** (`:725` scanPrefabFunctions) | `code-list-functions` | OK | - |

No new gaps; existing `code-*` family covers the prefab-functions surface.

### Widget blueprint editor - `widgetBlueprintEditor.cpp`

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Add widget object / component (palette + drag) | `widget-add-object` / `widget-add-component` | OK | - |
| Bind event from widget to handler | `widget-bind-event` | OK | - |
| Variable add/remove/rename/default | `widget-add/remove/rename-variable` + set-default | OK | - |
| Promote root, move object, set transform | `widget-*` family | OK | - |

No additional gaps in widget editor beyond what the prefab parity already covers.

### ResourceTypeEditor / ResourceInstanceEditor

| GUI action | CLI command | Status | Severity |
|-|-|-|-|
| Add / rename / duplicate / delete field on a resource type (`:61-141`) | `restype-add-prop` / `restype-rename-prop` / `restype-remove-prop` | OK (duplicate is partial - P2) | - |
| Edit resource instance values | `resource-set-prop` | OK | - |

No new gaps. Field duplicate could become `restype-duplicate-prop`, P2.

### Compile errors, log, ROM dashboard

Read-only. No mutations to mirror.

## Top severity rollup

P0 (9 entries):
1. `scene-extract-prefab` - Scene Graph "To Prefab"
2. `scene-add-object --canvas` (or `isCanvas2D` first-class) - 2D entry point
3. `material-graph-add-node` + `material-graph-remove-node` + `material-graph-connect` / `disconnect` (4 commands - the whole material-graph node surface)
4. `event-graph-add-node` + `event-graph-remove-node` + `event-graph-connect` / `disconnect` (4 commands)
5. `graph-add-node` + `graph-remove-node` + `graph-connect` / `disconnect` (4 commands)
6. Per-graph `*-list-nodes` discovery (3 commands)

(Counted as 9 distinct workflow gaps; expands to ~18 individual CLI commands.)

P1 (14):
- `*-duplicate-node` (3 graphs)
- `*-set-node-prop` (3 graphs) - per-node inline property editing
- `*-compile` (2 - node-graph, event-graph; material auto-recompiles on save)
- `event-graph-add-var-get` / `event-graph-add-func-call` (2 convenience wrappers matching the drag-drop UX)
- `scene-duplicate-component` (1)
- Explicit metadata in `component-describe` for `isCanvas2D`/`anchor2D`/`layerIndex2D` (3)

P2 (10): folder UX (Show in Explorer is partial), browser view settings, run-only command, reset-layers, duplicate-layer, duplicate-restype-prop, duplicate-variable (in prefab vars panel), set-node-pos for all three graphs, project-set-collision-layer ergonomics.

## Key engineering note

All three node editors (`nodeEditor.cpp`, `prefabEventGraphEditor.cpp`, `materialEditor.cpp`) lean on `ImFlow::ImNodeFlow` for the visual canvas. Their on-disk persistence is **already JSON** (`Project::Graph::Graph::serialize/deserialize` in `src/project/graph/graph.cpp:124-170`, `Project::MaterialGraph::Graph` similar). Headless mutations should round-trip through that JSON without requiring an ImFlow instance — i.e. CLI ops can patch the saved JSON directly (parse, mutate, write) the same way `scene-set-conf` patches scene JSON. No GPU/ImGui context needed. That makes the P0 graph commands cheaper than they sound — they are JSON edits with node-type validation against the `NODE_TABLE` registry, not full ImFlow instantiations.

## Status after this session

Delivered as CLI commands (all smoketested in `tests/cli/run.py`):

**P0 — all closed:**

- `scene-extract-prefab --asset <scene> --path <obj>` — Scene Graph "To Prefab"
- `scene-add-object --type canvas` — Scene Graph "Add Canvas (2D)" (extends existing command; sets `isCanvas2D=true`)
- `graph-{list-nodes,add-node,remove-node,connect,disconnect}` — standalone NodeGraph
- `event-graph-{list-nodes,add-node,remove-node,connect,disconnect}` — prefab event-graph (graph inside `eventGraphJSON`)
- `material-graph-{list-nodes,add-node,remove-node,connect,disconnect}` — material-graph (graph inside `.p64mat`'s `graphJSON`). Note: edits do NOT trigger a recompile of `MaterialAsset::compiled`; that still runs on save inside the editor.

**P1 — all closed:**

- `scene-duplicate-component` — Object Inspector "Duplicate Component"
- `{graph,event-graph,material-graph}-set-node-prop` — per-node inline property editing
- `{graph,event-graph,material-graph}-duplicate-node` — clone with fresh uuid, optional pos
- `{graph,event-graph}-compile` — structural validation (entry-node present, no duplicate Start, no dangling link refs). Pin-style reachability stays GUI-only since it needs ImFlow. Material auto-compiles on save, so no material-graph-compile.
- `event-graph-add-var-get` / `event-graph-add-func-call` — drag-drop UX wrappers

**P2 — all closed:**

- `{graph,event-graph,material-graph}-set-node-pos` — canvas placement
- `scene-duplicate-layer` — clone an existing layer entry
- `scene-reset-layers` — `Scene::resetLayers()` exposure with ref-remap semantics
- `restype-duplicate-prop` — clone a resource-type field
- `prefab-duplicate-variable` / `widget-duplicate-variable` — clone a prefab class variable
- `project-set-collision-layer --field N --name "..."` — per-index helper for the 8 collLayerN slots

**Still open (deliberate skips):**

- Explicit `isCanvas2D` / `anchor2D` / `layerIndex2D` metadata in `component-describe`. The generic `scene-set-prop` already handles these, just without schema introspection.
- `--cmd run` (launch the last-built ROM without rebuilding). Ergonomic shell call, not really a CLI gap; the .z64 path is already known.
- Show-in-Explorer / Copy-Path / Open-with-OS shell sugar. `asset-describe` returns the on-disk path; the rest is a host-shell concern.

## Component parity

- **PathFollow (id 31).** New component that drives its owning object along a Path spline (resolves a Path on self → parent → explicit object). No bespoke CLI verbs needed: the generic `component-describe --comp PathFollow` reflects its schema and `prefab-set-prop`/`scene-set-prop` set every field (`objectUUID`, `speed`, `startDistance`, `mode`, `orient`, `autoPlay`, `previewDistance`). Smoketested via `component-describe-pathfollow` + `prefab-add-pathfollow-comp` + `prefab-set-pathfollow-{speed,mode}`. The Camera-PiP scrub preview is editor-GUI-only (no headless surface, by design — the editor cannot run engine motion).

## Diagnostics parity (non-command)

- **N64 S10.5 UV-range warning.** The editor renders large-texture/large-object UVs faithfully (the rainbow wrap artifact is real hardware behaviour) but now flags it: a `WARN` toast + Log line on model load, mirrored headless as `uvRange{outOfRange,worstPixel,limitPixel,material}` in `asset-describe` (detailed) and a compact `uvOutOfRange:true` in `asset-list` (so `asset-list --type model3d` is a headless scanner). Build-time emission rides the shared asset-load `Logger::log(WARN)` → stdout in `--cli --cmd build`. Smoketested via `asset-list-model3d` (in-range fixtures must stay flag-free).

## CLI bugs fixed by this work

- **Camera `component-describe` aborted when stdout was piped.** `Camera::init` dereferenced `scene` without a null check; the editor side never hit it (a scene was always loaded) but the CLI path called it with no loaded scene. Fixed in `src/project/component/types/compCamera.cpp`.
- **`project-create` was unreachable** because the positional `project` arg forced `Project::Project{path}` to construct (and throw on missing file) before dispatch ran. Fixed by routing project-create through a new `dispatchBootstrap` path in `src/cli.cpp` + `src/cli/cliCommands.{h,cpp}` so it bypasses the ctor.
