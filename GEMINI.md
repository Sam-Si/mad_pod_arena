# Mad Pod Arena — AI coding conventions

**Ownership law (authoritative):** [`docs/SSOT.md`](docs/SSOT.md)  
**Merge gate law:** [`docs/VERIFICATION_TRUTH_POLICY.md`](docs/VERIFICATION_TRUTH_POLICY.md)  
**Behavioral truth (run this):** `./tools/run_truth_suite.sh`  
Human overview: [`README.md`](README.md).

## Repo map

```
src/physics/   ★ SSOT: Fidelity (csb::Game in physics.h) + GA search collision (csb::fast in fast.h)
               Optional exact-rollout API: fast_physics.h (must EXACT-match Fidelity; not a second design owner)
src/engine/    Arena, degrees Pod / ApplyGAAction, IBot (no collision math)
src/cg/        GA bot; library ga_bot; CG paste = generated amalgam only
src/core/      constants.h (numeric law), maps, progress
src/tournament Benchmarks via CreateGABot
sim/           Python gate + diagnostics
battles/       Replay corpora + retention
third_party/   Read-only external referees
docs/          Active: SSOT, verification policy, rules (+ archive/ history)
tools/         Bazel helpers (e.g. golden capture, optional validators)
```

## Physics ownership (do not invent a third copy)

| Concern | Edit only here |
|---|---|
| CG / gate / arena world step | `src/physics/physics.h` (`csb::Game`) |
| GA search collision fragment | `src/physics/fast.h` (`csb::fast::SimulateTurn`) |
| Numeric constants | `src/core/constants.h` (alias elsewhere; no new magic numbers) |
| Bot search / eval / CG loop | `src/cg/cg_bot.cpp` + `ga_bot.h` — **no collision math** |
| CG IDE paste | **Generated** `//src/cg:cg_bot_amalgam` only — never hand-fork a second submission file |

- `Game::step(Fast)` is **unsupported** (use `csb::fast` for search).
- Engine has **no** collision implementation; use `FastSimulateTurn` → `csb::fast`.
- Do **not** reintroduce `PhysicsSimulator` / `GAPhysicsSimulator` (deleted).

## Shield / boost (quick facts)

- Shield: `shield_cd` / `shieldtimer = 4` on activation; mass heavy only on activation frame; no thrust while cooldown &gt; 0; decrement in end-of-turn.
- Boost: per-pod once at 650 (Fidelity), then degrades to 200.

## Build & gate (local = CI intent)

```bash
bazel build //...
bazel test --config=ci //src/physics:test_physics

bazel build //src/physics:replay_driver
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
python3 sim/check_verification_policy.py
```

### CodinGame submission (separate from physics gate)

```bash
bazel build //src/cg:cg_bot_amalgam //src/cg:cg_bot_amalgam_bin
bazel test //src/cg:amalgam_fast_smoke_test
# Paste: bazel-bin/src/cg/cg_bot_amalgam.cpp
```

## Hygiene

- Repo-relative paths only.
- Do not commit `sim/replay_driver`, `logs/`, or build trees.
- Timeout corpora under `*_timeouts/` are not physics fails.
- Prefer golden + gate corpora over full leaderboard for day-to-day work.

## Curriculum / deep history

- Active SSOT plan notes: [`docs/artifacts/SSOT_TOP3_AND_CG_WORKFLOW.md`](docs/artifacts/SSOT_TOP3_AND_CG_WORKFLOW.md)
- Historical only: [`docs/archive/`](docs/archive/)
