# SSOT Register (living)

**Program:** Full-repo single source of truth refactor.  
**Design:** [`SSOT_REFACTOR_DESIGN.md`](SSOT_REFACTOR_DESIGN.md) (rev 3.1)  
**Implementation plan:** [`SSOT_REFACTOR_IMPLEMENTATION_PLAN.md`](SSOT_REFACTOR_IMPLEMENTATION_PLAN.md) (rev 1.1)  
**Verification plan:** [`SSOT_VERIFICATION_PLAN.md`](SSOT_VERIFICATION_PLAN.md)  

## Policy vs this program

[`VERIFICATION_TRUTH_POLICY.md`](VERIFICATION_TRUTH_POLICY.md) §6 still lists bot/engine unspaghetti as out of scope for **verification-only** tasks and freezes gate numbers / job id `physics-accuracy`.

**This SSOT program authorizes** deleting `csb_physics.h`, merging physics into one module with Fidelity/Fast profiles, moving arena onto `csb::Game`, modularizing the bot, amalgamation, and removing `CG_STANDALONE` engine forks / tournament include-cpp — **while** `GATE_*` and job id `physics-accuracy` stay frozen unless a co-PR updates policy + `sim/check_verification_policy.py`.

## Ownership (target)

| Concern | Authority |
|---|---|
| Physics Fidelity + Fast | `src/physics/physics.h` (canonical module) |
| Maps (tournament 18) | `src/core/maps/catalog.h` (from arena `ALL_MAPS`) |
| Constants | `src/core/constants.h` |
| Progress / win / timeouts | `csb::Game` + `src/core/progress.h` |
| Gate tolerances / roles | `sim/tolerance_policy.py` + this policy doc |
| Bot search / GA | `src/bot/ga/*` (target) / `src/cg/cg_bot.cpp` (today) |
| CG paste file | **Generated** amalgam only (not hand SSOT) |

## Normative API

```text
enum class PhysicsProfile { Fidelity, Fast };
struct StepOptions { PhysicsProfile profile = PhysicsProfile::Fidelity; };
void Game::step(const StepOptions& opt);  // authoritative when PR-3 lands
```

Default profile **Fidelity** — zero gate delta required.

Fast = full port of `GAPhysicsSimulator` collision algorithm (mcoeff, double impulse apply, −1 time sentinels), **not** a mass-factor branch. See implementation plan §2.3.

## Regression oracle (real CLI)

```bash
BOT_THREADS=1 bazel run //src/tournament:benchmark_tournament -- \
  --start-map 0 --end-map 18 --repeats 10 --time-budget 7.5
```

Self-play only; no `--seed` / `--maps` / `--games_per_pair` today.

## Accuracy suites (summary)

See [`SSOT_VERIFICATION_PLAN.md`](SSOT_VERIFICATION_PLAN.md):

- **L0** policy checker + `//src/physics:test_physics`
- **L1** test_session gate + golden `--tier pass` (must stay **100%**)
- **L2** `bazel build //...`
- Fast goldens (PR-3+), arena==driver traces (PR-4+), action exact (PR-7/9)

## PR status

| PR | Status |
|---|---|
| 0 Charter | done |
| 1 Delete `csb_physics.h` | done |
| 2 Constants + maps | done |
| 3 Fast profile | partial (API; Fast body TBD) |
| 4⊕4b Arena on Game | partial (Game owner; traces harness TBD) |
| 5–11 … | pending |

## Measured LOC (2026-06-27 baseline)

| Path | LOC |
|---|---:|
| `src/physics/physics.h` | 746 |
| `src/engine/engine.cpp` + `engine.h` | 495 |
| `src/engine/csb_physics.h` | 504 (deletion target) |
| `src/cg/cg_bot.cpp` | 2813 |

## OQ decision log

| OQ | Decision |
|---|---|
| OQ2 | Arena outcomes MUST match referee / Fidelity within `GATE_*` on action traces |
| OQ3 | Core radians + bot degrees adapter |
| OQ8 | PR-12 arena-fidelity CI non-blocking first |
| OQ1 | Owner: PR-6 implementer |
