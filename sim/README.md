# `sim/` — Python physics verification harness

Drives `src/physics/physics.h` via `replay_driver` against battle JSONs in `battles/`.

**Policy:** [`docs/VERIFICATION_TRUTH_POLICY.md`](../docs/VERIFICATION_TRUTH_POLICY.md) — **`MERGE_PHYSICS_OK`** vs diagnostics.

## Gate vs diagnostic

| Command | Role |
|---|---|
| `MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles` | **Gate (A)** — part of `MERGE_PHYSICS_OK` |
| `MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass` | **Gate (B)** |
| `python3 sim/verify_battles.py <other-dir>` (no `--gate`) | **DIAGNOSTIC** batch |
| `python3 sim/compare_battle.py <battle.json>` | **DIAGNOSTIC** single battle (`EXPLORE_*`; `--gate-tolerances` for `GATE_*`) |
| C++ `//src/physics:verify_battles` | **DIAGNOSTIC** (stricter) |

Tolerances: [`tolerance_policy.py`](tolerance_policy.py) (`GATE_*` / `EXPLORE_*`). Helpers: [`compare_util.py`](compare_util.py). Static CI checks: [`check_verification_policy.py`](check_verification_policy.py).

## Driver

Prefer:

```bash
bazel build //src/physics:replay_driver
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver
```

`ensure_driver_built()` in `physics_driver.py`: if `sim/replay_driver` exists it is used **without** mtime auto-rebuild — rebuild/copy after editing `physics.h`. Set `MAD_POD_GATE_STRICT=1` (CI) to fail if the binary is missing. Env `MAD_POD_REPLAY_DRIVER` overrides (cwd-relative paths OK).

## Typical usage

```bash
# From repo root — gate A
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles

python3 sim/compare_battle.py battles/test_session_battles/battle_891669739.json
python3 sim/check_verification_policy.py
```
