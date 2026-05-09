#!/usr/bin/env bash
# Editor-side smoke test: builds the asset/scene tables for every example
# project via `--cli --cmd build-tables` (skipping the final `make` step,
# so this does NOT need the N64 toolchain). Catches regressions in the
# table generators, asset pipeline, scene serializer, code/graph builders.
#
# Run from the repo root after `cmake --build --preset ...`. Exits non-zero
# on the first failed example so it's safe to chain after a build command.
set -u

cd "$(dirname "$0")/.."

EXE="./pyrite64.exe"
if [ ! -x "$EXE" ]; then
  EXE="./pyrite64"
fi
if [ ! -x "$EXE" ]; then
  echo "smoke_test: pyrite64 binary not found at repo root" >&2
  exit 2
fi

# pyrite64's `/pyrite64-sdk` Windows fallback only resolves from C: drive,
# so on this fork (B: drive) the asset converters fail unless N64_INST is set
# explicitly. Use the conventional MSYS2 install location if the user hasn't
# already exported one.
: "${N64_INST:=C:\\msys64\\pyrite64-sdk}"
export N64_INST

PROJECTS=(
  "n64/examples/empty/project.p64proj"
  "n64/examples/bigtex/project.p64proj"
  "n64/examples/material_test/project.p64proj"
  "n64/examples/jam25/project.p64proj"
)

failed=0
for proj in "${PROJECTS[@]}"; do
  printf -- "--- %s ---\n" "$proj"
  if "$EXE" --cli --cmd build-tables "$proj"; then
    printf -- "PASS  %s\n\n" "$proj"
  else
    printf -- "FAIL  %s\n\n" "$proj" >&2
    failed=$((failed + 1))
  fi
done

if [ "$failed" -gt 0 ]; then
  echo "smoke_test: $failed example(s) failed" >&2
  exit 1
fi
echo "smoke_test: all ${#PROJECTS[@]} examples passed"
