# Working Effectively with Legacy Code — repository gap analysis

**Book:** Michael C. Feathers, *Working Effectively with Legacy Code* (Prentice Hall, 2004/2005), full PDF (~457 pages) reviewed for this report.  
**Scope:** `mad_pod_arena` as of 2026-07-08.  
**Constraint:** Gaps are **structural / testability / simplicity** only. Closing them must not reduce physics **accuracy** (gate A/B, EXACT corpora) or search **speed** (GA wall-clock). Prefer Cover-and-Modify; never “clean” by rewriting the world-step without characterization.

---

## 1. Book gist (what Feathers actually argues)

### 1.1 Definition that drives the whole book
> **Legacy code = code without tests.**

Not “old code.” Not “ugly code.” Without tests you cannot tell whether a change improved or worsened behavior. Clean structure without tests is still aerial gymnastics without a net.

### 1.2 Two ways to change software (Ch. 2)
| Mode | Meaning |
|---|---|
| **Edit and Pray** | Plan carefully, change, then poke the system hoping nothing broke. Industry default; skill + care still insufficient. |
| **Cover and Modify** | Put a **safety net of tests** around the area you change; get fast feedback on good vs bad effects. |

### 1.3 The Legacy Code Change Algorithm (Ch. 2) — **the process spine**
1. **Identify change points**  
2. **Find test points**  
3. **Break dependencies** (so code can run in a harness)  
4. **Write tests**  
5. **Make changes and refactor**  

Goal of each episode: not only a feature, but **also** its tests — islands of coverage that grow into continents.

### 1.4 Unit tests vs higher-level tests (Ch. 2)
True **unit** tests run **fast**. A test is **not** a unit test if it:
1. Talks to a database  
2. Communicates across a network  
3. Touches the file system  
4. Needs special environment setup  

Higher-level tests (scenarios, multi-class) are valuable but must be **separated** so you always have a fast suite for tight edit loops.

### 1.5 Sensing and separation (Ch. 3)
To test legacy code you need either:
- **Sensing** — see values computed inside (return values, fakes that record calls), or  
- **Separation** — isolate the unit from hard dependencies.

Fakes / mock objects exist to serve those two needs.

### 1.6 The Seam Model (Ch. 4) — central design insight
> **A seam** is a place where you can **alter behavior without editing in that place.**

Seam types Feathers stresses:
- **Preprocessing seams** (C/C++ `#ifdef` / headers)  
- **Link seams** (link alternate object files / libraries under test)  
- **Object seams** (virtual override, inject collaborator)

Seams let you exclude nasty dependencies in tests and inject sensing code.

### 1.7 Tools (Ch. 5)
Automated refactoring, mocks, unit harnesses (CppUnit-style etc.), and broader harnesses for higher-level tests.

### 1.8 Part II — problem-shaped chapters (how you act day to day)
| Ch | Problem | Core techniques |
|---|---|---|
| 6 | Little time, must change | **Sprout Method/Class**, **Wrap Method/Class** — add new code under test without full untangle first |
| 7 | Changes take forever | Understanding lag + **break dependencies** for faster feedback |
| 8 | Add a feature | **TDD** on new code; programming by difference |
| 9–10 | Can’t get class/method in harness | Parameter / global / include / construction cases |
| 11 | What to test for a change | **Effect sketches**, effect propagation |
| 12 | Many changes in one area | **Interception points**, **pinch points** |
| 13 | Don’t know what tests to write | **Characterization tests** — pin **actual** current behavior, then change |
| 14–15 | Library / all-API code | Boundary wrapping, not littering the app with vendor types |
| 16 | Don’t understand code | Notes, listing markup, **scratch refactoring**, **delete unused code** |
| 17 | No structure | **Tell the story of the system**, Naked CRC |
| 18 | Test code in the way | Naming + co-location conventions |
| 19 | Not OO / procedural C | Function seams, gradual OO extraction |
| 20 | Class too big | **See responsibilities**, extract classes without Big Bang rewrite |
| 21 | Same change everywhere | Extract duplication only after characterization |
| 22 | **Monster method** | Varieties (bulleted, nested, …); extract method carefully; preserve behavior |
| 23 | Am I breaking anything? | Hyperaware editing, **single-goal editing**, **preserve signatures**, **lean on the compiler** |
| 24 | Overwhelmed | Small islands; don’t try to boil the ocean |

### 1.9 Part III — dependency-breaking catalog (Ch. 25)
Adapt Parameter, Break Out Method Object, Definition Completion, Encapsulate Global References, Expose Static Method, Extract and Override (Call / Factory / Getter), Extract Implementer/Interface, Introduce Instance Delegator / Static Setter, Link Substitution, Parameterize Constructor/Method, Primitivize Parameter, Pull Up Feature, Push Down Dependency, Replace Function with Function Pointer, Replace Global with Getter, Subclass and Override Method, Supersede Instance Variable, Template/Text Redefinition, …

**Appendix:** Extract Method as the workhorse refactoring once covered.

### 1.10 What the book is *not*
- Not a “make everything pretty” manifesto.  
- Examples deliberately ugly / field-realistic.  
- Priority order: **tests first → then structure**. Pretty without nets is still legacy.

---

## 2. What this repository **already has** (Feathers-aligned strengths)

Do **not** throw these away chasing “simplicity.”

| Feathers idea | How we already do it |
|---|---|
| Cover and Modify | `./tools/run_truth_suite.sh` is the behavioral safety net |
| Characterization / higher-level pins | Gate A/B battle corpora, Fast goldens, EXACT Fidelity↔`fast_physics` corpus, amalgam smoke |
| Interception / pinch points (physics) | Shared `simulateFidelityWorld`; policy forbids second world loop |
| Link / build seams | Bazel targets; amalgam genrule; `CG_STANDALONE` preprocessing seam for CG paste |
| Delete unused (partial) | Removed `experiments/cg_rust`, dead `maps.h`, façade `bounce` forks, `CheckpointCollide` |
| Story of the system (partial) | `docs/SSOT.md`, `src/README.md`, package roles |
| Single global sensing flag | `g_friendly_collision` one symbol |
| Preserve behavior under refactor | EXACT + goldens as characterization of current physics law |

Physics is an **island continent** of coverage. That is the book’s success metric for that product surface.

---

## 3. What the repository is **missing** (honest gap list)

Gaps ordered by **Feathers severity × impact on “super simple + safe change”**. None of these require sacrificing accuracy/speed if done as Cover-and-Modify.

### CRITICAL — Ch. 2 / 13 / 22: bot is still largely legacy by Feathers’s definition

| Gap | Evidence | Book remedy |
|---|---|---|
| **`ga_prelude_and_search.inc` is a monster (~2.4k LOC)** without fine-grained unit tests | Only `amalgam_fast_smoke_test` + tournament; no tests for eval weights, mutation, role selection, shield policy as pure functions | Ch. 22 monster method strategy; **Break Out Method Object**; extract pure functions; **characterization tests** for GA decisions on fixed pods |
| **Almost no true unit tests for bot search** | Feathers: FS-touching battle runs ≠ unit suite for tight loops | Separate **fast pure-function tests** (no battle JSON) from gate/EXACT |
| **Edit-and-Pray risk on bot knobs** | Changing `BotConfig` / heuristic weights has weak local feedback | Ch. 13 characterization of scores for fixed scenarios; then TDD for new heuristics (Ch. 8) |

**Not claiming:** rewrite the GA. Claiming: **cover then carve**, or the bot remains Feathers-legacy even when physics is not.

### HIGH — Ch. 3 / 9 / 25: globals and hidden dependencies

| Gap | Evidence | Book remedy |
|---|---|---|
| **Globals as production API** | `g_friendly_collision`, `g_runner_id`, LUT, RNG `xor_state`, `PI` | Encapsulate Global References; Replace Global with Getter; Parameterize where hot path allows (keep fast path inlinable) |
| **CG_STANDALONE re-implements engine types** | Dual `Pod` / `BotConfig` / `IBot` in prelude for paste | Documented mirror; still a **Text/Preprocessing seam** that needs **field-level characterization** (defaults sync test) |
| **Hard to sense GA internals** | Score/mutation buried in monster TU | Extract and Override / Expose Static Method for pure scoring helpers |

### HIGH — Ch. 2 unit-test definition vs our suite mix

| Gap | Evidence | Book remedy |
|---|---|---|
| **Truth suite mixes fast + slow + FS** | Battle JSON, multi-corpus EXACT, gate A | Keep high-level tests; **also** maintain a **sub-second pure unit layer** for daily edit (Feathers separation of unit vs higher-level) |
| **Lag time for feedback** (Ch. 7) | Full gate is minutes; temptation to skip | Promote `--quick` as default local; gate only on merge (already partial) |

### MEDIUM — Ch. 16 / 17 / 20: understandability & size

| Gap | Evidence | Book remedy |
|---|---|---|
| **Monster still primary bot surface** | One `.inc` owns prelude+search | Extract Class by responsibility: RNG/IO, population, runner eval, blocker eval, output formatting — **with characterization between each extract** |
| **Doc sprawl vs “tell the story”** | Many Fowler/SSOT/archive files; easy to confuse authority | One **short system story** (already partly SSOT.md); archive is fine if **active** set stays tiny |
| **Multiple Pod representations** | Fidelity radians vs degrees Fast/engine | By design (OQ3); still cognitive load — document as **two models, one law** (world step only once) |

### MEDIUM — Ch. 21: same idea in multiple places

| Gap | Evidence | Book remedy |
|---|---|---|
| **Intentional constant mirrors** | `fast.h`, `kCgFriction` | OK for amalgam; policy-enforced — still a Ch. 21 smell if someone edits only one site without policy run |
| **BotConfig defaults in two places** | `bot_config.h` vs CG_STANDALONE block | Characterization/sync test of default field values |

### LOW — tools / process (Ch. 5, 23)

| Gap | Evidence | Book remedy |
|---|---|---|
| **Little automated C++ refactoring tooling** in workflow | Manual extracts | Acceptable; lean on compiler + tests (Ch. 23) |
| **Single-goal editing not codified** | Agents sometimes mix physics+bot+docs | Process rule: one goal per PR when touching world-step |

### EXPLICITLY OUT OF SCOPE / NOT “missing” for accuracy-speed goal
- Turning Fidelity into “elegant OOP for its own sake”  
- Unifying degrees/radians pods if it costs EXACT or speed  
- Mocking the CG server (we have battle corpora instead — higher-level characterization)

---

## 4. Chapter-by-chapter scorecard (repo vs book)

| Ch | Title theme | Score | Notes |
|---|---|---|---|
| 1 | Reasons to change / risk | Good | We change for accuracy, speed, structure with known risk on physics |
| 2 | Feedback, unit vs higher, algorithm | Partial | Strong higher-level nets; weak **fast unit** layer for bot; algorithm not always followed for bot edits |
| 3 | Sensing / separation | Partial | Physics state easy to sense; GA internals opaque |
| 4 | Seams | Good | Preprocessing (CG_STANDALONE), link (Bazel), object limited |
| 5 | Tools | Partial | Bazel/pytest solid; no heavy mock framework (often unnecessary) |
| 6 | Sprout / Wrap under time pressure | Partial | Used informally; not a named practice in bot work |
| 7 | Forever to change / lag | Partial | Quick suite exists; discipline uneven |
| 8 | Features via TDD | Weak on bot | Physics changes often characterization-first (good); new GA features less TDD |
| 9–10 | Into harness | Good for physics; weak for bot modules |
| 11 | Effect reasoning | Informal | Gate/EXACT act as effect nets; few explicit effect sketches for bot |
| 12 | Pinch points | Good on physics world-step | Bot lacks a clear pinch point API |
| 13 | Characterization tests | **Strong physics, weak bot** | Goldens/EXACT = textbook characterization of law |
| 14–15 | Libraries / API soup | N/A–OK | Minimal third-party in hot path |
| 16 | Understand / delete unused | Improving | Dead code removed; monster remains hard to read |
| 17 | Structure / system story | Good enough | SSOT + READMEs; keep active docs small |
| 18 | Test location | OK | co-located-ish under package + `sim/tests` |
| 19 | Procedural C++ | OK | Free functions in fidelity_* are fine |
| 20 | Class too big | **Open on bot** | Ch. 20 poster child = GA prelude |
| 21 | Duplication | Managed | Policy for mirrors |
| 22 | Monster method | **Open** | Primary remaining legacy blob |
| 23 | Not breaking things | Good on physics | Preserve signatures on `simulateFidelityWorld` / public gate tools |
| 24 | Overwhelmed | Process | Prefer islands: physics done-ish; bot next island |
| 25 | Dependency catalog | Selective use | Not missing as a library — use when bot extract needs it |

---

## 5. Recommended path (Cover and Modify — no accuracy/speed loss)

### Island A (already mostly land) — Physics
Keep: truth suite, SSOT policy, single world-step.  
Only change under full characterization (EXACT / gate).

### Island B (next) — Bot pure core
1. **Characterization:** fixed 4-pod states → expected score / chosen action (no battles).  
2. **Break Out Method Object / Extract Method** on eval and mutation only.  
3. **Fast unit tests** (<1s total).  
4. Then structure for readability.  
5. Never change friction/collision paths in the same PR as GA split.

### Island C — Globals (only if blocking tests)
Encapsulate for **test harness**, not for purity fashion. Hot path may keep thread_local if measured.

### Process rule (Ch. 23)
- **Single-goal PRs** when touching `fidelity_world_step.h` or gate contracts.  
- **Preserve signatures** of `simulateFidelityWorld` and public gate CLIs.

---

## 6. Garbage / temporary cleanup done with this pass

Removed from working tree (local/runtime junk or dead unreferenced scripts):

| Item | Why |
|---|---|
| `logs/` entire tree | Ad-hoc benchmark/validation output (gitignored) |
| `dist/` | Generated CG paste (gitignored; rebuild via `export_cg_submission.sh`) |
| `sim/validate_fast_physics_battles` | Ad-hoc `g++` binary (prefer Bazel) |
| `**/__pycache__`, `*.pyc` | Python bytecode |
| `src/cg/patch_getactions.py`, `patch_heuristic.py` | Unreferenced dead scripts |
| `docs/archive/research/*_raw_thoughts.md` | ~700KB LLM dump archives — not operational truth |

**Kept on purpose:** `battles/` (characterization data), `docs/archive` runbooks (historical contracts), Bazel outputs (symlinks; not source), committed goldens.

Regenerate paste anytime:
```bash
./tools/export_cg_submission.sh
```

---

## 7. Bottom line

| Question | Answer |
|---|---|
| Is the **whole** repo “non-legacy” by Feathers? | **No.** Physics is well covered (characterization continent). **Bot search is still largely legacy** (monster + weak unit net). |
| Are we Edit-and-Pray overall? | **No for physics** (Cover and Modify). **Yes risk for bot heuristics** without new pure tests. |
| Super simple + books + accuracy + speed? | Feasible: **do not** rewrite physics for beauty; **do** apply Ch. 13→22→20 to the GA island under characterization; **delete** unused/temp; **one active story** (SSOT + this gap list). |

**Simplest honest mission statement (Feathers-compatible):**  
*Change only under tests; grow test islands; extract the bot monster only after characterization; never touch the world-step owner without EXACT green.*
