# Fowler *Refactoring* (2nd ed., 2018) — full report & test-as-truth for mad_pod_arena

**Book file:** `~/Downloads/...Refactoring...2018...pdf` (Second Edition, Martin Fowler with Kent Beck; ISBN 978-0-13-475759-9; ~455 pages).  
**Repo law:** **Observable behavior is defined only by the Truth Suite** (`./tools/run_truth_suite.sh`). Docs describe; tests decide.

---

## Part A — The book, step by step

### A.1 What the 2nd edition is
An updated handbook on **improving the design of working software** by applying small, named structural changes that **do not alter observable behavior**. Examples modernized (often JS); catalog expanded; the process is the same as the 1999 first edition.

### A.2 Chapter arc (how to read it for this repo)

| Phase | Content | Takeaway for us |
|---|---|---|
| **Ch 1 — First example** | Theatrical company billing (replaces video-store example): extract functions, move data, replace conditionals with polymorphism, **test after each step** | Never rewrite the whole bot/physics in one shot; change structure under tests |
| **Ch 2 — Principles** | Noun/verb definition of refactoring; two hats (feature vs refactor); **self-testing code**; when to refactor; risks; performance is secondary | Dual “truth” (docs vs code vs two physics) is forbidden; **tests win** |
| **Bad smells** | Duplicated Code, Long Function, Large Class, Shotgun Surgery, Divergent Change, Feature Envy, Primitive Obsession, Speculative Generality, … | Our closed smells: dual world-step, magic constants, engine-owned GA config, experiments fork, stale agent docs |
| **Catalog (middle of book)** | Extract Function/Class, Move Function/Field, Replace Magic Number, Inline, Rename, … each with **motivation + mechanics + test** | We applied Extract for math/world-step, Move for BotConfig, modular bot files |
| **Later chapters** | APIs, inheritance, data organization | Optional further catalog; not required if Truth Suite is green |
| **Throughout** | **Test. Test. Test.** after each micro-step | Truth Suite is the gate for “OK to continue” |

### A.3 Core definitions (from the book)

- **Refactoring (noun):** a change to internal structure that makes software easier to understand and cheaper to modify **without changing observable behavior**.  
- **Refactoring (verb):** applying a series of such changes.  
- **Observable behavior:** what the program produces for valid inputs under its contract—not every undefined edge case.  
- **Self-testing code:** a suite that can tell you in seconds whether you broke behavior—**that suite is the practical source of truth**.

### A.4 “Only source of truth is the tests”

In Fowler/Beck terms:

1. Specs and SSOT **docs** record intent and ownership.  
2. **If a conflict arises, the failing/passing automated checks decide.**  
3. You do not “know” physics is correct because `physics.h` looks right—you know because **gate A/B, EXACT, goldens, and policy checks pass**.  
4. You do not “know” CG paste is correct because you copied a file by hand—you know because **amalgam build + smoke + export markers pass**.

This repo codifies that as **`./tools/run_truth_suite.sh`**.

---

## Part B — How mad_pod_arena implements the book

| Book practice | Implementation |
|---|---|
| Preserve observable behavior | Gate A (CG battles), Gate B (golden pass), EXACT Fidelity vs `fast_physics`, fast goldens |
| Self-testing / safety net | `run_truth_suite.sh` + policy scripts + Bazel tests |
| Remove Duplicated Code | `fidelity_math.h` + **`fidelity_world_step.h` / `simulateFidelityWorld`** single world-step |
| Replace Magic Number | `src/core/constants.h` + policy-checked `fast.h` mirror |
| Extract Class / modular ownership | `src/cg/internal/ga_*.inc`, thin `cg_bot.cpp` |
| Move Field | `BotConfig` → `src/cg/bot_config.h`; engine = thin `IBot` |
| Separate delivery product | CG amalgam export/workflow ≠ physics merge gate |
| Kill Speculative Generality | `experiments/` removed |
| Two hats | Truth suite green ⇒ structure OK; fidelity long-tail math is a different hat |

---

## Part C — The Truth Suite (sole behavioral SSOT)

```bash
# Full contract (merge-quality)
./tools/run_truth_suite.sh

# Faster loop while editing structure (skips full gate A/B)
./tools/run_truth_suite.sh --quick
```

| Step | What it proves |
|---|---|
| 1 `check_ssot_policy` | Single world-step owner, constants mirror, no BotConfig in engine, modular bot files, no experiments fork |
| 2 `check_verification_policy` | Gate policy frozen + embeds SSOT policy |
| 3 Bazel physics/amalgam/arena tests | Unit contracts + fast goldens + amalgam present |
| 4 EXACT 100 battles | Rollout API == Fidelity after shared core |
| 5 CG export | Paste is generated, non-empty, marked GENERATED/SimulateTurn |
| 6 Gate A (full) | 312/312 test_session turn-perfect under GATE_* |
| 7 Gate B (full) | 188/188 golden pass tier |

**Rule for contributors:** If the Truth Suite is green, structure refactors are allowed. If red, behavior or ownership is broken—**fix tests or code, never “fix” by arguing from docs alone.**

Unit entry that drives real policy scripts: `python3 -m unittest sim.tests.test_truth_suite_entry -v`.

---

## Part D — Step-by-step how we stay compliant

1. Change structure (extract/move/rename) in a small step.  
2. Run `./tools/run_truth_suite.sh --quick` (or full before merge).  
3. If green, commit. If red, revert or fix until green.  
4. Do **not** mix long-tail Fidelity numeric tuning with ownership refactors in one change.  
5. Update `docs/SSOT.md` only when ownership **actually** changes; tests already enforce the critical bits.

---

## Part E — Report status (honest)

| Area | Grade under 2018 Fowler |
|---|---|
| Self-testing physics | **A** (gates + EXACT + goldens) |
| Single algorithm for world step | **A** (`simulateFidelityWorld`) |
| Constants / ownership guards | **A−** (policy-enforced) |
| Bot modularity | **B+** (review modules; still large search .inc) |
| CG delivery | **A** (generated amalgam + export + workflow) |
| “Tests are the only truth” culture | **A** once Truth Suite is the required pre-merge command |

Optional further catalog (not required while Truth Suite green): split `ga_prelude_and_search.inc` into linked TUs; more Extract Function inside GA eval.

---

*Generated from full-text extraction of the 2018 PDF plus in-repo enforcement. Re-run `./tools/run_truth_suite.sh` to re-prove the report.*
