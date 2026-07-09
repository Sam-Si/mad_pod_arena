# `src/physics/` — physics SSOT package

| Header / target | Role |
|---|---|
| **`fidelity_world_step.h`** | **Single world-step owner** — `simulateFidelityWorld` (bounce/CP/commit) |
| **`fidelity_math.h`** | Shared scalars + **SSOT rotate/thrust/move** (`applyFidelityRotate` / `Thrust` / `Move` / friction/snap); aliases `core/constants.h` |
| `physics.h` (`csb::Game`) | Fidelity **façade** — string driver / applyAction; **thin wrappers only** |
| `fast_physics.h` (`csb::fast_physics`) | Fidelity-equal **façade** (fixed buffers); same SSOT move/rotate/thrust + world step |
| `fast.h` (`csb::fast`) | **GA search fragment** only (collision/end-turn on degrees pods) — intentional constant mirrors for amalgam |
| `//src/physics:replay_driver` | Text protocol driver for Python `sim/` |
| `//src/physics:test_physics` | Unit + Fast goldens + **fidelity edge-case lattice** (battle knife-edges as unit tests) |
| `//src/physics:bench_fast_physics` | Stress EXACT + timing Fidelity vs `fast_physics` |
| `//src/physics:validate_fast_physics_battles` | Stream format: Fidelity vs `fast_physics` per battle |

Maps live in **`src/core/maps/catalog.h` only** (no `maps.h` here).

Parity driver: `python3 sim/validate_fast_physics_corpus.py` (see `sim/README.md`).

Do **not** implement a third ruleset or a second world-step loop on either Game façade.
