# Mad Pod Arena

Competitive **Genetic Algorithm** bot for CodinGame
[Mad Pod Racing](https://www.codingame.com/multiplayer/bot-programming/mad-pod-racing)
(*Coders Strike Back*), plus a **referee-faithful** C++ physics engine verified against real battle replays.

| | |
|---|---|
| **Physics SSOT** | [`docs/SSOT.md`](docs/SSOT.md) — Fidelity [`physics.h`](src/physics/physics.h) · search [`fast.h`](src/physics/fast.h) · exact-rollout [`fast_physics.h`](src/physics/fast_physics.h) (must match Fidelity) |
| **Merge gate** | Job id **`physics-accuracy`** — see [`docs/VERIFICATION_TRUTH_POLICY.md`](docs/VERIFICATION_TRUTH_POLICY.md) |
| **Agent notes** | [`GEMINI.md`](GEMINI.md) |

---

## Repository layout

```text
mad_pod_arena/
├── src/                 First-party C++ (Bazel)
│   ├── physics/         Fidelity Game + Fast (csb::fast) + replay_driver
│   ├── engine/          Arena, degrees Pod / ApplyGAAction (no collision math)
│   ├── cg/              GA bot + ga_bot library / amalgam
│   ├── core/            constants, maps catalog, progress
│   └── tournament/      Self-play benchmarks
├── sim/                 Python verification harness (gate + diagnostics)
├── battles/             Replay corpora + golden tier + retention scripts
├── docs/                Active policy/rules/SSOT (+ archive/)
├── third_party/         Read-only referee references
├── tools/               Bazel helpers, golden capture
└── .github/workflows/   CI (retention, Bazel, physics gate)
```

---

## Quick start

```bash
./setup.sh   # or ./setup-ubuntu.sh
bazel build //...
bazel test //src/physics:test_physics //src/engine:arena_fidelity_trace_test
```

### Behavioral truth suite (run before merge)

```bash
./tools/run_truth_suite.sh          # full: policies + unit + EXACT + CG export + gate A/B
./tools/run_truth_suite.sh --quick  # skip full gate A/B while iterating structure
```

**If the truth suite is green, observable behavior is OK** (Fowler / self-testing code). Docs explain; tests decide.

### Physics merge gate (local = CI)

```bash
bazel build //src/physics:replay_driver //src/physics:test_physics
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver

bazel test --config=ci //src/physics:test_physics
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
python3 sim/check_verification_policy.py
python3 battles/scripts/enforce_retention.py --truncated
```

| Gate | Command | Corpus |
|---|---|---|
| **(U)** | `bazel test //src/physics:test_physics` | Fast goldens + unit smoke |
| **(A)** | `verify_battles.py --gate` | `battles/test_session_battles` (~312) **100%** |
| **(B)** | `verify_golden_corpus.py --tier pass` | Golden **pass** tier (~188) **100%** |

Tolerances (`GATE_*`): pos ≤ 5, vel ≤ 3, angle ≤ 1°, timeout ≤ 1, exact `next_cp` — [`sim/tolerance_policy.py`](sim/tolerance_policy.py).

### Bot / tournament

```bash
bazel build //src/cg:cg_bot //src/cg:cg_bot_standalone
bazel run //src/tournament:benchmark_tournament
```

### CodinGame submission (not the physics merge gate)

```bash
bazel build //src/cg:cg_bot_amalgam //src/cg:cg_bot_amalgam_bin
bazel test //src/cg:amalgam_fast_smoke_test
# Paste file: bazel-bin/src/cg/cg_bot_amalgam.cpp
```

See [`src/cg/`](src/cg/) and [`docs/artifacts/SSOT_TOP3_AND_CG_WORKFLOW.md`](docs/artifacts/SSOT_TOP3_AND_CG_WORKFLOW.md).

---

## Documentation

| Doc | Purpose |
|---|---|
| [`docs/VERIFICATION_TRUTH_POLICY.md`](docs/VERIFICATION_TRUTH_POLICY.md) | Merge gate law (authoritative) |
| [`docs/SSOT.md`](docs/SSOT.md) | Physics ownership (Fidelity + Fast) |
| [`docs/rules.md`](docs/rules.md) | Game rules |
| [`docs/FOWLER_REFACTORING_COMPLIANCE.md`](docs/FOWLER_REFACTORING_COMPLIANCE.md) | Fowler smell map + compliance status |
| [`docs/FOWLER_2018_ZERO_SMELL_REFACTOR_PLAN.md`](docs/FOWLER_2018_ZERO_SMELL_REFACTOR_PLAN.md) | **Zero-smell plan (Fowler 2018)** |
| [`docs/FOWLER_2018_REPORT_AND_TEST_TRUTH.md`](docs/FOWLER_2018_REPORT_AND_TEST_TRUTH.md) | 2018 book report + test-as-truth |
| [`docs/archive/`](docs/archive/) | Historical research / runbooks |
| [`battles/README.md`](battles/README.md) | Corpora & golden tiers |
| [`sim/README.md`](sim/README.md) | Harness usage |

---

## CI

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) on **push/PR** to `main` / `master` / `feature/**` / **`physics/**`**:

1. **battle-retention** — id cutoff + truncated replay purge  
2. **build-and-test** — `bazel build/test //...`  
3. **physics-accuracy** — compound gate (U) ∧ (A) ∧ (B) + policy checker  

Nightly: [`.github/workflows/scheduled-tests.yml`](.github/workflows/scheduled-tests.yml) (diagnostic, not PR merge gate).

---

## Contributing

1. Keep **Gate A + Gate B** green for any physics change.  
2. Edit Fidelity only in `src/physics/physics.h`; Fast search collision only in `src/physics/fast.h`.  
3. Do not commit `sim/replay_driver` binaries or `logs/`.  
4. Prefer one focused PR; update [`docs/SSOT.md`](docs/SSOT.md) when ownership changes.
