# SSOT Register (living)

**Program:** Full-repo single source of truth refactor.  
**Normative runbook (sequencing + Phase 1–4 contracts):** [`SSOT_IMPLEMENTER_RUNBOOK.md`](archive/SSOT_IMPLEMENTER_RUNBOOK.md) **v2 — wins on all conflicts**  
**Design (annex):** [`SSOT_REFACTOR_DESIGN.md`](archive/SSOT_REFACTOR_DESIGN.md)  
**Implementation plan (annex; yields to runbook):** [`SSOT_REFACTOR_IMPLEMENTATION_PLAN.md`](archive/SSOT_REFACTOR_IMPLEMENTATION_PLAN.md)  
**Verification plan:** [`SSOT_VERIFICATION_PLAN.md`](archive/SSOT_VERIFICATION_PLAN.md)  
**Gate policy:** [`VERIFICATION_TRUTH_POLICY.md`](VERIFICATION_TRUTH_POLICY.md)  
**Behavioral source of truth:** [`./tools/run_truth_suite.sh`](../tools/run_truth_suite.sh) (Fowler self-testing code — tests decide, not docs)  
**Fowler 2018 report:** [`FOWLER_2018_REPORT_AND_TEST_TRUTH.md`](FOWLER_2018_REPORT_AND_TEST_TRUTH.md)

Update **this file** on every phase PR.

## Policy vs this program

Verification policy freezes `GATE_*` and job id `physics-accuracy`. This SSOT program authorizes deleting competing physics owners and bot delivery forks **while** those stay frozen unless co-PR updates policy + `sim/check_verification_policy.py`.

## Ownership (target) — see runbook §C

| Concern | Authority | Must not re-own |
|---|---|---|
| Numeric law (friction, radii, thrust caps, …) | `src/core/constants.h` (`csb_constants`) | Hardcoded `0.85` / `120` in engine or bot without mirror policy |
| Fidelity scalar math + **rotate/thrust/move** | `src/physics/fidelity_math.h` (`applyFidelityRotate` / `Thrust` / `Move`, friction/snap/trig/CP TOI) | Second lattice or mid-band rotate on façades |
| Fidelity **world step** (bounce, CP pass, commit) | `src/physics/fidelity_world_step.h` → `simulateFidelityWorld` | Local `bounce`/`forwardTime` loops on Game façades |
| Fidelity façade (string driver, applyAction) | `csb::Game` in `src/physics/physics.h` — **thin** wrappers only | Own collision / rotate / thrust / move law |
| Fast search fragment | `csb::fast::SimulateTurn` in `src/physics/fast.h` (degrees pods) — **not** full Fidelity | A second GA collision kernel *or* using Fast as EXACT oracle |
| Fidelity-equal rollout façade | `csb::fast_physics::Game` in `src/physics/fast_physics.h` — **thin** wrappers | Own world-step / rotate / thrust / move law |
| Friendly flag | **Global** `thread_local bool g_friendly_collision` (one symbol; def in `engine.cpp`) | Second flag name |
| Maps (18 tournament) | `src/core/maps/catalog.h` only | `src/physics/maps.h` (deleted) |
| Progress index helpers | `src/core/progress.h` | — |
| Terminal win/timeout | **`csb::Game` fields** (not a separate HasWon type) | — |
| Gate tolerances / roles | `sim/tolerance_policy.py` + verification policy | — |
| Degrees arena `Pod` / apply actions | `src/engine/engine.{h,cpp}` | Collision math |
| Bot config knobs | `src/cg/bot_config.h` | `engine/bot.h` |
| Bot search / eval | `src/cg/internal/ga_*.inc` via thin `cg_bot.cpp` | Second paste file hand-edited |
| **Bot pure scoring/action math** | `src/cg/ga_pure.h` (`ga_pure::*`) — no I/O/RNG; unit-tested by `//src/cg:ga_pure_test` | Duplicating clamp/eval terms outside `ga_pure` |
| IBot contract | `src/engine/bot.h` | — |
| CG paste | Generated amalgam only (`//src/cg:cg_bot_amalgam`) | Hand-maintained submission.cpp |

### Intentional mirrors (not dual owners)

| Mirror | Why | Enforced by |
|---|---|---|
| `fast.h` numeric constants | Amalgam cannot `#include` `core/constants.h` | `sim/check_ssot_policy.py` value equality |
| `ga_prelude` `kCgFriction` / radii | Same + local eval heuristics under `CG_STANDALONE` | policy value equality |
| `CG_STANDALONE` `Pod` / `BotConfig` block in prelude | Single-file CG paste needs self-contained types | defaults documented as must-match `bot_config.h` |
| `csb::fast::Pod` vs `engine::Pod` | Layout-compatible degrees pods; bridge `FastSimulateTurn` | `static_assert` offsets |

## Normative API (locked by runbook §F)

```text
// Arena / gate / referee — Fidelity only:
void Game::step(StepOptions{Fidelity});  // → nextTurn()
// Game::step(Fast) = UNSUPPORTED (assert in debug; no-op return in release; never nextTurn)

// GA / goldens / submit Fast fragment:
namespace csb::fast {
  // Pod = degrees / pos / vel, engine-isomorphic fields
  void SimulateTurn(Pod* pods /* length 4 */);
  // sets global g_friendly_collision on teammate pairs — does NOT use a second flag name
}
extern thread_local bool g_friendly_collision;  // single global symbol
```

## Phase status (runbook phases)

| Phase | Legacy PR | Status |
|---|---|---|
| 0 Teaching | PR-0 | **done** (one physics package SSOT in GEMINI/README/src/README) |
| 1 Fast + goldens | PR-3 | **done** (`csb::fast`, goldens exact, `Game::step(Fast)` no-op/assert) |
| 2 OQ2 harness | PR-4/12 | **done** (`arena_fidelity_trace_test`: 18×50 A/B + map0 vs `replay_driver` GATE_*); L4 soak **deferred 2026-06-30** |
| 3 GA → csb::fast; delete GAPhysics | PR-5/6 | **done** (`FastSimulateTurn`; GAPhysicsSimulator/PhysicsSimulator removed from engine) |
| 4 Amalgam; kill include-cpp | PR-7/9 | **partial / minimum** — amalgam genrule + `ga_bot` (no include-cpp); `CG_STANDALONE` keeps Pod/RNG/ApplyGAAction stubs (collision = `csb::fast` only); full P0c-on-amalgam-TU not required for exit |
| 5 Engine zero collision | … | **done** |
| 6 Constants/maps hygiene | … | partial (no large rename) |
| 7 Docs + L4 promote | … | docs aligned; L4 soak deferred 2026-06-30 |
| 8 No new forks | … | **done hygiene** — removed non-authoritative `experiments/cg_rust` (2026-07-08); do not re-add parallel predictors without SSOT update |

## OQ decision log (complete)

| OQ | Status | Notes |
|---|---|---|
| **OQ1** | Deferred | Fidelity-under-budget for GA — notes only if Phase 3 implementer measures |
| **OQ2** | Ownership **done**; harness **done** (A/B + replay_driver GATE) | L4 soak still deferred 2026-06-30 |
| **OQ3** | **Locked** | Radians `Game` + degrees `SyncViewFromGame` |
| **OQ4** | Deferred | Stay C++17 unless staff decides here |
| **OQ5** | Deferred; **default stay `src/physics/`** | No path move in Phases 1–4 |
| **OQ6** | Deferred | nlohmann_json adopt/remove — not blocking physics SSOT |
| **OQ7** | Deferred | GA global CP eval optional post–Phase 4 |
| **OQ8** | **Locked** | Arena-fidelity CI non-blocking first; promote after 7/7 green default-branch |

Do **not** invent OQ1/4/5/6/7 in code without updating this table.

## Regression oracle

```bash
BOT_THREADS=1 bazel run //src/tournament:benchmark_tournament -- \
  --start-map 0 --end-map 18 --repeats 10
```

## L-ladder (summary)

See runbook §N. L0–L2 block now; L3 after Phase 1; L4 after Phase 2 soak; L5 after Phase 4.

## SSOT gaps (status after full-repo walk 2026-07-09)

| Gap | Status |
|---|---|
| Fidelity world-step owner | **Done:** `fidelity_world_step.h` / `simulateFidelityWorld` |
| Shared scalar math | **Done:** `fidelity_math.h` (friction/snap/trig/CP TOI) |
| **Rotate + thrust ULP lattice** | **Done:** `applyFidelityRotate` / `applyFidelityThrust` — both façades call SSOT |
| **Full-move apply** | **Done (2026-07-09):** `applyFidelityMove` — shield/boost/invalid/dest==pos/first-rotate; both façades thin wrappers |
| Constants | **Done:** `core/constants.h`; `fast.h` + prelude mirrors policy-checked |
| Maps | **Done:** only `core/maps/catalog.h` |
| Engine CP geometry fork | **Done** |
| Bot config ownership | **Done:** `src/cg/bot_config.h` |
| Bot pure scoring | **Done:** `ga_pure.h` + `ga_pure_test` |
| CG paste | **Done:** amalgam genrule + export + CI (must not fork Fidelity lattice) |
| Experiments / forks | **Removed** |
| GA Fast fragment vs Fidelity | **Intentional dual product** (`csb::fast`) — not EXACT oracle; policy allows |
| `ga_prelude_and_search.inc` size | **Partial:** ~2.4k LOC single product unit (non-physics) |
| Terminal win/timeout helpers | **Partial:** on `Game` + arena; extract when Path C needs shared free functions |
| Python sim tolerances / drivers | **Done enough:** `tolerance_policy.py` + `replay_driver` |
| `check_ssot_policy` rotate/thrust/move/world | **Done:** requires `applyFidelityRotate`/`Thrust`/`Move` + `simulateFidelityWorld` on both façades; forbids façade bounce/TOI/nextafter lattice |
| Fidelity vs CG battles | **Done:** Gate A 312, golden 200, leaderboard Failed:0 under GATE |

Forward plan: [`SSOT_FORWARD_PLAN.md`](SSOT_FORWARD_PLAN.md).

## Fidelity vs SSOT

Gate A **312/312**, golden pass-tier **200/200**, and leaderboard **Failed:0** must hold for any physics law change.  
Fast (`csb::fast`) is **not** a Fidelity substitute — intentional GA collision fragment only.
