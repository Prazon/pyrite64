# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

This is **Prazon's fork of Pyrite64** — a C++23 visual editor + N64 runtime engine built on libdragon + tiny3d. The fork's purpose is to add **2D and 2.5D functionality** that the upstream engine (which is 3D-only) does not provide, and to improve editor usability/intuitiveness, drawing on Unreal/Godot/GameMaker conventions.

Upstream is `HailToDodongo/pyrite64`; this fork's `origin` is `git@github.com:Prazon/pyrite64.git`. Always assume changes here will eventually need to be defensible against upstream — keep fork additions clearly marked (see existing `// added by SPBF64 fork` comments) and avoid gratuitous reformatting of upstream code.

The first consumer of this fork is the SPBF64 game project at `B:\PyriteProjects\SPBF64\` (a libdragon-original platform fighter being ported onto Pyrite). Its `CLAUDE.md` describes the consumer-side contract — read it when reasoning about how editor changes ripple into game projects, especially the **engine mirroring** behavior: every project gets a regenerated copy of `n64/engine/` under `<project>/engine/` whenever the editor builds.

## Build

GCC + CMake + Ninja, C++23 (engine library inside `n64/engine` is C++20 with `-fno-exceptions -Os`). On Windows use MSYS2 UCRT64. Output binary is `pyrite64.exe` at the repo root; `./data` and `./n64` must sit next to it at runtime.

```bash
# Configure once
cmake --preset windows-gcc-release   # or linux-release / *-debug
# Build (every iteration)
cmake --build --preset windows-gcc-release
```

Editor CLI mode (used by game projects' `make p64`):

```bash
./pyrite64.exe --cli --cmd build  /path/to/project.p64proj
./pyrite64.exe --cli --cmd clean  /path/to/project.p64proj
./pyrite64.exe --help
```

`build/compile_commands.json` is emitted — point clangd/IDE tooling there.

There are no editor-side unit tests. `n64/tests/` and `n64/examples/` are runtime sample ROMs built through libdragon's makefile system, not CTest targets. CI workflows are in `.github/workflows/` (`editor.yml`, `docs.yml`).

## Two-codebase layout

This repo is **two separate C++ codebases** that ship as one binary + sidecar:

1. **Editor & build tooling** — `src/` (C++23, links SDL3/SDL_image/SDL_shadercross, ImGui, ImGuizmo, glm, ImNodeFlow). Compiled by the top-level `CMakeLists.txt`. This is the host app, the GLTF importer, the asset pipeline, the project format, and the CLI builder.
2. **N64 runtime engine** — `n64/engine/` (C++20, libdragon + tiny3d, cross-compiled by N64 toolchain via `n64/engine/Makefile`). This is shipped *as source* into each game project, where it's compiled into `engine.a` and linked into the ROM.

A change to a runtime feature usually touches **both** sides:

- `n64/engine/include/scene/components/<name>.h` + `n64/engine/src/scene/components/<name>.cpp` — the on-device implementation.
- `src/project/component/types/comp<Name>.cpp` — the editor-side schema (PROP_* fields, ImGui inspector, JSON serialization, viewport gizmo) registered through `src/project/component/components.{h,cpp}`.
- `src/project/component/components.cpp` (registration entry).
- For editor-viewport preview: `src/editor/pages/parts/viewport3D.cpp` if the component needs gizmo/preview rendering.

The two halves communicate by **UUID-keyed component IDs** (e.g. `SpriteBillboard::ID = 12`). The editor writes binary scene data and asset tables; the runtime reads them at boot. When you add a runtime field, both sides of the wire must agree.

`SpriteBillboard` (commit `d83321a` and follow-ups) is the canonical reference for "how to add a fork-specific component" — it shows the full editor↔engine boundary, the inspector UI, the 3D-viewport preview quad, and the device-side `rdpq_sprite_blit` rendering.

## Engine sync into game projects

When the editor builds a project in CLI or GUI mode, it **mirrors `<repo>/n64/engine/` into `<project>/engine/`** and prunes stale files (commit `09a1861`). Game projects therefore never edit `engine/` themselves — they edit it here in the fork, and the next build copies it across.

Implication: any header or source you add under `n64/engine/{include,src}/` needs to be picked up by the engine `Makefile`'s wildcard globs (`src/scene/components/*.cpp`, etc.) — if it's not in a globbed directory, it won't compile in projects.

Generated outputs in game projects (`Makefile`, `engine/`, `src/p64/{sceneTable,scriptTable,globalScriptTable,assetTable}.{cpp,h}`, `assets/p64/`, `filesystem/`) are owned by **this** binary. Hand-written game code in projects only lives in `src/user/`. Don't suggest editing those generated outputs — fix the generator here.

## Editor architecture (big picture)

- `src/main.cpp` boots SDL3 + SDL_GPU + ImGui. There's a single global `Context ctx{}` (`src/context.h`).
- `src/editor/window.cpp` is the top-level frame; pages live under `src/editor/pages/` (`launcher`, `editorScene`) and panel widgets under `src/editor/pages/parts/` (scene graph, object inspector, asset browser, node editor, viewport3D, etc.).
- `src/project/` owns the on-disk project model: `assetManager` (UUID-keyed asset store with thumbnails), `scene/` (objects, prefabs, scene manager), `component/` (per-component schema + inspector + serialization), `assets/` (collision, material, model3d), `graph/` (visual node graph).
- `src/renderer/` is the **editor's preview renderer** (SDL_GPU, not RDP) — separate from the on-device `n64/engine/src/renderer/`. `n64Mesh.cpp` bridges T3D-imported meshes into the editor preview.
- `src/build/projectBuilder.cpp` (referenced from `main.cpp`) is the CLI build driver that regenerates tables and invokes `make` on the project.
- `src/editor/undoRedo.{cpp,h}` — every mutation that should be undoable goes through this; component inspectors push actions defined in `src/editor/actions.cpp`.
- `src/editor/keymap.{cpp,h}` + `preferences.cpp` — input bindings and persisted user prefs (`preferences.json`, gitignored).

ImGui state persists in `imgui.ini` at the repo root (gitignored). When testing UI/UX changes, delete or back this up if you need a clean layout.

## Runtime engine architecture (n64/engine/)

Subsystems under `n64/engine/include/`: `assets`, `audio`, `collision`, `debug`, `lib` (math/memory/types/ringbuffer/fifo), `renderer` (incl. `hdr`, `bigtex`, `particles` — each ships custom RSP microcode), `scene` (objects/components/camera/lighting/event), `script`, `vi`. World convention is **Y-up, gravity along -Y**.

Component model: each component is a POD struct in `P64::Comp::` with `static` lifecycle (`initDelete`, `update`, `draw`) and a stable `ID`. Components register through `n64/engine/include/scene/componentTable.h`. Adding a component means adding the ID there and updating the editor's `components.cpp` to mirror it.

Rendering caveats carried from libdragon work that still apply when authoring engine code:

- Vertex / matrix buffers must be `malloc_uncached()` — stack-allocated `T3DVertPacked[]` silently fails to render.
- Push a model matrix (`t3d_matrix_push`) before `t3d_vert_load` and pop after; identity is fine for world-space.
- The MIPS toolchain has occasional optimization quirks at `-Os`. If a freeze appears after an innocuous edit, try `make clean` and consider splitting compound conditionals into nested `if`s before chasing logic bugs.

## Working in this fork

- **2D/2.5D is the goal of this fork.** Upstream is conservatively 3D-focused and may reject 2D features. Keep fork additions self-contained and feature-flagged where reasonable so rebases on upstream stay cheap.
- Tag fork-specific files with a header comment (existing convention: `// added by SPBF64 fork`) so future merges with upstream are easy to triage.
- Don't echo this fork's branding/icon changes into upstream PRs — the blue-tinted icon (`32d37e8`) and inline branding (`a82fbea`) are intentional fork markers.
- Editor UX work should draw on Unreal/Godot/GameMaker conventions where they conflict with the current Pyrite UX — that's the explicit point of this fork. Call out the parallel in commit messages so the design intent survives.
- LFS: `*.blend`, `*.wav`, `*.mp3`, `*.mp4` are LFS-tracked. PNG/TTF/JPG were intentionally untracked from LFS in `a82fbea` — don't re-add them.
- Submodules are pinned in `.gitmodules`; after pulling run `git submodule update --init --recursive`.

## When debugging a runtime issue

The editor cannot run engine code on the host — it only previews. To actually exercise engine changes you must build a project (e.g. SPBF64) through the editor's CLI build, then run the resulting `.z64` in **Ares (v147+)** or gopher64. HLE emulators (Project64, Mupen64Plus stock) will not be accurate enough.
