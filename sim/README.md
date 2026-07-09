# `sim/` — Python physics verification

Drives `src/physics/physics.h` via Bazel `//src/physics:replay_driver` against `battles/`.

## Merge gate (required)

```bash
bazel build //src/physics:replay_driver //src/physics:test_physics
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver

MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
python3 sim/check_verification_policy.py
```

Policy: [`docs/VERIFICATION_TRUTH_POLICY.md`](../docs/VERIFICATION_TRUTH_POLICY.md).

## Supported tools

| Script | Role |
|---|---|
| `verify_battles.py` | Gate (A) with `--gate`; diagnostic batch otherwise |
| `battles/scripts/verify_golden_corpus.py` | Gate (B) `--tier pass` |
| `check_verification_policy.py` | Static CI policy checks |
| `compare_battle.py` | Single-battle debug; **`--exact`** for zero-tol pos/vel/timeout (diag) |
| `verify_quick_accuracy.py` | Outcomes + turn stats (research) |
| `validate_fast_physics_corpus.py` | Stream battle corpora → C++ Fidelity vs `fast_physics` **EXACT** |
| `battle_parser.py` / `physics_driver.py` / `tolerance_policy.py` / `compare_util.py` | Libraries |

### Fidelity vs `fast_physics` (EXACT parity + speed)

```bash
# Build (Bazel preferred)
bazel build -c opt //src/physics:validate_fast_physics_battles //src/physics:bench_fast_physics

# Or ad-hoc:
g++ -std=c++17 -O3 -DNDEBUG -I src/physics \
  src/physics/validate_fast_physics_battles.cpp -o sim/validate_fast_physics_battles

# Full corpora (includes all leaderboard root battles — slow)
python3 sim/validate_fast_physics_corpus.py --all-leaderboard

# Default: gate+golden+divergences+quick+copy_pasted + 400 LB sample
python3 sim/validate_fast_physics_corpus.py

# Speed + stress EXACT (see //src/physics:bench_fast_physics)
bazel run -c opt //src/physics:bench_fast_physics
```

`EXACT_*` tolerances (angle ε only): `tolerance_policy.py`.

## Legacy

[`legacy/`](legacy/) — older one-off scripts (not used by CI).
