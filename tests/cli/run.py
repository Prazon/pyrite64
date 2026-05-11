#!/usr/bin/env python3
"""CLI smoketest harness for pyrite64.

For each registered CLI command, runs it against a copy of the baseline
fixture and asserts:
  - exit code matches expectation (0 by default; some tests are negative)
  - stdout, after stripping the "Pyrite64 - CLI" prelude and [INF] log
    lines, parses as JSON (most commands emit JSON)

Tests run sequentially against a shared temp copy of the fixture so
mutations from earlier tests are visible to later ones (e.g. prefab-create
then prefab-describe). A test marked reset=True forces a fresh copy of
the baseline before it runs.

Usage:
  python3 tests/cli/run.py                  # run everything
  python3 tests/cli/run.py --filter prefab  # substring filter
  python3 tests/cli/run.py --keep-temp      # leave temp dir for inspection
  python3 tests/cli/run.py --list           # show test names only
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

REPO = Path(__file__).resolve().parents[2]
BASELINE = REPO / "tests" / "cli" / "fixtures" / "baseline"
PROJ_REL = "project.p64proj"
# pyrite64.exe is a Windows binary, so /tmp paths (WSL-side) end up as
# C:\tmp\... and fail to open. Park work-copies inside the repo's build/
# directory, which is gitignored and shares the same path between WSL and
# Windows (/mnt/b/forks/pyrite64 <-> B:\forks\pyrite64).
TEMP_PARENT = REPO / "build" / "cli_smoke"


def find_exe() -> Path:
    for name in ("pyrite64.exe", "pyrite64"):
        p = REPO / name
        if p.exists():
            return p
    sys.exit(f"smoke: binary not found at {REPO}/pyrite64[.exe]")


EXE = find_exe()


@dataclass
class Test:
    name: str
    cmd: str
    args: List[str] = field(default_factory=list)
    expect_json: bool = True
    expect_fail: bool = False
    reset: bool = False
    # Override the project path positional (default: current temp copy).
    project: Optional[str] = None


_LOG_BRACKETS = ("[INF]", "[WRN]", "[ERR]", "[DBG]")


def strip_prelude(stdout: str) -> str:
    """Discard non-JSON prelude lines (the 'Pyrite64 - CLI' header,
    sceneManager log lines like 'Create-Scene: foo', [INF]/[WRN]/etc.).
    Returns the substring starting at the first line whose first
    non-whitespace char opens a JSON value."""
    lines = stdout.splitlines()
    for i, line in enumerate(lines):
        stripped = line.lstrip()
        if not stripped:
            continue
        c = stripped[0]
        if c in "{[":
            # Reject "[INF] ..." style log brackets.
            if any(stripped.startswith(p) for p in _LOG_BRACKETS):
                continue
            return "\n".join(lines[i:])
        if c == '"' and stripped.endswith('"'):
            return "\n".join(lines[i:])
        if stripped in ("true", "false", "null") or _is_number(stripped):
            return "\n".join(lines[i:])
        # Non-JSON content (e.g. "Create-Scene: b") — skip.
    return ""


def _is_number(s: str) -> bool:
    try:
        float(s)
        return True
    except ValueError:
        return False


def to_repo_rel(p: Path | str) -> str:
    """Path relative to REPO so the Windows binary sees something it can open."""
    pp = Path(p)
    try:
        return str(pp.relative_to(REPO)).replace("\\", "/")
    except ValueError:
        return str(pp)


def run_test(test: Test, workdir: Path) -> tuple[bool, str]:
    workdir_rel = to_repo_rel(workdir)
    def expand(s: str) -> str:
        return s.replace("{WORKDIR}", workdir_rel)
    proj = expand(test.project) if test.project else to_repo_rel(workdir / PROJ_REL)
    exe_rel = to_repo_rel(EXE)
    cmd = ["./" + exe_rel, "--cli", "--cmd", test.cmd] + [expand(x) for x in test.args] + [proj]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=120, cwd=str(REPO))
    except subprocess.TimeoutExpired:
        return False, "timeout"

    if test.expect_fail:
        rc_ok = res.returncode != 0
        expected = "non-zero"
    else:
        rc_ok = res.returncode == 0
        expected = "0"

    detail = []
    if not rc_ok:
        detail.append(f"exit={res.returncode} expected {expected}")
        if res.stderr.strip():
            detail.append(f"stderr={res.stderr.strip()[:200]}")
        if res.stdout.strip():
            detail.append(f"stdout={res.stdout.strip()[:200]}")
        return False, " | ".join(detail)

    if test.expect_fail:
        return True, "expected failure"

    if test.expect_json:
        body = strip_prelude(res.stdout)
        if not body.strip():
            return False, "no JSON body in stdout"
        try:
            json.loads(body)
        except json.JSONDecodeError as e:
            return False, f"invalid JSON: {e} | body[:200]={body[:200]!r}"

    return True, "ok"


# ---------------------------------------------------------------------------
# Test list. Roughly grouped by entity type.
# ---------------------------------------------------------------------------

TESTS: List[Test] = [
    # === help / read-only ==============================================
    Test("help",                       "help",                expect_json=False),
    Test("asset-list",                 "asset-list",          reset=True),
    Test("asset-list-image",           "asset-list",          ["--type", "image"]),
    Test("asset-list-prefab",          "asset-list",          ["--type", "prefab"]),
    Test("asset-describe-image",       "asset-describe",      ["--asset", "crate32.png"]),
    Test("asset-describe-model",       "asset-describe",      ["--asset", "box.glb"]),
    Test("asset-describe-conf-image",  "asset-describe-conf", ["--asset", "crate32.png"]),
    Test("asset-find-unused",          "asset-find-unused"),
    Test("component-list",             "component-list"),
    Test("component-describe-camera",  "component-describe",  ["--comp", "Camera"]),
    Test("component-describe-model",   "component-describe",  ["--comp", "1"]),  # Model (Static), use numeric id
    Test("scene-list",                 "scene-list"),
    Test("event-list",                 "event-list"),
    Test("project-describe",           "project-describe"),

    # === prefab CRUD ===================================================
    Test("prefab-create",              "prefab-create",       ["--name", "TPrefab1"]),
    Test("prefab-describe",            "prefab-describe",     ["--asset", "TPrefab1"]),
    Test("prefab-duplicate",           "prefab-duplicate",    ["--asset", "TPrefab1", "--name", "TPrefab1Dup"]),
    Test("prefab-add-object-child",    "prefab-add-object",   ["--asset", "TPrefab1", "--name", "Child"]),
    Test("prefab-add-object-leaf",     "prefab-add-object",   ["--asset", "TPrefab1", "--parent", "Child", "--name", "Leaf"]),
    Test("prefab-set-transform-pos",   "prefab-set-transform",["--asset", "TPrefab1", "--path", "Child", "--field", "pos", "--value", "[1,2,3]"]),
    Test("prefab-set-transform-rot",   "prefab-set-transform",["--asset", "TPrefab1", "--path", "Child", "--field", "rot", "--value", "[0,0,0,1]"]),
    Test("prefab-set-transform-scale", "prefab-set-transform",["--asset", "TPrefab1", "--path", "Child", "--field", "scale", "--value", "[2,2,2]"]),
    Test("prefab-add-component-model", "prefab-add-component",["--asset", "TPrefab1", "--path", "Child", "--comp", "1"]),
    Test("prefab-set-prop-model",      "prefab-set-prop",     ["--asset", "TPrefab1", "--path", "Child", "--comp", "1", "--field", "layerIdx", "--value", "1"]),
    Test("prefab-remove-component",    "prefab-remove-component",["--asset", "TPrefab1", "--path", "Child", "--comp", "1"]),
    Test("prefab-move-object",         "prefab-move-object",  ["--asset", "TPrefab1", "--path", "Child/Leaf", "--parent", ""]),
    Test("prefab-find",                "prefab-find",         ["--asset", "TPrefab1", "--comp", "Path"]),
    Test("prefab-find-references",     "prefab-find-references",["--asset", "TPrefab1"]),
    Test("prefab-remove-object",       "prefab-remove-object",["--asset", "TPrefab1", "--path", "Child"]),

    # === prefab variables ==============================================
    Test("prefab-add-variable",        "prefab-add-variable", ["--asset", "TPrefab1", "--name", "speed", "--type", "float", "--value", "3.5"]),
    Test("prefab-set-variable-default","prefab-set-variable-default",["--asset", "TPrefab1", "--name", "speed", "--value", "7.0"]),
    Test("prefab-rename-variable",     "prefab-rename-variable",["--asset", "TPrefab1", "--from", "speed", "--to", "velocity"]),
    Test("prefab-remove-variable",     "prefab-remove-variable",["--asset", "TPrefab1", "--name", "velocity"]),

    # === prefab variant + patch ========================================
    Test("prefab-variant",             "prefab-variant",      ["--parent", "TPrefab1", "--name", "TPrefabVar"]),
    Test("prefab-list-patches",        "prefab-list-patches", ["--asset", "TPrefabVar"]),
    Test("prefab-add-patch",           "prefab-add-patch",    ["--asset", "TPrefabVar", "--value", '{"op":"replace","path":"/obj/name","value":"VarRoot"}']),
    Test("prefab-remove-patch",        "prefab-remove-patch", ["--asset", "TPrefabVar", "--field", "0"]),

    # === path component (prefab) =======================================
    Test("prefab-add-path-comp",       "prefab-add-component",["--asset", "TPrefab1", "--path", "", "--comp", "Path"]),
    Test("path-add-point-1",           "path-add-point",      ["--asset", "TPrefab1", "--path", "", "--value", "[0,0,0]"]),
    Test("path-add-point-2",           "path-add-point",      ["--asset", "TPrefab1", "--path", "", "--value", "[10,0,0]"]),
    Test("path-add-point-3",           "path-add-point",      ["--asset", "TPrefab1", "--path", "", "--value", "[20,0,0]"]),
    Test("path-insert-point",          "path-insert-point",   ["--asset", "TPrefab1", "--path", "", "--field", "1", "--value", "[5,0,0]"]),
    Test("path-set-point",             "path-set-point",      ["--asset", "TPrefab1", "--path", "", "--field", "0", "--value", "[0,1,0]"]),
    Test("path-add-branch",            "path-add-branch",     ["--asset", "TPrefab1", "--path", "", "--field", "0", "--value", '{"to":2}']),
    Test("path-set-branch",            "path-set-branch",     ["--asset", "TPrefab1", "--path", "", "--field", "0", "--value", '{"to":3}']),
    Test("path-remove-branch",         "path-remove-branch",  ["--asset", "TPrefab1", "--path", "", "--field", "0"]),
    Test("path-remove-point",          "path-remove-point",   ["--asset", "TPrefab1", "--path", "", "--field", "0"]),

    # === prefab promote-root ===========================================
    Test("prefab-add-promote-child",   "prefab-add-object",   ["--asset", "TPrefab1Dup", "--name", "ToPromote"]),
    Test("prefab-promote-root",        "prefab-promote-root", ["--asset", "TPrefab1Dup", "--path", "ToPromote"]),

    # === scene CRUD ====================================================
    Test("scene-create",               "scene-create",        ["--name", "TScene1"]),
    Test("scene-describe",             "scene-describe",      ["--asset", "TScene1"]),
    Test("scene-duplicate",            "scene-duplicate",     ["--asset", "TScene1", "--name", "TScene1Dup"]),
    Test("scene-rename",               "scene-rename",        ["--asset", "3", "--to", "TScene1Renamed"]),
    Test("scene-set-relpath",          "scene-set-relpath",   ["--asset", "TScene1Renamed", "--value", '"scenes/sub/renamed"']),
    Test("scene-set-conf",             "scene-set-conf",      ["--asset", "TScene1", "--field", "clearColor", "--value", "[0.1,0.2,0.3,1]"]),
    Test("scene-add-object",           "scene-add-object",    ["--asset", "TScene1", "--name", "SObj"]),
    Test("scene-add-object-child",     "scene-add-object",    ["--asset", "TScene1", "--parent", "SObj", "--name", "SObjChild"]),
    Test("scene-set-transform",        "scene-set-transform", ["--asset", "TScene1", "--path", "SObj", "--field", "pos", "--value", "[5,0,0]"]),
    Test("scene-add-component-light",  "scene-add-component", ["--asset", "TScene1", "--path", "SObj", "--comp", "Light"]),
    Test("scene-set-prop",             "scene-set-prop",      ["--asset", "TScene1", "--path", "SObj", "--comp", "Light", "--field", "color", "--value", "[1,1,1,1]"]),
    Test("scene-remove-component",     "scene-remove-component",["--asset", "TScene1", "--path", "SObj", "--comp", "Light"]),
    Test("scene-move-object",          "scene-move-object",   ["--asset", "TScene1", "--path", "SObj/SObjChild", "--parent", ""]),
    Test("scene-add-prefab-instance",  "scene-add-prefab-instance",["--asset", "TScene1", "--from", "TPrefab1", "--parent", "", "--name", "PInst"]),
    Test("scene-find",                 "scene-find",          ["--asset", "TScene1", "--comp", "Path"]),

    # === scene path component =========================================
    Test("scene-add-path-obj",         "scene-add-object",    ["--asset", "TScene1", "--name", "SPathObj"]),
    Test("scene-add-path-comp",        "scene-add-component", ["--asset", "TScene1", "--path", "SPathObj", "--comp", "Path"]),
    Test("scene-path-add-point-1",     "scene-path-add-point",["--asset", "TScene1", "--path", "SPathObj", "--value", "[0,0,0]"]),
    Test("scene-path-add-point-2",     "scene-path-add-point",["--asset", "TScene1", "--path", "SPathObj", "--value", "[20,0,0]"]),
    Test("scene-path-insert-point",    "scene-path-insert-point",["--asset", "TScene1", "--path", "SPathObj", "--field", "1", "--value", "[10,0,0]"]),
    Test("scene-path-set-point",       "scene-path-set-point",["--asset", "TScene1", "--path", "SPathObj", "--field", "0", "--value", "[0,5,0]"]),
    Test("scene-path-add-branch",      "scene-path-add-branch",["--asset", "TScene1", "--path", "SPathObj", "--field", "0", "--value", '{"to":1}']),
    Test("scene-path-set-branch",      "scene-path-set-branch",["--asset", "TScene1", "--path", "SPathObj", "--field", "0", "--value", '{"to":2}']),
    Test("scene-path-remove-branch",   "scene-path-remove-branch",["--asset", "TScene1", "--path", "SPathObj", "--field", "0"]),
    Test("scene-path-remove-point",    "scene-path-remove-point",["--asset", "TScene1", "--path", "SPathObj", "--field", "0"]),

    # === scene layers / vars ==========================================
    Test("scene-add-layer-3d",         "scene-add-layer",     ["--asset", "TScene1", "--type", "3d", "--name", "MainLayer"]),
    Test("scene-set-layer-3d",         "scene-set-layer",     ["--asset", "TScene1", "--type", "3d", "--field", "0", "--value", '{"enabled":true}']),
    Test("scene-remove-layer-3d",      "scene-remove-layer",  ["--asset", "TScene1", "--type", "3d", "--field", "0"]),

    # === scene cleanup =================================================
    Test("scene-remove-object",        "scene-remove-object", ["--asset", "TScene1", "--path", "SObj"]),

    # === canvas-2D shorthand (P0 patch) ===============================
    Test("scene-add-canvas",           "scene-add-object",    ["--asset", "TScene1", "--name", "MyCanvas", "--type", "canvas"]),

    # === scene-duplicate-component (P1) ===============================
    Test("scene-dup-comp-stub-obj",    "scene-add-object",    ["--asset", "TScene1", "--name", "DupHost"]),
    Test("scene-dup-comp-stub-comp",   "scene-add-component", ["--asset", "TScene1", "--path", "DupHost", "--comp", "Light"]),
    Test("scene-duplicate-component",  "scene-duplicate-component", ["--asset", "TScene1", "--path", "DupHost", "--comp", "Light"]),
    Test("scene-dup-comp-missing",     "scene-duplicate-component", ["--asset", "TScene1", "--path", "DupHost", "--comp", "Camera"], expect_fail=True),

    # === scene-extract-prefab (P0 patch) ==============================
    Test("scene-extract-root-stub",    "scene-add-object",    ["--asset", "TScene1", "--name", "Extractable"]),
    Test("scene-extract-prefab",       "scene-extract-prefab",["--asset", "TScene1", "--path", "Extractable"]),
    Test("scene-extract-already-inst", "scene-extract-prefab",["--asset", "TScene1", "--path", "Extractable"], expect_fail=True),
    Test("scene-extract-bad-path",     "scene-extract-prefab",["--asset", "TScene1", "--path", "DoesNotExist"], expect_fail=True),

    Test("scene-delete",               "scene-delete",        ["--asset", "TScene1Renamed"]),

    # === widget ========================================================
    Test("widget-create",              "widget-create",       ["--name", "TWidget1"]),
    Test("widget-describe",            "widget-describe",     ["--asset", "TWidget1"]),
    Test("widget-add-object",          "widget-add-object",   ["--asset", "TWidget1", "--name", "WChild"]),
    Test("widget-set-transform",       "widget-set-transform",["--asset", "TWidget1", "--path", "WChild", "--field", "pos", "--value", "[0,0,0]"]),
    Test("widget-add-component",       "widget-add-component",["--asset", "TWidget1", "--path", "WChild", "--comp", "Button (2D)"]),
    Test("widget-set-prop",            "widget-set-prop",     ["--asset", "TWidget1", "--path", "WChild", "--comp", "Button (2D)", "--field", "width", "--value", "200"]),
    Test("widget-bind-event",          "widget-bind-event",   ["--asset", "TWidget1", "--path", "WChild", "--comp", "Button (2D)", "--to", "READY"]),
    Test("widget-add-variable",        "widget-add-variable", ["--asset", "TWidget1", "--name", "flag", "--type", "bool", "--value", "true"]),
    Test("widget-set-variable-default","widget-set-variable-default",["--asset", "TWidget1", "--name", "flag", "--value", "false"]),
    Test("widget-rename-variable",     "widget-rename-variable",["--asset", "TWidget1", "--from", "flag", "--to", "enabled"]),
    Test("widget-remove-variable",     "widget-remove-variable",["--asset", "TWidget1", "--name", "enabled"]),
    Test("widget-move-object",         "widget-move-object",  ["--asset", "TWidget1", "--path", "WChild", "--parent", ""]),
    Test("widget-remove-component",    "widget-remove-component",["--asset", "TWidget1", "--path", "WChild", "--comp", "Button (2D)"]),
    Test("widget-remove-object",       "widget-remove-object",["--asset", "TWidget1", "--path", "WChild"]),
    Test("widget-promote-root-stub",   "widget-add-object",   ["--asset", "TWidget1", "--name", "WPromote"]),
    Test("widget-promote-root",        "widget-promote-root", ["--asset", "TWidget1", "--path", "WPromote"]),

    # === scripts / code ================================================
    Test("script-create",              "script-create",       ["--name", "TScript"]),
    Test("code-add-function",          "code-add-function",   ["--asset", "TScript", "--func", "tick"]),
    Test("code-list-functions",        "code-list-functions", ["--asset", "TScript"]),
    Test("code-rename-function",       "code-rename-function",["--asset", "TScript", "--from", "tick", "--to", "tock"]),
    Test("code-remove-function",       "code-remove-function",["--asset", "TScript", "--func", "tock"]),

    # === restype / resource ============================================
    Test("restype-create",             "restype-create",      ["--name", "TResType"]),
    Test("restype-add-prop",           "restype-add-prop",    ["--asset", "TResType", "--name", "hp", "--type", "int", "--value", "100"]),
    Test("restype-rename-prop",        "restype-rename-prop", ["--asset", "TResType", "--from", "hp", "--to", "hitpoints"]),
    Test("resource-create",            "resource-create",     ["--restype", "TResType", "--name", "TResInst"]),
    Test("resource-set-prop",          "resource-set-prop",   ["--asset", "TResInst", "--field", "hitpoints", "--value", "75"]),
    Test("restype-remove-prop",        "restype-remove-prop", ["--asset", "TResType", "--name", "hitpoints"]),

    # === graph / material =============================================
    Test("graph-create",               "graph-create",        ["--name", "TGraph"]),
    Test("material-create",            "material-create",     ["--name", "TMat"]),
    Test("material-set-prop",          "material-set-prop",   ["--asset", "TMat", "--field", "dither", "--value", "7"]),

    # === graph node-level ops (P0 patch) ==============================
    Test("graph-list-nodes-empty",     "graph-list-nodes",    ["--asset", "TGraph"]),
    Test("graph-add-node-start",       "graph-add-node",      ["--asset", "TGraph", "--type", "Start"]),
    Test("graph-add-node-wait",        "graph-add-node",      ["--asset", "TGraph", "--type", "Wait", "--value", "[120,0]"]),
    Test("graph-add-node-by-idx",      "graph-add-node",      ["--asset", "TGraph", "--type", "5"]),  # Value node
    Test("graph-list-nodes",           "graph-list-nodes",    ["--asset", "TGraph"]),
    Test("graph-add-bad-type",         "graph-add-node",      ["--asset", "TGraph", "--type", "NotARealNode"], expect_fail=True),
    Test("graph-connect-missing-node", "graph-connect",       ["--asset", "TGraph", "--from", "999:0", "--to", "888:0"], expect_fail=True),
    Test("graph-disconnect-noop",      "graph-disconnect",    ["--asset", "TGraph", "--from", "1:0", "--to", "2:0"], expect_fail=True),

    # === event-graph node-level ops (deferred follow-up landed) =======
    Test("event-graph-list-empty",     "event-graph-list-nodes", ["--asset", "TPrefab1"]),
    Test("event-graph-add-event",      "event-graph-add-node", ["--asset", "TPrefab1", "--type", "Event"]),
    Test("event-graph-add-func",       "event-graph-add-node", ["--asset", "TPrefab1", "--type", "Function", "--value", "[100,40]"]),
    Test("event-graph-add-bad",        "event-graph-add-node", ["--asset", "TPrefab1", "--type", "NotANode"], expect_fail=True),
    Test("event-graph-list",           "event-graph-list-nodes", ["--asset", "TPrefab1"]),
    Test("event-graph-disconnect-noop","event-graph-disconnect", ["--asset", "TPrefab1", "--from", "1:0", "--to", "2:0"], expect_fail=True),

    # === material-graph node-level ops (deferred follow-up landed) ====
    Test("material-graph-list-empty",  "material-graph-list-nodes", ["--asset", "TMat"]),
    Test("material-graph-add-output",  "material-graph-add-node", ["--asset", "TMat", "--type", "Material Output"]),
    Test("material-graph-add-colors",  "material-graph-add-node", ["--asset", "TMat", "--type", "Colors", "--value", "[60,260]"]),
    Test("material-graph-add-by-idx",  "material-graph-add-node", ["--asset", "TMat", "--type", "1"]),  # ColorCombiner
    Test("material-graph-list",        "material-graph-list-nodes", ["--asset", "TMat"]),
    Test("material-graph-add-bad",     "material-graph-add-node", ["--asset", "TMat", "--type", "NotANode"], expect_fail=True),
    Test("material-graph-disc-noop",   "material-graph-disconnect", ["--asset", "TMat", "--from", "1:0", "--to", "2:0"], expect_fail=True),

    # === folders ======================================================
    Test("folder-create",              "folder-create",       ["--path", "myfolder"]),
    Test("folder-rename",              "folder-rename",       ["--path", "myfolder", "--to", "renamedfolder"]),
    Test("folder-move-parent",         "folder-create",       ["--path", "subparent"]),
    Test("folder-move",                "folder-move",         ["--path", "renamedfolder", "--dest", "subparent"]),
    Test("folder-delete",              "folder-delete",       ["--path", "subparent/renamedfolder"]),

    # === asset move / rename / import / delete ========================
    Test("asset-import",               "asset-import",        ["--file", "tests/cli/fixtures/baseline/assets/crate32.png", "--dest", "imported"]),
    Test("asset-rename",               "asset-rename",        ["--asset", "TPrefab1", "--name", "TPrefab1Final"]),
    Test("asset-move",                 "asset-move",          ["--asset", "TPrefab1Final", "--dest", "movedhere"]),
    Test("asset-set-conf-image",       "asset-set-conf",      ["--asset", "crate32.png", "--field", "blender", "--value", "0"]),
    Test("asset-delete",               "asset-delete",        ["--asset", "TPrefab1Dup"]),

    # === project ======================================================
    Test("project-set-conf",           "project-set-conf",    ["--field", "name", "--value", '"SmokeProj"']),

    # project-create bootstraps a fresh project from the empty template.
    # Runs via the dispatchBootstrap path so it doesn't need a pre-existing
    # project. Target lives under the per-run workdir so reruns stay clean.
    Test("project-create",             "project-create",      ["--path", "{WORKDIR}/pc_target", "--name", "SmokeNew"], project="{WORKDIR}/_pc_nope.p64proj"),
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="", help="Run only tests whose name contains this substring.")
    ap.add_argument("--keep-temp", action="store_true", help="Leave the working temp dir on exit.")
    ap.add_argument("--list", action="store_true", help="List test names and exit.")
    ap.add_argument("--stop-on-fail", action="store_true")
    args = ap.parse_args()

    selected = [t for t in TESTS if args.filter in t.name] if args.filter else TESTS

    if args.list:
        for t in selected:
            print(t.name)
        return 0

    if not BASELINE.exists():
        sys.exit(f"smoke: baseline fixture missing at {BASELINE}")

    TEMP_PARENT.mkdir(parents=True, exist_ok=True)
    workdir = Path(tempfile.mkdtemp(prefix="run_", dir=str(TEMP_PARENT)))
    fixture = workdir / "fixture"
    shutil.copytree(BASELINE, fixture)
    print(f"smoke: temp workdir = {workdir}")

    pass_count = 0
    fail_count = 0
    failures: List[tuple[str, str]] = []

    t0 = time.time()
    for t in selected:
        if t.reset:
            shutil.rmtree(fixture, ignore_errors=True)
            shutil.copytree(BASELINE, fixture)
        ok, detail = run_test(
            Test(name=t.name, cmd=t.cmd, args=t.args, expect_json=t.expect_json,
                 expect_fail=t.expect_fail, reset=t.reset, project=t.project),
            fixture
        )
        status = "PASS" if ok else "FAIL"
        print(f"  {status:4}  {t.name:36}  {detail}")
        if ok:
            pass_count += 1
        else:
            fail_count += 1
            failures.append((t.name, detail))
            if args.stop_on_fail:
                break

    elapsed = time.time() - t0
    print()
    print(f"smoke: {pass_count} pass, {fail_count} fail in {elapsed:.1f}s")
    if failures:
        print("smoke: failed tests:")
        for name, detail in failures:
            print(f"  - {name}: {detail}")

    if not args.keep_temp:
        shutil.rmtree(workdir, ignore_errors=True)
    else:
        print(f"smoke: kept temp dir at {workdir}")

    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
