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
    # After a passing run, parse the JSON output and store named values
    # for subsequent {VARNAME} substitutions in later test args.
    # Each entry maps an exported variable name to a dotted path within
    # the response JSON (e.g. "added" or "obj.uuid").
    captures: dict = field(default_factory=dict)


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


def run_test(test: Test, workdir: Path, captures_env: dict) -> tuple[bool, str]:
    workdir_rel = to_repo_rel(workdir)
    def expand(s: str) -> str:
        s = s.replace("{WORKDIR}", workdir_rel)
        for k, v in captures_env.items():
            s = s.replace("{" + k + "}", str(v))
        return s
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

    parsed = None
    if test.expect_json:
        body = strip_prelude(res.stdout)
        if not body.strip():
            return False, "no JSON body in stdout"
        try:
            parsed = json.loads(body)
        except json.JSONDecodeError as e:
            return False, f"invalid JSON: {e} | body[:200]={body[:200]!r}"

    if test.captures:
        if parsed is None and test.expect_json:
            return False, "captures requested but no JSON body"
        for var, path in test.captures.items():
            cur = parsed
            for part in path.split("."):
                if cur is None:
                    return False, f"capture {var}: path {path!r} missed at {part!r}"
                if isinstance(cur, list):
                    try:
                        cur = cur[int(part)]
                    except (ValueError, IndexError):
                        return False, f"capture {var}: bad list index {part!r}"
                else:
                    cur = cur.get(part) if isinstance(cur, dict) else None
            if cur is None:
                return False, f"capture {var}: path {path!r} resolved to None"
            captures_env[var] = cur

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
    # model3d list doubles as a headless scanner for the N64 S10.5 UV-range
    # diagnostic (uvOutOfRange flag); in-range fixtures must NOT carry it.
    Test("asset-list-model3d",         "asset-list",          ["--type", "model3d"]),
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
    Test("prefab-duplicate-variable",  "prefab-duplicate-variable", ["--asset", "TPrefab1", "--from", "velocity", "--to", "velocityCopy"]),
    Test("prefab-remove-variable",     "prefab-remove-variable",["--asset", "TPrefab1", "--name", "velocity"]),

    # === prefab variant (Blueprint-Actor inheritance) ==================
    Test("prefab-variant",             "prefab-variant",      ["--parent", "TPrefab1", "--name", "TPrefabVar"]),
    Test("prefab-describe-inheritance","prefab-describe-inheritance", ["--asset", "TPrefabVar"]),

    # Structural overrides on the inherited tree. TPrefab1 left Leaf at the
    # root after prefab-remove-object Child (see above), so Leaf is the
    # canonical inherited child we can hang overrides off.
    Test("prefab-override-prop-pos",   "prefab-override-prop",
         ["--asset", "TPrefabVar", "--path", "Leaf", "--field", "pos", "--value", "[9,9,9]"]),
    Test("prefab-describe-after-override", "prefab-describe-inheritance",
         ["--asset", "TPrefabVar"]),
    Test("prefab-reset-prop-pos",      "prefab-reset-prop",
         ["--asset", "TPrefabVar", "--path", "Leaf", "--field", "pos"]),

    # Variable default-value overrides. velocityCopy is the variable that
    # survives the rename/remove flow above (still on TPrefab1).
    Test("prefab-override-var-default","prefab-override-var-default",
         ["--asset", "TPrefabVar", "--name", "velocityCopy", "--value", "12.0"]),
    Test("prefab-reset-var-default",   "prefab-reset-var-default",
         ["--asset", "TPrefabVar", "--name", "velocityCopy"]),

    # Inherited-removal: drop the inherited Leaf entirely on the variant.
    Test("prefab-remove-inherited-object","prefab-remove-inherited-object",
         ["--asset", "TPrefabVar", "--path", "Leaf"]),

    # Deprecation shims for the old RFC-6902 patch CLI — must now fail
    # with a useful error so old scripts get pointed at the new commands.
    Test("prefab-list-patches-removed", "prefab-list-patches",
         ["--asset", "TPrefabVar"], expect_json=False, expect_fail=True),
    Test("prefab-add-patch-removed",   "prefab-add-patch",
         ["--asset", "TPrefabVar", "--value", '{"op":"test"}'],
         expect_json=False, expect_fail=True),
    Test("prefab-remove-patch-removed","prefab-remove-patch",
         ["--asset", "TPrefabVar", "--field", "0"], expect_json=False, expect_fail=True),

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

    # === path-follow component (prefab) ================================
    Test("component-describe-pathfollow","component-describe", ["--comp", "PathFollow"]),
    Test("prefab-add-pathfollow-comp", "prefab-add-component",["--asset", "TPrefab1", "--path", "", "--comp", "PathFollow"]),
    Test("prefab-set-pathfollow-speed","prefab-set-prop",
         ["--asset", "TPrefab1", "--comp", "PathFollow", "--field", "speed", "--value", "150"]),
    Test("prefab-set-pathfollow-mode", "prefab-set-prop",
         ["--asset", "TPrefab1", "--comp", "PathFollow", "--field", "mode", "--value", "1"]),

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
    Test("scene-add-layer-dup-src",    "scene-add-layer",     ["--asset", "TScene1", "--type", "3d", "--name", "ToDup"]),
    Test("scene-duplicate-layer",      "scene-duplicate-layer",["--asset", "TScene1", "--type", "3d", "--field", "1", "--name", "DupLayer"]),
    Test("scene-reset-layers",         "scene-reset-layers",  ["--asset", "TScene1"]),
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
    Test("restype-duplicate-prop",     "restype-duplicate-prop", ["--asset", "TResType", "--from", "hp", "--to", "armor"]),
    Test("restype-rename-prop",        "restype-rename-prop", ["--asset", "TResType", "--from", "hp", "--to", "hitpoints"]),
    Test("resource-create",            "resource-create",     ["--restype", "TResType", "--name", "TResInst"]),
    Test("resource-set-prop",          "resource-set-prop",   ["--asset", "TResInst", "--field", "hitpoints", "--value", "75"]),
    Test("restype-remove-prop",        "restype-remove-prop", ["--asset", "TResType", "--name", "hitpoints"]),

    # === graph / material =============================================
    Test("graph-create",               "graph-create",        ["--name", "TGraph"]),
    Test("material-create",            "material-create",     ["--name", "TMat"]),
    Test("material-set-prop",          "material-set-prop",   ["--asset", "TMat", "--field", "dither", "--value", "7"]),
    Test("particle-system-create",     "particle-system-create", ["--name", "TPtx"]),

    # === save file (.p64save) =========================================
    Test("save-file-create",           "save-file-create",       ["--name", "TSave"]),
    Test("save-file-list",             "save-file-list"),
    Test("save-file-add-int",          "save-file-add-field",    ["--asset", "TSave", "--field", "playerLevel", "--type", "Int", "--value", "1"]),
    Test("save-file-add-float",        "save-file-add-field",    ["--asset", "TSave", "--field", "bestTime", "--type", "Float", "--value", "0.0"]),
    Test("save-file-add-bool",         "save-file-add-field",    ["--asset", "TSave", "--field", "tutorialDone", "--type", "Bool", "--value", "false"]),
    Test("save-file-add-string",       "save-file-add-field",    ["--asset", "TSave", "--field", "playerName", "--type", "String", "--value", "\"AAA\""]),
    Test("save-file-add-vec3",         "save-file-add-field",    ["--asset", "TSave", "--field", "spawnPos", "--type", "Vec3", "--value", "[0,1,0]"]),
    Test("save-file-describe",         "save-file-describe",     ["--asset", "TSave"]),
    Test("save-file-set-field-rename", "save-file-set-field",    ["--asset", "TSave", "--field", "playerLevel", "--to", "level", "--value", "5"]),
    Test("save-file-remove-field",     "save-file-remove-field", ["--asset", "TSave", "--field", "tutorialDone"]),
    Test("save-file-add-bad-type",     "save-file-add-field",    ["--asset", "TSave", "--field", "x", "--type", "Garbage"], expect_fail=True),
    Test("save-file-add-dup",          "save-file-add-field",    ["--asset", "TSave", "--field", "level", "--type", "Int"], expect_fail=True),
    Test("save-file-remove-missing",   "save-file-remove-field", ["--asset", "TSave", "--field", "doesnotexist"], expect_fail=True),

    # === graph node-level ops (P0 patch) ==============================
    Test("graph-list-nodes-empty",     "graph-list-nodes",    ["--asset", "TGraph"]),
    Test("graph-add-node-start",       "graph-add-node",      ["--asset", "TGraph", "--type", "Start"]),
    Test("graph-add-node-wait",        "graph-add-node",      ["--asset", "TGraph", "--type", "Wait", "--value", "[120,0]"]),
    Test("graph-add-node-by-idx",      "graph-add-node",      ["--asset", "TGraph", "--type", "5"]),  # Value node
    Test("graph-list-nodes",           "graph-list-nodes",    ["--asset", "TGraph"]),
    Test("graph-add-bad-type",         "graph-add-node",      ["--asset", "TGraph", "--type", "NotARealNode"], expect_fail=True),
    Test("graph-connect-missing-node", "graph-connect",       ["--asset", "TGraph", "--from", "999:0", "--to", "888:0"], expect_fail=True),
    Test("graph-disconnect-noop",      "graph-disconnect",    ["--asset", "TGraph", "--from", "1:0", "--to", "2:0"], expect_fail=True),

    # === graph: set-node-prop / duplicate-node / set-node-pos / compile ==
    Test("graph-anchor-wait-node",     "graph-add-node",      ["--asset", "TGraph", "--type", "Wait", "--value", "[40,40]"],
         captures={"GRAPH_WAIT_UUID": "added"}),
    Test("graph-set-node-prop-time",   "graph-set-node-prop", ["--asset", "TGraph", "--parent", "{GRAPH_WAIT_UUID}", "--field", "time", "--value", "2.5"]),
    Test("graph-set-node-prop-reject", "graph-set-node-prop", ["--asset", "TGraph", "--parent", "{GRAPH_WAIT_UUID}", "--field", "uuid", "--value", "0"], expect_fail=True),
    Test("graph-set-node-pos",         "graph-set-node-pos",  ["--asset", "TGraph", "--parent", "{GRAPH_WAIT_UUID}", "--value", "[300,100]"]),
    Test("graph-duplicate-node",       "graph-duplicate-node",["--asset", "TGraph", "--parent", "{GRAPH_WAIT_UUID}"]),
    # graph-compile on the now-rich TGraph catches the unwired Wait/Value
    # nodes via real pin-style reachability (proves the caveat-1 fix —
    # validate() runs against a deserialized ImNodeFlow graph, not just
    # the JSON walker).
    Test("graph-compile-unreachable",  "graph-compile",       ["--asset", "TGraph"], expect_fail=True),
    # Clean-room positive: a fresh graph with just a Start node compiles green.
    Test("graph-compile-clean-make",   "graph-create",        ["--name", "TGraphClean"]),
    Test("graph-compile-clean-start",  "graph-add-node",      ["--asset", "TGraphClean", "--type", "Start"]),
    Test("graph-compile-clean-ok",     "graph-compile",       ["--asset", "TGraphClean"]),

    # === event-graph node-level ops (deferred follow-up landed) =======
    Test("event-graph-list-empty",     "event-graph-list-nodes", ["--asset", "TPrefab1"]),
    Test("event-graph-add-event",      "event-graph-add-node", ["--asset", "TPrefab1", "--type", "Event"]),
    Test("event-graph-add-func",       "event-graph-add-node", ["--asset", "TPrefab1", "--type", "Function", "--value", "[100,40]"]),
    Test("event-graph-add-bad",        "event-graph-add-node", ["--asset", "TPrefab1", "--type", "NotANode"], expect_fail=True),
    Test("event-graph-list",           "event-graph-list-nodes", ["--asset", "TPrefab1"]),
    Test("event-graph-disconnect-noop","event-graph-disconnect", ["--asset", "TPrefab1", "--from", "1:0", "--to", "2:0"], expect_fail=True),

    # === event-graph: set-node-prop / duplicate-node / set-node-pos / compile / convenience ===
    Test("eg-add-event-anchor",        "event-graph-add-node",["--asset", "TPrefab1", "--type", "Event"],
         captures={"EG_EVENT_UUID": "added"}),
    Test("eg-set-node-pos",            "event-graph-set-node-pos", ["--asset", "TPrefab1", "--parent", "{EG_EVENT_UUID}", "--value", "[40,40]"]),
    Test("eg-set-node-prop",           "event-graph-set-node-prop",["--asset", "TPrefab1", "--parent", "{EG_EVENT_UUID}", "--field", "eventKind", "--value", "0"]),
    Test("eg-duplicate-node",          "event-graph-duplicate-node",["--asset", "TPrefab1", "--parent", "{EG_EVENT_UUID}"]),
    # eg-compile expects failure because the earlier "event-graph-add-func"
    # test added a Function node without wiring it to any entry node —
    # real validate() catches that as unreachable (proving pin-style
    # reachability works headlessly now, post-caveat-1 fix).
    Test("eg-compile-unreachable",     "event-graph-compile", ["--asset", "TPrefab1"], expect_fail=True),
    # Clean-room positive: a fresh prefab with one PrefabEvent node compiles green.
    Test("eg-compile-clean-make",      "prefab-create",       ["--name", "TPrefabClean"]),
    Test("eg-compile-clean-event",     "event-graph-add-node",["--asset", "TPrefabClean", "--type", "Event"]),
    Test("eg-compile-clean-ok",        "event-graph-compile", ["--asset", "TPrefabClean"]),

    Test("eg-prefab-var-stub",         "prefab-add-variable", ["--asset", "TPrefab1", "--name", "egTestVar", "--type", "int", "--value", "0"]),
    Test("eg-add-var-get",             "event-graph-add-var-get", ["--asset", "TPrefab1", "--name", "egTestVar", "--value", "[200,0]"]),
    Test("eg-add-var-get-missing",     "event-graph-add-var-get", ["--asset", "TPrefab1", "--name", "doesNotExist"], expect_fail=True),
    Test("eg-add-func-call",           "event-graph-add-func-call",["--asset", "TPrefab1", "--func", "onTick"]),

    # === material-graph node-level ops (deferred follow-up landed) ====
    Test("material-graph-list-empty",  "material-graph-list-nodes", ["--asset", "TMat"]),
    Test("material-graph-add-output",  "material-graph-add-node", ["--asset", "TMat", "--type", "Material Output"]),
    Test("material-graph-add-colors",  "material-graph-add-node", ["--asset", "TMat", "--type", "Colors", "--value", "[60,260]"]),
    Test("material-graph-add-by-idx",  "material-graph-add-node", ["--asset", "TMat", "--type", "1"]),  # ColorCombiner
    Test("material-graph-list",        "material-graph-list-nodes", ["--asset", "TMat"]),
    Test("material-graph-add-bad",     "material-graph-add-node", ["--asset", "TMat", "--type", "NotANode"], expect_fail=True),
    Test("material-graph-disc-noop",   "material-graph-disconnect", ["--asset", "TMat", "--from", "1:0", "--to", "2:0"], expect_fail=True),

    # === material-graph: set-node-prop / duplicate-node / set-node-pos ===
    Test("mg-anchor-colors-node",      "material-graph-add-node", ["--asset", "TMat", "--type", "Colors"],
         captures={"MG_COLORS_UUID": "added"}),
    Test("mg-set-node-pos",            "material-graph-set-node-pos", ["--asset", "TMat", "--parent", "{MG_COLORS_UUID}", "--value", "[120,200]"]),
    Test("mg-set-node-prop",           "material-graph-set-node-prop",["--asset", "TMat", "--parent", "{MG_COLORS_UUID}", "--field", "setPrim", "--value", "true"]),
    Test("mg-duplicate-node",          "material-graph-duplicate-node",["--asset", "TMat", "--parent", "{MG_COLORS_UUID}"]),

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
    Test("project-set-coll-layer",     "project-set-collision-layer", ["--field", "3", "--name", "Hazard"]),
    Test("project-set-coll-bad-idx",   "project-set-collision-layer", ["--field", "9", "--name", "Out"], expect_fail=True),
    Test("project-reset-conf",         "project-reset-conf",  ["--field", "name"]),
    Test("project-reset-conf-bad",     "project-reset-conf",  ["--field", "nope"], expect_fail=True),
    Test("prefs-describe",             "prefs-describe"),
    Test("prefs-set",                  "prefs-set",           ["--field", "moveSpeed", "--value", "200"]),
    Test("prefs-reset",                "prefs-reset",         ["--field", "moveSpeed"]),
    Test("prefs-reset-bad",            "prefs-reset",         ["--field", "nope"], expect_fail=True),

    # project-create bootstraps a fresh project from the empty template.
    # Runs via the dispatchBootstrap path so it doesn't need a pre-existing
    # project. Target lives under the per-run workdir so reruns stay clean.
    Test("project-create",             "project-create",      ["--path", "{WORKDIR}/pc_target", "--name", "SmokeNew"], project="{WORKDIR}/_pc_nope.p64proj"),
    # Template discovery + creation from a non-empty example template.
    Test("project-templates",          "project-templates",   project="{WORKDIR}/_pc_nope.p64proj"),
    Test("project-create-template",    "project-create",      ["--path", "{WORKDIR}/pc_tmpl", "--name", "SmokeTmpl", "--template", "bigtex"], project="{WORKDIR}/_pc_nope.p64proj"),
    Test("project-create-bad-template","project-create",      ["--path", "{WORKDIR}/pc_bad", "--name", "SmokeBad", "--template", "nope"], project="{WORKDIR}/_pc_nope.p64proj", expect_fail=True),
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
    captures_env: dict = {}
    for t in selected:
        if t.reset:
            shutil.rmtree(fixture, ignore_errors=True)
            shutil.copytree(BASELINE, fixture)
            captures_env.clear()
        ok, detail = run_test(t, fixture, captures_env)
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
