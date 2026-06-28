# Quick physics accuracy corpus

Hard subset for iterative physics work:
- All `golden_physics_battles` JSON
- All `leaderboard_physics_divergences` JSON
- Known remaining fails (expected_fail + divergences)

Target: **100% battle outcome** then **100% turn-by-turn** (pos/vel within 1 unit).

```bash
bazel build //src/physics:replay_driver && cp -f bazel-bin/src/physics/replay_driver sim/replay_driver
python3 sim/verify_quick_accuracy.py
```
