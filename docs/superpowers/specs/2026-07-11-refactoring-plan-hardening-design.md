# Design: Harden the full-repo refactoring plan

| Field | Value |
|---|---|
| **Date** | 2026-07-11 |
| **Status** | Approved (brainstorm) |
| **Target** | `docs/plans/2026-07-11-full-repo-refactoring-plan.md` (+ index touch-ups) |
| **Mode** | Docs-only amendment (no C++ / sim behavior change) |
| **Convention** | In-place amend of the active campaign plan (Option A) |

---

## 1. Problem

The full-repo refactoring plan is strong (A−) but not fully **executable/auditable**:

1. Wave 1 “done” uses an undefined “LOC/seam budget.”
2. No explicit **freeze list** for Fidelity/GATE files during structure waves.
3. Amalgam acceptance is implied, not a mandatory green bar.
4. Product map omits `ga_pure.h`.
5. Older docs (`SSOT_FORWARD_PLAN`, Fowler zero-smell) can look like competing structure sequencers.
6. Plan header lacks **Status: Active**.

## 2. Goals & non-goals

### Goals

- Make Wave 0–1 **reviewable** with pass/fail exit criteria.
- Reduce risk of Fidelity ULP / GATE edits during bot modularization.
- Make amalgam breakage a first-class failure mode.
- Clarify which doc owns **structure sequencing** vs living SSOT register.
- Keep a single living campaign file (no duplicate full plan).

### Non-goals

- Implementing bot extracts or any C++ change.
- Rewriting the whole plan or renumbering waves.
- Changing GATE tolerances, physics law, or truth-suite definition.
- Merging RL / Path C feature design into this structure plan (only a fence note).

## 3. Design

### 3.1 Landing strategy

**Edit in place** `docs/plans/2026-07-11-full-repo-refactoring-plan.md`:

| Change | Detail |
|---|---|
| Header | `**Status:** Active — structure campaign` (replace “Plan only”) |
| §2 product map | Add `ga_pure.h` under `cg/` |
| §6.4 / §7 Wave 1 | Replace soft exit with **§13** criteria (or inline table) |
| New **§13 Plan hardening** | Freeze list, amalgam gate, Wave 1 exit, supersedes note |
| Optional one-line in §12 | Point to §13 for execution gates |

**Also touch:**

| File | Change |
|---|---|
| `docs/plans/README.md` | Status convention (`Active` / `Superseded` / `Done`); note structure sequencing doc |
| `docs/README.md` | Clarify `SSOT_FORWARD_PLAN` = audit history; structure waves → 2026-07-11 plan |

Do **not** create a second full plan dated 2026-07-12 unless the campaign is abandoned.

### 3.2 Wave 1 exit criteria (normative)

Wave 1 is **done** only when all hold:

| # | Criterion |
|---|---|
| W1.1 | `ga_prelude_and_search.inc` is **glue only** (includes + thin wiring) **or** ≤ **400 LOC** of non-comment code |
| W1.2 | At least these **named seams** exist as separate modules (names may vary; roles may not): **evolve/population**, **evaluate**, **I/O or GetActions orchestration**, **CG_STANDALONE / amalgam stubs** |
| W1.3 | `ga_pure` (or successor) remains pure scoring SSOT; no I/O/RNG |
| W1.4 | Amalgam genrule source list matches modules; `//src/cg:amalgam_fast_smoke_test` (or export smoke) **PASS** |
| W1.5 | Fast goldens + `./tools/run_truth_suite.sh --quick` **PASS** |
| W1.6 | No new unflagged Fast call sites in any module labeled EXACT/champion/oracle |

**Not required for Wave 1:** tournament full suite, Gate A (unless physics files touched), Path C implementation, data clumps (Wave 2).

### 3.3 Fidelity / GATE freeze list (Waves 1–5)

**Do not modify** in structure PRs (except comment-only / doc pointers):

| Path | Why |
|---|---|
| `src/physics/fidelity_math.h` | ULP / rotate / thrust lattice |
| `src/physics/fidelity_world_step.h` | World step law |
| `src/core/constants.h` | Numeric law values |
| `sim/tolerance_policy.py` | GATE numbers |
| Gate tolerance tables in verification policy | Merge bar |

**Allowed:** comments, SSOT cross-links, policy *extensions* that only forbid new dual owners (no tolerance number changes).

If a PR must touch a freeze-list file for a real bug: **split PR** — structure first, or Fidelity hat alone with Gate A + golden.

### 3.4 Amalgam acceptance (every bot-structure PR)

When any `src/cg/**` product source used by amalgam changes:

1. Genrule still lists all modules.  
2. Amalgam builds.  
3. Amalgam smoke (or export + size sanity) **PASS**.  
4. PR description notes paste path still “generated only.”

### 3.5 Product map addition

Under `cg/`:

```text
cg/   ga_pure.h (pure scoring SSOT)
      ga_prelude… + modules (search orchestration)
      amalgam genrule (only paste path)
```

### 3.6 Supersedes / index clarity

| Doc | Role after hardening |
|---|---|
| `plans/2026-07-11-full-repo-refactoring-plan.md` | **Active** structure campaign |
| `SSOT_FORWARD_PLAN.md` | SSOT audit / history; not the wave sequencer |
| `FOWLER_2018_ZERO_SMELL_REFACTOR_PLAN.md` | Smell inventory / process notes; defer waves to 2026-07-11 |
| `SSOT.md` | Living ownership register (unchanged role) |

One-line each at top of the two older plans (or only in `docs/README` if we want zero edits to old files):  
*Structure sequencing: see `docs/plans/2026-07-11-full-repo-refactoring-plan.md`.*

**Preference:** update `docs/README` always; add one-line to older plans if low churn.

### 3.7 plans/README status convention

Add:

```text
Status values for campaign plans:
  Active | Superseded by <path> | Done
```

Index table gains a Status column.

### 3.8 Path C fence (structure-only note)

In §6.4 or §13:

> Structure extracts must not add new Fast call sites to EXACT/champion paths. Path C / EI features are a separate product hat (see Zero-Bias design); they *consume* modular bot layout but are **not** Wave 1 scope.

### 3.9 Error handling / process

| Failure | Response |
|---|---|
| Wave 1 PR fails amalgam | Block merge |
| Wave 1 PR touches freeze list | Reject or split |
| Truth suite red | Block merge |
| Dispute “is prelude glue?” | Count non-comment LOC; if >400 and still holds evolve+eval+I/O, not done |

### 3.10 Testing (for the *docs* change)

No physics suite required. Verification of the hardened plan:

- [ ] Header Status = Active  
- [ ] §13 (or equivalent) contains freeze list + W1.1–W1.6 + amalgam gate  
- [ ] `docs/README` disambiguates structure vs SSOT_FORWARD  
- [ ] `plans/README` has status convention  
- [ ] No C++ files changed in the hardening PR  

---

## 4. Implementation steps (for writing-plans next)

1. Patch `2026-07-11-full-repo-refactoring-plan.md` per §3.  
2. Patch `docs/plans/README.md` status convention + index.  
3. Patch `docs/README.md` structure vs audit wording.  
4. Optional one-liners on older Fowler/SSOT forward docs.  
5. Self-check checklist in §3.10.

---

## 5. Success criteria

| Criterion | Met when |
|---|---|
| Executable Wave 1 | W1.1–W1.6 written in plan |
| Safe waves | Freeze list present |
| Amalgam first-class | Gate in plan + PR checklist |
| Single campaign file | In-place amend only |
| Index clarity | docs/README points structure waves to 2026-07-11 |

---

## 6. Out of scope after this design

Executing Wave 1 bot extracts — separate implementation plan after this docs hardening ships.

---

*End of design.*
