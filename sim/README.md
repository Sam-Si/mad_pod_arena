# `sim/` — Python physics verification harness

Drives `src/physics/` against real battle JSONs in `battles/`.

## Main entry points

| Script | Purpose |
|---|---|
| `verify_battles.py` | Batch: whole directory of battles, pass/fail summary |
| `compare_battle.py` | Single battle, turn-by-turn diff for debugging |
| `battle_parser.py` | Parse CG battle JSON → init state, actions, ground truth |
| `physics_driver.py` | Subprocess wrapper around `replay_driver` (auto-builds via g++) |
| `validate.py` | Extra validation helpers |
| `extract_ground_truth.py` | Pull referee state frames from a battle |
| `analyze_all_predictions.py` | Bulk analysis helper |

## Typical usage

```bash
# From repo root
python3 sim/verify_battles.py battles/test_session_battles
python3 sim/compare_battle.py battles/test_session_battles/battle_891669739.json
```

The driver source lives at `src/physics/replay_driver.cpp` (includes `physics.h`).
`physics_driver.py` compiles it on demand into `sim/replay_driver` (local binary, not committed).

Alternatively:

```bash
bazel build //src/physics:replay_driver
# binary at bazel-bin/src/physics/replay_driver
```

## Relationship to C++ verifier

`src/physics/verify_battles.cpp` is the all-C++ path (faster on large corpora).
`sim/` is better for interactive debugging and iterating on parser/driver protocol.
