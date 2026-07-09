# Zero–code-smell refactor plan (Fowler *Refactoring*, 2nd ed. 2018)

**Authority (process):** Martin Fowler with Kent Beck, *Refactoring: Improving the Design of Existing Code*, Second Edition (Addison-Wesley / Pearson, 2018).  
**Local PDF:** `~/Downloads/[Addison-Wesley Object Technology Series] Martin Fowler - Refactoring_ Improving the Design of Existing Code (2018, Addison-Wesley Professional) - libgen.li.pdf`  
**Behavioral authority (repo):** **tests only** — `./tools/run_truth_suite.sh` (docs explain; suite decides).

This plan is the **complete roadmap** to top-notch quality under the book’s rules. It does **not** claim every catalog entry must touch every line once; it claims every **smell that still exists** has a named fix, a success test, and an order that stays green.

---

## 0. Book process law (non-negotiable)

From Ch 1–2 (2nd ed.):

1. **Refactoring** = change internal structure so code is easier to understand and cheaper to change **without changing observable behavior**.
2. **Observable behavior** is what the program does under its contracts for valid inputs.
3. **Self-testing code** is required: a suite that fails if behavior breaks. That suite is the **only behavioral source of truth**.
4. **Two hats:** feature/bug work vs structure work — do not mix Fidelity numeric tuning with ownership/module refactors in one step.
5. **Small steps:** Extract / Move / Rename / Inline one at a time; **test after each step**.
6. **Performance is a different hat:** measure first; do not preserve Duplicated Code “for speed” without proof and without a clean structure.

**Repo encoding of (3):**

```bash
./tools/run_truth_suite.sh          # full contract
./tools/run_truth_suite.sh --quick  # structure loop
```

Any plan step is **done** only when the truth suite (or the step’s named subset) is green.

---

## 1. Book map → our system (what “quality” means here)

| Book theme | Meaning for mad_pod_arena |
|---|---|
| First example discipline | Never “rewrite physics/bot in one PR”; always green suite |
| Principles | Structure under tests; SSOT is ownership; **tests** define truth |
| Smells | Inventory in §2; zero open **Critical** smells is the bar for “top notch” |
| Catalog | Each wave uses named moves (Extract Function/Class, Move Field, Replace Magic Number, …) |
| APIs / modules | Clear packages: physics core, search fragment, bot product, engine host, sim truth harness |
| Performance chapter | Exact-rollout and GA speed only **after** single world-step owner and modular bot (already partly done) |

**Products in one monorepo (must stay distinct):**

| Product | Behavior locked by |
|---|---|
| Fidelity / gate physics | Gate A, Gate B, `test_physics` (partial), EXACT vs rollout |
| Exact rollout API | EXACT suite + shared `simulateFidelityWorld` |
| GA search (approx) | Fast goldens + amalgam/search contract |
| CG paste delivery | Amalgam build/smoke/export markers |
| Design ownership | `check_ssot_policy` / `check_verification_policy` |

---

## 2. Complete smell inventory (2nd ed. style) vs current code

Status: **Closed** | **Open (must fix for zero Critical)** | **Acceptable residual** (documented, not a dual owner)

### 2.1 Critical smells (must be zero for “no trace of critical smell”)

| Smell (Fowler) | Current status | Evidence | Target state |
|---|---|---|---|
| **Duplicated Code** — Fidelity world step | **Closed** | One `simulateFidelityWorld` in `fidelity_world_step.h`; both APIs call it; policy enforces | Keep; never reintroduce second loop |
| **Duplicated Code** — scalar commit/CP/TOI | **Closed** | `fidelity_math.h` | Keep |
| **Shotgun Surgery** — physics constants | **Mostly closed** | `core/constants.h` + policy on friction mirror | Close remaining raw literals in bot heuristics (§3 Wave C) |
| **Divergent Change** — GA config in engine | **Closed** | `BotConfig` in `src/cg/bot_config.h`; thin `IBot` | Keep |
| **Speculative Generality** — parallel predictor | **Closed** | `experiments/` removed | Do not re-add without SSOT + tests |
| **Alternative Classes / parallel physics owners** | **Closed for world step** | Policy requires both call shared core | Keep |
| **Comments / wrong docs as deodorant** | **Mostly closed** | GEMINI/SSOT/truth suite | Keep gaps table honest |

### 2.2 High smells (block “top notch”; not always dual-owner)

| Smell | Current status | Where | Fix wave |
|---|---|---|---|
| **Large Class / Long Function** | **Open** | `ga_prelude_and_search.inc` ~2473 lines (eval + evolve + helpers + standalone block) | Wave B |
| **Long Function** | **Open** | `RunGA`, `GetActions`, multi-phase budgets | Wave B |
| **Feature Envy** | **Open (moderate)** | GA heuristics apply friction/partial physics instead of one EndTurn API | Wave C |
| **Data Clumps** | **Open (moderate)** | Repeated 4-pod arrays, move tuples, timeout pairs without small types | Wave C |
| **Primitive Obsession** | **Open (low–mod)** | rad vs deg as bare `double`; thrust as string/int hybrids | Wave D (careful) |
| **Message Chains / Middle Man** | **Acceptable / minor** | `CGBotWrapper` thin — optional Inline later | Wave D optional |
| **Lazy Element** | **Scan each PR** | Empty/near-empty wrappers | Opportunistic |
| **Incomplete Library Class** | **N/A / low** | Prefer extend shared physics helpers over copy-paste | Process |
| **Global Data** | **Open (low)** | `g_friendly_collision` thread_local | Wave D |
| **Mutable Data / Temporary Field** | **Acceptable in sim state** | Pods are intentionally mutable simulation state | Document, don’t “fix” with immutability religion |
| **Repeated Switches** | **Open (mod in bot)** | Action/thrust mode branches | Wave B/C as Extract Function |
| **Parallel Inheritance Hierarchies** | **Low** | Avoid new bot base hierarchies | Process |
| **Refused Bequest** | **Low** | Avoid deep inheritance for bots | Process |
| **Insider Trading / Inappropriate Intimacy** | **Watch** | Bot modules reaching into physics internals | Prefer public Fast/Fidelity APIs only |
| **Data Class** | **Acceptable** | Pod/Move as data with behavior co-located in physics | OK for simulation domain |

### 2.3 Already good (do not “refactor for sport”)

| Area | Why leave stable |
|---|---|
| Gate A/B harness + policy | Book’s self-testing backbone |
| `//src/physics:test_physics` fast goldens | Bit-exact search fragment |
| Amalgam genrule + export + CI workflow | Single CG delivery |
| Truth suite script | Encodes “tests are truth” |
| Arena on Fidelity only | Correct product boundary |

---

## 3. Target architecture (end state — zero Critical, High cleared)

```text
src/core/                 constants, maps, progress          [numeric SSOT]
src/physics/
  fidelity_math.h         scalar helpers                    [done]
  fidelity_world_step.h   simulateFidelityWorld             [done]
  physics.h               Game API, applyMove, I/O compare  [thin façade]
  fast_physics.h          exact-rollout API façade only     [thin façade]
  fast.h                  GA collision fragment only        [done shape]
src/engine/               arena, degrees Pod view, thin IBot [done shape]
src/cg/
  bot_config.h            BotConfig                         [done]
  ga/ or internal/        eval / evolve / bot / io modules  [Wave B]
  ga_bot.h                CreateGABot public API            [done]
sim/                      truth harness only                [done shape]
tools/run_truth_suite.sh  sole behavioral authority         [done]
```

**Definition of done for “no critical smell / top notch”:**

1. Truth suite full green on every merge.  
2. No second world-step implementation (policy + code review).  
3. No GA config in engine.  
4. No multi-kLOC single review file for bot: **no single module > ~400–500 LOC** without a documented exception.  
5. No raw physics constants outside `constants.h` / approved amalgam mirrors (policy expanded).  
6. CG paste only via generated amalgam (export/CI).  
7. SSOT + Fowler docs match reality.

---

## 4. Phased plan (execute in order; green after each phase)

### Wave 0 — Process lock (already largely done; keep forever)

| ID | Action | Catalog move | Success test |
|---|---|---|---|
| 0.1 | Truth suite is required pre-merge | Self-testing code | `./tools/run_truth_suite.sh` exit 0 |
| 0.2 | Policy greps for dual owners / constants / no BotConfig in engine | Introduce Assertion / automated smell detector | `check_ssot_policy` exit 0 |
| 0.3 | Two-hat rule in docs | — | FOWLER_2018 report + this plan |

**Exit:** Wave 0 always green before starting any later wave.

---

### Wave A — Finish “Duplicated Code / Alternative Classes” in physics façades (thin remaining shells)

**Goal:** `physics.h` / `fast_physics.h` hold **API + applyMove only**; no private reimplementation of bounce/forward/CP loops (already moved for world step). Remove dead duplicate methods if unused.

| ID | Action | Move | Success |
|---|---|---|---|
| A.1 | Audit `Game::bounce` / `forwardTime` / local `newCollide` in `physics.h` — if only used by old path, **Inline/Remove** dead code or delegate to world helpers | Inline Function / Remove Dead Code | `test_physics` + EXACT 100 + gate A subset |
| A.2 | Same for `fast_physics` opt paths that bypass shared core (FREE_FLIGHT etc.) — either **delete** or prove bit-identical and policy-allowlist | Remove Dead Code | EXACT 100 must stay 0 fails |
| A.3 | Expand policy: forbid a second `while (t > 0` collision loop under `src/physics` outside `fidelity_world_step.h` | Guard | `check_ssot_policy` |

**Exit:** One world-step body; façades thin; truth suite green.

---

### Wave B — Large Class / Long Function on GA bot (primary remaining quality gap)

**Problem:** `ga_prelude_and_search.inc` ~2473 LOC still concentrates eval, evolution, threading, heuristics, standalone types.

| ID | Action | Move | Success |
|---|---|---|---|
| B.1 | Extract **standalone types block** (`CG_STANDALONE` Pod/IBot stubs) → `internal/ga_standalone_types.inc` | Extract Class / file | Amalgam build + smoke |
| B.2 | Extract **eval** (`SimulateAndEvaluate`, tactical cell, scoring) → `internal/ga_eval.inc` (or `.h` + symbols later) | Extract Function/Class | Fast goldens still pass if any touch search; amalgam smoke; tournament links |
| B.3 | Extract **evolution** (`RunGA`, `RunGAParallel`, population ops) → `internal/ga_evolve.inc` | Extract Class | Same |
| B.4 | Extract **GABot methods / GetActions time split** → `internal/ga_bot_class.inc` | Extract Class | `CreateGABot` API unchanged |
| B.5 | Update amalgam genrule to cat modules in dependency order | — | `export_cg_submission.sh` markers + size |
| B.6 | Size gate (policy or CI): fail if any `internal/ga_*.inc` exceeds **800 lines** without exemption comment + SSOT note | Introduce Assertion | policy |

**Order within B:** B.1 → B.2 → B.3 → B.4 → B.5 → B.6; **truth suite --quick after each**, full suite after B.5.

**Exit:** No single bot module > 800 LOC; public API and amalgam unchanged in behavior; truth suite green.

---

### Wave C — Feature Envy, Shotgun Surgery residue, Data Clumps

| ID | Action | Move | Success |
|---|---|---|---|
| C.1 | Replace ad-hoc `trunc(v * kCgFriction)` in eval with **`ApplyFriction(Pod&)`** (or `EndTurn` where full end-turn is intended) using one constant path | Extract Function | `test_physics` + amalgam; manual reason if partial friction is intentional document in function name `ApplyFrictionOnly` |
| C.2 | Expand `check_ssot_policy` to ban raw `0.85` under `src/` except `constants.h` and the single documented fast.h mirror line | Guard | policy |
| C.3 | Introduce small types for clumps: e.g. `TeamPods` / `TimeoutPair` where it removes repeated 4-array noise **without** changing search semantics | Introduce Parameter Object / Extract Class | suite green |
| C.4 | Collapse duplicate weight constants in bot prelude vs `BotConfig` defaults (one source) | Replace Magic Number / Consolidate | suite green |

**Exit:** No raw physics literals in bot; friction envy reduced to one function; suite green.

---

### Wave D — Polish smells (top-notch finish)

| ID | Action | Move | Success |
|---|---|---|---|
| D.1 | Replace `g_friendly_collision` global with return value or `SimCtx` field | Replace Global with Context | fast goldens |
| D.2 | Optional: `Rad` / `Deg` thin wrappers at API boundaries only (avoid Primitive Obsession on angles) | Replace Primitive with Object | suite; no perf claim without measure |
| D.3 | Inline or delete `CGBotWrapper` if it only forwards | Inline Class / Remove Middle Man | tournament build |
| D.4 | Split `physics.h` into `pod_move` / `game_api` / `compare` **headers that only include the core** (readability, not second algorithm) | Extract Class (files) | suite |
| D.5 | Ensure `json_minimal` / diagnostic tools never imported into bot amalgam path | Move / boundary | amalgam still builds |

**Exit:** No undocumented globals for search; physics API files readable; suite green.

---

### Wave E — Process & quality bar (keep top notch)

| ID | Action | Success |
|---|---|---|
| E.1 | PR template: “Truth suite run? (link/log)” required | Human process |
| E.2 | CI remains piecewise equivalent to truth suite (already); document mapping in this file §5 | Docs |
| E.3 | Quarterly smell audit: re-run inventory §2 against `wc -l` and policy | Living doc update |
| E.4 | Never merge with failing truth suite | Branch protection if available |

---

## 5. Truth suite mapping to CI (tests as only truth)

| Truth suite step | CI today |
|---|---|
| Policy | `physics-accuracy` → `check_verification_policy` (includes SSOT) |
| Unit / goldens / arena | `build-and-test` + physics-accuracy unit |
| EXACT multi-battle | **Local/required in truth suite**; recommend optional CI job on PR for `--limit 50` |
| CG amalgam | `cg-submission.yml` |
| Gate A/B | `physics-accuracy` |

**Rule:** CI jobs may be split for parallelism; **developers treat `./tools/run_truth_suite.sh` as the single definition of “done.”** If CI and truth suite diverge, **fix the suite first**, then align CI.

---

## 6. Explicit non-goals (book-aligned)

| Non-goal | Why |
|---|---|
| Apply every catalog entry to every function | Book uses catalog **when a smell appears**, not as a vanity rewrite |
| Change GATE_* or Fidelity long-tail for “clean code” | Different hat (feature/accuracy research) |
| Replace `csb::fast` with Fidelity as default search | Product choice; would be a feature flag project |
| Full immutable pods / pure FP redesign | Mismatches simulation domain; not required to clear smells |
| Micro-opts for leaderboard wall clock | Ch 2 performance: measure; JSON I/O ≠ class design |

---

## 7. Risk register

| Risk | Mitigation |
|---|---|
| Wave B breaks amalgam | Genrule lists modules; smoke + export after each extract |
| Wave A.2 opt removal slows rollout | Measure with `bench_fast_physics`; structure first, re-opt with single core |
| Angle wrappers break gate | Only at I/O boundaries; suite includes gate A |
| Standalone BotConfig drift | Policy or test that compares default fields to `bot_config.h` |

---

## 8. Suggested execution calendar (solid, not rushed)

| Sprint | Wave | Outcome |
|---|---|---|
| **Now (baseline)** | 0 + A (if any dead code left) | Critical duplication closed; suite is law |
| **Sprint 1** | B.1–B.3 | Search file broken into eval/evolve/standalone |
| **Sprint 2** | B.4–B.6 + C.1–C.2 | Size gate; no raw friction |
| **Sprint 3** | C.3–C.4 + D | Top-notch polish |
| **Ongoing** | E | Process |

**Each sprint exit criterion:** `./tools/run_truth_suite.sh` green + updated §2 status table in this doc.

---

## 9. Immediate next actions (first concrete PR after this plan)

1. **A.1–A.3** — dead path cleanup + policy “no second collision while-loop.”  
2. **B.1** — extract `ga_standalone_types.inc` only (smallest bot split).  
3. Re-run full truth suite; update this doc’s §2 statuses.

Do **not** start D.2 angle types before B completes (higher risk, lower smell severity).

---

## 10. One-page summary

| Question | Answer |
|---|---|
| What is the book’s core demand? | Small, tested, behavior-preserving structure changes; **self-testing code**. |
| What is our behavioral SSOT? | **`./tools/run_truth_suite.sh`** |
| What Critical smells remain? | **None for dual world-step / dual config / experiments** after prior work; **High** remains **Large Class in `ga_prelude_and_search.inc`**. |
| What makes quality “top notch”? | Critical=0, High cleared (Wave B–C), Truth Suite always green, façades thin, CG paste generated only. |
| How do we get there? | Waves **A → B → C → D → E** above, never skipping the suite. |

---

*This plan is the operational bridge from Fowler 2nd ed. (2018) to mad_pod_arena. Update §2 after every wave. If the Truth Suite and this plan disagree, fix the code or the suite—not the book.*
