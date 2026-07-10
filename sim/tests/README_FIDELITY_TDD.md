# Fidelity failure TDD suite

## Iron law
Fixtures and tests were written **before** claiming new physics passes them.
Run `python3 sim/tests/test_fidelity_failures_tdd.py` — must show RED on unpaid debt.

## INPUT / EXPECTED (each battle fixture JSON)

| Field | Role |
|---|---|
| `initial.pods[]` + timeouts | **INPUT** seed (`SET_POD` / `SET_TIMEOUTS`) |
| `checkpoints`, `laps` | **INPUT** track |
| `turns[t].actions[4]` | **INPUT** `{tx,ty,thrust}` per pod (invalid-thrust propagation applied) |
| `turns[t].expected` | **EXPECTED** CG keyframe: pods `x,y,vx,vy,angle,next_cp` + timeouts |
| `tolerances` | GATE: pos≤5, vel≤3, ang≤1°, timeout≤1, exact local `next_cp` |

## Cases (10 curriculum divergences)

See `fixtures/fidelity_failures/MANIFEST.json`.

## Acceptance

- Each `test_battle_*_turn_perfect_GATE` must pass (full battle turn-perfect).
- Aggregate turn rate ≥ **99.9%** across all fixture turns.
- Gate A must remain 312/312 (run separately in CI / verify_battles).

## Status

Track with the suite itself; do not trust stale markdown over `test_fidelity_failures_tdd.py` output.
