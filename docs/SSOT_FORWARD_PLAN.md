# SSOT Forward Plan — full-repo walk (2026-07-09)

> **Structure sequencing:** use [`plans/2026-07-11-full-repo-refactoring-plan.md`](plans/2026-07-11-full-repo-refactoring-plan.md) (Active). This file is not the wave driver.

**Audience:** implementers consolidating ownership so one law owns each concern.  
**Normative register:** [`SSOT.md`](SSOT.md) (update on every phase).  
**Behavioral truth:** `./tools/run_truth_suite.sh` (tests decide, not docs).  
**Policy:** [`VERIFICATION_TRUTH_POLICY.md`](VERIFICATION_TRUTH_POLICY.md).

This plan is a **fresh end-to-end audit** of the tree after Fidelity 100% + shared rotate/thrust extraction. It is ordered for low regression risk: freeze gates → close remaining dual owners → shrink intentional mirrors → harden CI guards.

---

## 0. How to read the tree (ownership map)

```
                    ┌─────────────────────────┐
                    │  CG battles (JSON)      │  observational oracle
                    │  battles/**             │
                    └───────────┬─────────────┘
                                │ verify_battles / GATE_*
                    ┌───────────▼─────────────┐
                    │  sim/ (Python harness)  │  tolerances, parse, driver I/O
                    │  NOT a physics owner    │
                    └───────────┬─────────────┘
                                │ replay_driver protocol
         ┌──────────────────────┼──────────────────────┐
         │                      │                      │
┌────────▼────────┐  ┌──────────▼──────────┐  ┌────────▼────────┐
│ physics.h Game  │  │ fast_physics.h Game │  │ fast.h fragment │
│ (façade)        │  │ (rollout façade)    │  │ (GA only)       │
└────────┬────────┘  └──────────┬──────────┘  └────────┬────────┘
         │                      │                      │
         │    ┌─────────────────┴────────┐             │
         └───►│ fidelity_math.h          │◄────────────┘ mirrors constants only
              │ applyFidelityRotate/Thrust│
              │ friction/snap/trig/CP TOI │
              └───────────┬──────────────┘
                          │
              ┌───────────▼──────────────┐
              │ fidelity_world_step.h    │  bounce / CP / commit
              │ simulateFidelityWorld    │
              └───────────┬──────────────┘
                          │
              ┌───────────▼──────────────┐
              │ core/constants.h         │  numeric law values
              │ core/maps/catalog.h      │  18 maps
              └──────────────────────────┘

  engine/     degrees Pod + Arena + IBot   (no collision math)
  cg/         bot_config + ga_pure + search (amalgam for CG paste)
  tournament/ benchmark only
```

**Rule of thumb:** if two files can disagree on a number or a branch, one must die or become a *checked mirror*.

---

## 1. Step-by-step audit (what exists today)

### 1.1 `src/core/` — numeric + map law

| File | Owns | Status |
|---|---|---|
| `constants.h` | friction, radii, thrust, timeout, shield mass factors | **SSOT** |
| `maps/catalog.h` | 18 tournament maps | **SSOT** |
| `progress.h` | progress index helpers | **SSOT** |

**Gaps:** none critical. Optional: export a single `kFriction` include for amalgam via codegen instead of mirrored literals.

### 1.2 `src/physics/` — physics products

| File | Owns | Status |
|---|---|---|
| `fidelity_math.h` | scalars + **rotate/thrust lattice** | **SSOT** (2026-07-09) |
| `fidelity_world_step.h` | collision/CP/commit | **SSOT** |
| `physics.h` | string driver façade, `csb::Game` | thin wrapper — **OK** |
| `fast_physics.h` | fixed-buffer Fidelity-equal Game | must call SSOT only — **OK after rotate/thrust** |
| `fast.h` | GA collision fragment (degrees) | **intentional non-Fidelity product** |
| `replay_driver.cpp` | text protocol for Python | I/O only |
| `test_physics.cpp` | unit + edge lattice + goldens | regression bar |

**Remaining physics gaps:**

1. **`applyMove` still duplicated** (shield / boost / invalid / dest==pos / hasRotated) in `physics.h` Pod and `fast_physics.h` Game — same order as Go, but two copies.
2. **`applyRotateByClampedDelta` / `applyGAActionDegrees`** (fast only) are search helpers — not GATE law; keep clearly named as non-oracle.
3. **Trig cache in fast_physics** is invalidated after SSOT thrust (correct); do not re-enable cache that bypasses `thrustCosSin`.
4. **`check_ssot_policy.py`** does not yet assert façades call `applyFidelityRotate`/`Thrust`.

### 1.3 `src/engine/` — arena / degrees view

| Concern | Status |
|---|---|
| Degrees `Pod`, apply GA actions | OK — engine owns presentation |
| Collision | **Must not own** — uses `FastSimulateTurn` → `csb::fast` only |
| Arena joint apply (observe → 4 moves → one Fidelity step) | Correct simultaneous pattern |
| `g_friendly_collision` def | `engine.cpp` — single symbol |

**Gaps:**

1. **Terminal / winner logic** split between `Game` fields and `arena.cpp` loop — extract free functions for reuse by FP search/league.
2. **SyncViewFromGame** is privileged vs pure CG stdin (boost/shield/timeout for all pods) — training risk if V/π see Arena views (document + CG-parity filter when Path C lands).

### 1.4 `src/cg/` — bot product + paste

| Concern | Status |
|---|---|
| `bot_config.h` | SSOT knobs |
| `ga_pure.h` | pure scoring/clamp — unit-tested |
| `internal/ga_*.inc` | search body (~2.4k LOC prelude) |
| amalgam genrule | only CG paste path |

**Gaps:**

1. **Prelude mirrors** (`kCgFriction`, standalone Pod) — intentional for CG_STANDALONE; keep policy-checked.
2. **Default search uses Fast fragment** — correct for latency; **forbidden** for Fidelity-equal champion rollouts (Path C must hard-fork compile paths).
3. Split prelude only when a seam is clear (factory already separate); do not dual-implement physics inside GA.

### 1.5 `sim/` — verification harness

| Concern | Status |
|---|---|
| `tolerance_policy.py` | GATE_* SSOT for Python |
| `verify_battles.py` | GATE vs DIAGNOSTIC roles |
| `battle_parser.py` | JSON → turns/keyframes |
| `physics_driver.py` | subprocess to C++ driver |
| `check_ssot_policy.py` | structural ownership tests |
| `check_verification_policy.py` | frozen gate contract |

**Gaps:** multiple entry scripts (`verify_quick_accuracy`, `compare_battle`, legacy/) — keep DIAGNOSTIC labels; do not invent second GATE.

### 1.6 `battles/` — corpora

| Corpus | Role |
|---|---|
| `test_session_battles` | **GATE A** (merge) |
| `golden_physics_battles` | **GATE B** pass tier |
| `leaderboard_battles` | full fidelity soak (now Failed:0) |
| `leaderboard_timeouts` / `test_session_timeouts` | segregated; truncated streams |
| scripts/retention | id cutoff + truncated prune |

### 1.7 `third_party/referees` — reference only

Go `csbref.go` is **behavioral reference**, not linked into product. Do not dual-build against it in CI without an explicit harness.

### 1.8 Docs / tools

| Concern | Status |
|---|---|
| `docs/SSOT.md` | living register |
| Archive SSOT_* | historical; yield to this plan + SSOT.md |
| `run_truth_suite.sh` | L0–L2 behavioral bar |
| Coverage suites under `src/coverage/` | branch cosmetics; not law owners |

---

## 2. Principles (non-negotiable)

1. **One owner per concern** — second implementation must be deleted, generated, or policy-checked as a *mirror*.
2. **Façades may not re-own law** — only protocol, storage layout, and inlining.
3. **Intentional dual products are labeled** — `csb::fast` ≠ Fidelity; never used as EXACT oracle.
4. **Tests define behavior** — `./tools/run_truth_suite.sh` green before/after each phase.
5. **Gate A + golden pass always green** on physics PRs; leaderboard Failed:0 for law changes that touch rotate/thrust/world.
6. **No new physics in Python, GA eval, or engine.**

---

## 3. Phased plan

### Phase A — Lock what we just won (1–2 PRs)  **P0**

**Goal:** prevent re-divergence of rotate/thrust.

| Step | Action | Done when |
|---|---|---|
| A1 | Extend `sim/check_ssot_policy.py`: both façades must contain `applyFidelityRotate` and `applyFidelityThrust`; forbid local `nextafter` / mid-band rotate blocks in `fast_physics.h` | policy fails if someone re-inlines lattice |
| A2 | Document in `physics/README.md` + `SSOT.md` ownership table (done if this file + SSOT.md updated) | agents stop editing two copies |
| A3 | Keep edge tests in `test_physics.cpp` as permanent lattice bar | `bazel test //src/physics:test_physics` |

**Exit:** `python3 sim/check_ssot_policy.py` + truth suite `--quick` green.

### Phase B — Full move SSOT (1 PR)  **P1**

**Goal:** one `applyFidelityMove(...)` free function.

| Step | Action |
|---|---|
| B1 | Extract shield/boost/invalid/dest==pos/hasRotated first-turn snap into `fidelity_math.h` (or `fidelity_move.h` if math file grows) |
| B2 | `Pod::applyMove` and `fast_physics::applyMove` become one-liners |
| B3 | Unit tests: invalid thrust, shield cooldown, boost once, dest==pos, first rotate |

**Exit:** test_physics + residual/golden smoke; no behavior change expected (pure Extract Function).

### Phase C — Terminal / winner SSOT (1 PR)  **P1**

**Goal:** one owner for “who won / timeout / max turns”.

| Step | Action |
|---|---|
| C1 | Free functions over `{won[4], playerTimeout[2], turn}` in physics or core (e.g. `fidelity_terminal.h`) |
| C2 | Arena calls them; document for future FP EpisodeRunner / league |
| C3 | Table-driven unit tests for dual-finish, dual-timeout, max-turns |

**Exit:** arena_fidelity_trace_test + new unit tests.

### Phase D — Verification hygiene (1 PR)  **P2**

| Step | Action |
|---|---|
| D1 | Single doc table: every script → GATE / GATE_COMPONENT / DIAGNOSTIC |
| D2 | `run_truth_suite.sh` remains sole merge-oriented compound entry |
| D3 | Timeout corpora: enforce truncated-stream exclusion or document “not physics gate” |
| D4 | Optional: promote leaderboard sample to nightly (not merge) now that Failed:0 |

### Phase E — Bot modularization without physics forks (ongoing)  **P2**

| Step | Action |
|---|---|
| E1 | Only split `ga_prelude_and_search.inc` at clear seams (already: factory, main, pure) |
| E2 | Path C / value search: **compile-time** separate TU that cannot link `FastSimulateTurn` for champion rollouts |
| E3 | Encode / Bridge (when built): CG-parity filter; no privileged Arena fields on champion path |

### Phase F — Mirror reduction (optional, careful)  **P3**

| Step | Action |
|---|---|
| F1 | Codegen amalgam constants from `constants.h` instead of hand mirrors (or expand policy checks to all mirrored names) |
| F2 | Do **not** force `fast.h` to call Fidelity world step (size/latency) |
| F3 | Do **not** merge degrees `Pod` with radians `Pod` without a measured bridge plan |

### Phase G — Anti-regression forever  **P0 ongoing**

| Guard | Command |
|---|---|
| Ownership | `python3 sim/check_ssot_policy.py` |
| Verification policy | `python3 sim/check_verification_policy.py` |
| Units + edge lattice | `bazel test //src/physics:test_physics` |
| Exact FP == Fidelity | `validate_fast_physics_corpus.py` |
| Merge physics | Gate A + golden pass via `run_truth_suite.sh` |
| Full soak | leaderboard `verify_battles` Failed:0 (nightly / pre-release) |

---

## 4. Explicit non-goals

- Replacing `csb::fast` with Fidelity inside the GA hot path (latency product).
- Rewriting Go referee as the linked SSOT.
- Unifying Python and C++ into one language.
- “One Pod type” across engine degrees and Fidelity radians without a dedicated bridge design.
- Mixing Fidelity law PRs with bot search hyperparameter churn.

---

## 5. Suggested PR sequence (concrete)

| PR | Title | Risk | Depends |
|---|---|---|---|
| **PR-A** | `check_ssot_policy`: require applyFidelity* in façades | Low | — |
| **PR-B** | Extract `applyFidelityMove` | Low–med | A |
| **PR-C** | `fidelity_terminal.h` + arena call sites | Med | — |
| **PR-D** | Verification entry matrix + timeout corpus docs | Low | — |
| **PR-E** | (Later) Path C hard-fork: no Fast on champion rollouts | High product | B, C |

Each PR: truth suite green; physics PRs also Gate A + golden.

---

## 6. Definition of “SSOT complete” for this repo

SSOT is **complete enough to stop structure churn** when:

1. Every concern in §0 map has exactly one authority row in `SSOT.md`.
2. `check_ssot_policy` encodes those rows as automated fails.
3. Fidelity façades share rotate, thrust, move, world, terminal.
4. Fast fragment is the only intentional alternate physics product and is barred from EXACT/champion paths by policy + compile guards.
5. `./tools/run_truth_suite.sh` is green on default branch.
6. No parallel predictor / second constants table / second map list / second bounce loop.

**Not required for “SSOT complete”:** zero LOC in `ga_prelude`, single Pod layout, or amalgam without any mirrored constant (mirrors with policy equality are allowed).

---

## 7. Immediate next action

Implement **Phase A1** (`check_ssot_policy` asserts) so the rotate/thrust SSOT cannot silently regress — highest leverage, lowest risk.
