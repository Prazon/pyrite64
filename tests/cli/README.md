# CLI smoketests

`run.py` exercises every `--cli --cmd <name>` command against a committed
fixture project, asserting exit code and (where applicable) that stdout
parses as JSON.

## Layout

- `fixtures/baseline/` — a minimal `.p64proj` (derived from `n64/examples/empty`)
  used as the read-only template. Each run copies this to a temp workdir
  under `build/cli_smoke/run_*/fixture/` and mutates the copy.
- `run.py` — the harness + ordered test list.
- `AUDIT.md` — point-in-time audit of GUI vs CLI parity. See its last
  section for the gaps this session's CLI additions covered, and the
  follow-up gaps still open.

## Run

```bash
python3 tests/cli/run.py                # all tests
python3 tests/cli/run.py --filter graph # substring filter
python3 tests/cli/run.py --keep-temp    # leave temp dir for inspection
python3 tests/cli/run.py --list         # names only
python3 tests/cli/run.py --stop-on-fail
```

The harness invokes `./pyrite64.exe` (built into the repo root) and uses
repo-relative paths because the binary is a Windows .exe and can't parse
WSL-style `/mnt/...` paths. Workdirs are parented under `build/`
(gitignored) so the same path resolves the same way from WSL and Windows.

## When tests break

- After changing on-disk asset schema, rebuild the baseline:
  `rm -rf tests/cli/fixtures/baseline && cp -r n64/examples/empty tests/cli/fixtures/baseline && rm -rf tests/cli/fixtures/baseline/{build,filesystem,Makefile,p64_project.z64}`.
- After adding a new CLI command, add a test entry to `TESTS` in
  `run.py` so coverage stays at 100%.
