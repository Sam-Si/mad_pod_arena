# Full-repository refactoring plan

**Date:** 2026-07-11  
**Status:** Active — structure campaign  
**Hardening:** §13 (Wave-1 exit, freeze list, amalgam gate) — 2026-07-11  
**Authority (process):** Fowler *Refactoring* 2nd ed. — change structure without changing observable behavior  
**Behavioral truth:** `./tools/run_truth_suite.sh` (tests decide; this doc explains)  
**Related living docs:** [`../SSOT.md`](../SSOT.md), [`../SSOT_FORWARD_PLAN.md`](../SSOT_FORWARD_PLAN.md), [`../FOWLER_2018_ZERO_SMELL_REFACTOR_PLAN.md`](../FOWLER_2018_ZERO_SMELL_REFACTOR_PLAN.md), [`../VERIFICATION_TRUTH_POLICY.md`](../VERIFICATION_TRUTH_POLICY.md)

This is a **structure-and-ownership** plan for `mad_pod_arena`. It is **not** a rewrite plan and **not** a Fidelity-numeric tuning plan.

---

## 1. What “refactoring” means here

| Rule | Implication for this repo |
|------|---------------------------|
| Change structure, not observable behavior | Gate A, golden pass, EXACT FP≡Fidelity, Fast goldens, amalgam export markers must stay green |
| Tests define behavior | `./tools/run_truth_suite.sh` is the behavioral authority; docs only explain |
| Two hats | Never mix Fidelity ULP/edge-case tuning with module moves in the same PR |
| Small steps | Extract / Move / Rename one seam; re-run the named suite after each step |
| Products stay distinct | Fidelity, GA Fast, bot search, CG paste, Python harness are different products sharing ownership rules |

**Refactoring is done when structure is clearer and cheaper to change, and the truth suite still passes — not when files are “cleaner looking.”**

---

## 2. Current product map (must remain explicit)

```
battles/  ──oracle JSON──►  sim/ (parse, GATE_*, drivers)
                                │
                                ▼
                         replay_driver
                                │
         ┌──────────────────────┼──────────────────────┐
         ▼                      ▼                      ▼
   physics.h Game        fast_physics.h Game      fast.h (GA only)
   (Fidelity façade)     (Fidelity-equal)         intentional ≠ Fidelity
         │                      │
         └──────────┬───────────┘
                    ▼
          fidelity_math.h + fidelity_world_step.h
                    │
          core/constants.h + maps/catalog.h

engine/   Arena + degrees Pod + IBot   (host)
cg/       ga_pure.h (pure scoring SSOT)
          GA search modules + amalgam paste (bot product)
tournament/  benchmarks only
third_party/ Go referee reference only (not linked product)
```

Any refactor that **blurs** Fidelity with Fast, or Arena privilege with CG stdin parity, is a design failure even if tests still “pass” on the wrong product.

**Approx scale (for planning, not a target):** ~9.5k LOC under `src/`, ~3k LOC under `sim/`, largest bot unit `ga_prelude_and_search.inc` ~2.4k LOC.

---

## 3. Factors every refactor must account for

These are the dimensions to plan against (the complete “factors” list).

### 3.1 Ownership / SSOT factors

- **One authority per concern** (constants, rotate/thrust/move, world step, maps, gate tolerances, bot config, pure scoring, CG paste generation).
- **Checked mirrors** only where amalgam/CG_STANDALONE cannot include core headers (`fast.h` friction, prelude `kCgFriction`, etc.) — equality enforced by `check_ssot_policy`.
- **Forbidden second owners:** façade-local bounce/TOI/nextafter lattice; second map list; resurrected `PhysicsSimulator` / experiments forks.
- **Intentional dual product:** `csb::fast` is allowed **only** as non-EXACT search; never as GATE oracle.

### 3.2 Behavioral / verification factors

- **GATE A** (`test_session_battles` + `--gate`) — merge bar.
- **GATE B** golden pass-tier — second corpus, not a subset of A.
- **EXACT** Fidelity ≡ `fast_physics` on shared world step.
- **Fast goldens / rollout goldens** — search fragment bit-contract.
- **Unit edge lattice** (`test_physics`) — knife-edges without full battles.
- **Leaderboard soak** — confidence, not necessarily every PR’s merge gate.
- **Timeout corpora** — segregated; truncated streams must not redefine “physics fail.”
- **Role lines** (GATE / GATE_COMPONENT / DIAGNOSTIC) must not be diluted.

### 3.3 Domain / type factors

- **Radians Fidelity Pod** vs **degrees engine/Fast Pod** — two layouts by design (OQ3 locked); bridges must be explicit.
- **Angle representation:** principal snap on mid-rotate, unwrapped accumulation on max-rotate, 2π peel-only canonicalize — do not “simplify” without GATE proof.
- **Integer commit surface:** friction trunc + position round-half-up only at end of turn; doubles mid-turn.
- **Simultaneous joint apply:** observe → four pending moves → one world step (Arena order).

### 3.4 Bot / CG paste factors

- **Amalgam size + single-file CG constraints** — cannot freely `#include` core; forces mirrors and `.inc` split strategy.
- **`ga_prelude_and_search.inc` ~2.4k LOC** — largest open structure smell (Long Function / Large Class).
- **Search latency budget** (~75 ms class) — forbids naive “always Fidelity in GA.”
- **`ga_pure.h`** — pure scoring SSOT; keep I/O/RNG out.
- **Champion vs training privilege** — Arena views can leak shield/timeout/boost for all pods; CG stdin does not.

### 3.5 Build / tooling factors

- Bazel package graph (`//src/physics`, `//src/cg`, `//src/engine`, …).
- CI jobs: `physics-accuracy`, build-and-test, battle-retention (frozen names/policy).
- Truth suite composition (policy + units + EXACT sample + optional full GATE).
- Local artifacts (coverage binaries, `docs/coverage_out/`) must stay out of the product graph.

### 3.6 Data / corpus factors

- Retention cutoff and truncated-replay policy (`battles/RETENTION.md`).
- Golden `expected_pass` / `expected_fail` semantics.
- Battle JSON framing: 1 game turn = 2 frames (harness only; physics steps once per turn).

### 3.7 Process / human factors

- Two-hat discipline; PR size; who can change GATE tolerances.
- Docs drift (SSOT register must update when ownership rows move).
- Agent/automation safety: structural policy tests beat prose.
- **Plans must be written under `docs/plans/`** (this folder) — not chat-only.

### 3.8 Performance factors (separate hat)

- Measure before optimizing; do not re-duplicate world step “for speed.”
- Fast fragment vs Fidelity-equal rollouts are different SLAs.
- Inlining / fixed buffers in `fast_physics` are façade optimizations **only if** they still call shared law.

### 3.9 Risk / non-goal factors

- No full rewrite of GA in one PR.
- No unifying degrees+radians Pod without a measured bridge design.
- No linking Go referee as runtime SSOT.
- No Python reimplementation of physics.
- No mixing “best bot” feature work into ownership refactors.

---

## 4. Current state (what is already “refactored enough”)

| Area | Status | Do not re-open without strong reason |
|------|--------|--------------------------------------|
| World step SSOT | Done | `simulateFidelityWorld` |
| Rotate / thrust / move SSOT | Done | `applyFidelity*` |
| Constants + maps SSOT | Done | `core/*` |
| Façades thin wrappers | Done | Policy-enforced |
| Engine no collision law | Done | `FastSimulateTurn` only |
| BotConfig / IBot split | Done | |
| CG amalgam path | Done (minimum) | Generated paste only |
| Gate A + golden pass + leaderboard Failed:0 | Done | Fidelity accuracy hat finished for now |
| Experiments / dual predictors | Removed | |

**Largest remaining structure debt:** bot search monoblock + terminal helpers + residual primitive clumps + doc/CI hygiene — **not** Fidelity dual ownership.

---

## 5. Smell-driven backlog (repo-wide)

### Critical (must stay zero)

- Second Fidelity world / rotate / thrust / move owner
- Second constants/maps owner without policy
- Parallel physics predictors

### High (next structure work)

| Smell | Where | Direction |
|-------|--------|-----------|
| Large Class / Long Function | `ga_prelude_and_search.inc` (~2446 LOC) | Extract by seam: evolve, evaluate, free-flight, CG_STANDALONE block, I/O |
| Long Function | `RunGA` / `GetActions` / budget phases | Extract Function; table-driven phases |
| Feature Envy | Bot heuristics touching friction/partial physics | Call shared EndTurn/Fast APIs only |
| Data Clumps | 4-pod arrays, move tuples, timeout pairs | Small value types (`PodView`, `TeamTimeouts`, `Move4`) |
| Divergent Change (residual) | Terminal win/timeout in Game + Arena | Optional `fidelity_terminal` free functions when a second consumer appears |
| Primitive Obsession | rad/deg bare doubles; thrust string/int | Named types or thin wrappers **only with** bridge tests |
| Global Data | `g_friendly_collision` | Keep single symbol; document; optional context param later |

### Acceptable residuals

- Mutable sim state (Pods)
- Thin wrappers (`CGBotWrapper`)
- Fast vs Fidelity dual product
- Checked constant mirrors for amalgam

---

## 6. Package-by-package refactor plan

### 6.1 `src/physics/` — stabilize; only extract, don’t redesign

**Factors:** law purity, façade thinness, Fast labeling, test lattice.

**Moves:**

1. Freeze Fidelity law API surface (`applyFidelity*`, `simulateFidelityWorld`).
2. Keep `fast.h` clearly labeled “search fragment” in docs/BUILD comments.
3. Optionally split `fidelity_math.h` if it grows further (`fidelity_move.h` vs scalars) — **only** if navigation pain is real.
4. Do **not** fold Fast into Fidelity for GA.

**Exit:** policy + `test_physics` + EXACT sample green.

### 6.2 `src/core/` — hygiene

**Factors:** single numeric law, map catalog only.

**Moves:** expand policy checks to all mirrored names if new mirrors appear; avoid renames for sport.

### 6.3 `src/engine/` — host only

**Factors:** degrees presentation, joint apply order, no collision ownership.

**Moves:**

1. Extract terminal/winner pure functions when Arena + search both need them.
2. Document CG-parity vs privileged `SyncViewFromGame` (training risk).
3. Keep Arena on Fidelity step only.

### 6.4 `src/cg/` — main structure campaign

**Factors:** amalgam size, latency, pure scoring SSOT, large prelude.

**Wave B (recommended core of “repo refactor”):**

1. Inventory call graph: `GetActions` → GA → evaluate → FastSimulateTurn.
2. Extract pure helpers into `ga_pure` (already started) until prelude is orchestration.
3. Split prelude by **stable seams** only (evolve, population, evaluate, free-flight, standalone stubs) — not arbitrary file cuts.
4. Ensure amalgam genrule source list stays the only paste path.
5. Hard fence: champion/EXACT paths never call Fast without an explicit, tested product flag (future Path C).

**Exit:** smaller modules, same Fast goldens + amalgam smoke + tournament smoke.

### 6.5 `src/tournament/` — thin client

No physics ownership; only ensure it links the intended bot/physics products.

### 6.6 `sim/` — harness clarity

**Factors:** GATE roles, no physics reimplementation, parser fidelity.

**Moves:**

1. One published matrix: script → GATE / GATE_COMPONENT / DIAGNOSTIC.
2. Keep `tolerance_policy.py` as sole GATE numbers owner.
3. Legacy scripts stay quarantined under `sim/legacy/`.
4. Do not grow second parsers for “convenience.”

### 6.7 `battles/` — data governance

**Factors:** retention, truncation, golden tiers, timeout segregation.

**Moves:** enforce retention/truncated scripts in CI; never “fix” incomplete replays by ranks.

### 6.8 `docs/` — register, don’t accumulate

**Factors:** living SSOT vs archive; avoid dual runbooks.

**Moves:**

1. One living ownership register (`SSOT.md`).
2. Archive historical plans; update register on every ownership PR.
3. **Campaign plans** live under `docs/plans/` with dated filenames (this file).

### 6.9 `tools/` + CI — compound truth

**Factors:** truth suite as single behavioral entry; frozen job ids.

**Moves:** keep `run_truth_suite.sh` as default “is it safe to refactor?”; promote soak jobs carefully (OQ8).

### 6.10 `third_party/referees` — reference only

Never dual-build as second runtime without an explicit dual-oracle harness.

---

## 7. Recommended wave order (structure hat only)

| Wave | Scope | Why this order | Green bar |
|------|--------|----------------|-----------|
| **0** | Policy freeze + “no new dual owners” | Prevent regression while refactoring | `check_ssot_policy`, verification policy |
| **1** | Bot modularization (prelude seams) | Highest structure debt, physics already stable | truth suite `--quick` + Fast goldens + amalgam |
| **2** | Data clumps / small types (Move4, timeouts) | Makes bot+engine APIs clearer | units + arena trace |
| **3** | Terminal free functions (if second consumer) | Shared win/timeout without Arena intimacy | arena tests + units |
| **4** | Sim harness matrix + legacy quarantine | Cleaner verification story | policy scripts |
| **5** | Optional: math header split, mirror codegen | Navigation only | full truth suite + Gate A/B |
| **6** | Performance hat (separate) | Only after structure | benchmarks + EXACT |

**Hard rule:** Fidelity numeric PRs never land in Waves 1–5.

---

## 8. PR / process template for each refactor step

1. Name the smell and the Fowler move (Extract Function / Move Method / …).
2. List **behavior that must not change** and which suite locks it.
3. One seam only; no “while we’re here” law edits.
4. Update `docs/SSOT.md` if ownership rows move.
5. Extend `check_ssot_policy` if a new dual-owner risk is inventable.
6. Run: at least `./tools/run_truth_suite.sh --quick`; physics-touching steps also Gate A + golden.
7. Write/update the campaign plan under `docs/plans/` if scope is multi-PR.
8. PR description: structure-only, product boundary note (Fidelity vs Fast).

---

## 9. Definition of “repo refactor complete” (structure)

Structure campaign is **complete enough** when:

1. Every concern has one authority row in `SSOT.md`, enforced by policy tests.
2. Fidelity façades stay thin; Fast is labeled and non-oracle.
3. Bot search is modular enough that no single file is the default “god object” for all of evolve+eval+I/O (~prelude under a chosen LOC/seam budget).
4. Engine has zero collision law and clear view-privilege docs.
5. Sim has one GATE matrix; legacy is quarantined.
6. Truth suite green on default branch; Gate A/B green for any physics-adjacent change.
7. No parallel predictors; no second map/constants owners.
8. Plans for multi-step campaigns are filed under `docs/plans/`.

**Not required for complete:** single Pod type, zero mirrored constants, zero GA Fast, 100% branch coverage of prelude, or rewriting the Go referee.

---

## 10. Explicit non-goals (do not put in this campaign)

- Rewriting physics for more accuracy (already at GATE 100% on primary corpora).
- Replacing Fast with Fidelity in the GA hot path without a measured Path C design.
- Unifying degrees/radians pods “for elegance.”
- Large-scale renames for aesthetics.
- RL / Encode / Bridge / Path C features (those are product hats; they *consume* this structure plan).
- Deleting battle corpora or changing GATE tolerances without policy co-change.

---

## 11. Factors checklist on every refactor PR

- [ ] Product boundary clear (Fidelity / Fast / bot / harness)?
- [ ] Ownership row unchanged or SSOT.md + policy updated?
- [ ] No second lattice / bounce / constants / maps?
- [ ] Amalgam still builds if bot sources moved?
- [ ] Degrees↔radians bridge still correct if types moved?
- [ ] Joint apply order preserved?
- [ ] Named test suite green (truth suite / Gate A/B as required)?
- [ ] Two hats: no Fidelity numeric tweak in this PR?
- [ ] Performance claim measured if any “for speed” argument?
- [ ] Docs/archive: no second conflicting runbook?
- [ ] Multi-PR campaign plan filed or updated under `docs/plans/`?

---

## 12. One-line sequencing recommendation

**Physics SSOT is largely finished → next repo refactor center of gravity is the bot monoblock (`ga_prelude_and_search.inc`) under a frozen Fidelity law and a hard Fidelity-vs-Fast product fence; then data clumps, terminal helpers, and harness/docs hygiene — always with the truth suite as the only behavior oracle.**
