# Refactoring plan hardening — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Apply the approved hardening design so `docs/plans/2026-07-11-full-repo-refactoring-plan.md` is Active, executable (Wave-1 exit criteria, freeze list, amalgam gate), and clearly indexed.

**Architecture:** Docs-only edits. Amend the living campaign plan in place; clarify `docs/plans/README.md` status convention; disambiguate structure sequencing vs `SSOT_FORWARD_PLAN` in `docs/README.md`. No C++ or sim behavior changes.

**Tech Stack:** Markdown under `docs/`; verification by file content checks (grep/test script), not physics suites.

**Spec:** `docs/superpowers/specs/2026-07-11-refactoring-plan-hardening-design.md`

## Global Constraints

- Docs-only: do not modify `src/**`, `sim/**` (except if a docs path is under them — none are).
- Do not change GATE tolerances, Fidelity numbers, or truth-suite scripts.
- Do not create a second full refactoring plan; amend `2026-07-11-full-repo-refactoring-plan.md` in place.
- Plans remain non-behavioral; `./tools/run_truth_suite.sh` stays the behavior oracle.
- Preserve existing plan sections 1–12; add/append rather than rewrite history of prior waves.

---

### Task 1: Header Status + product map (`ga_pure`)

**Files:**
- Modify: `docs/plans/2026-07-11-full-repo-refactoring-plan.md` (header + §2 product map ASCII)
- Test: shell content checks (below)

**Interfaces:**
- Consumes: current plan text
- Produces: `Status: Active — structure campaign`; product map includes `ga_pure.h`

- [ ] **Step 1: Write failing check script**

Create file `docs/plans/_check_hardening.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PLAN="$ROOT/docs/plans/2026-07-11-full-repo-refactoring-plan.md"
fail=0
grep -q 'Status:\*\* Active' "$PLAN" || { echo "FAIL: Status Active missing"; fail=1; }
grep -q 'ga_pure' "$PLAN" || { echo "FAIL: ga_pure not in plan"; fail=1; }
grep -q 'W1\.1' "$PLAN" || { echo "FAIL: W1.1 exit criteria missing"; fail=1; }
grep -q 'fidelity_world_step.h' "$PLAN" || { echo "FAIL: freeze list missing world step"; fail=1; }
grep -q 'Amalgam acceptance' "$PLAN" || { echo "FAIL: amalgam gate section missing"; fail=1; }
grep -q 'Active | Superseded' "$ROOT/docs/plans/README.md" || { echo "FAIL: plans README status convention"; fail=1; }
grep -q 'structure sequencing' "$ROOT/docs/README.md" || grep -qi 'structure waves' "$ROOT/docs/README.md" || { echo "FAIL: docs README structure pointer"; fail=1; }
if [[ "$fail" -ne 0 ]]; then exit 1; fi
echo "hardening checks PASS"
```

- [ ] **Step 2: Run check (expect FAIL before patches)**

```bash
chmod +x docs/plans/_check_hardening.sh
./docs/plans/_check_hardening.sh
```

Expected: FAIL (Status Active / W1.1 / etc. missing)

- [ ] **Step 3: Patch plan header**

In `docs/plans/2026-07-11-full-repo-refactoring-plan.md`, replace:

```markdown
**Status:** Plan only (structure / ownership campaign)
```

with:

```markdown
**Status:** Active — structure campaign  
**Hardening:** §13 (Wave-1 exit, freeze list, amalgam gate) — 2026-07-11
```

- [ ] **Step 4: Patch §2 product map**

In the ASCII map under §2, change the `cg/` line block to:

```text
engine/   Arena + degrees Pod + IBot   (host)
cg/       ga_pure.h (pure scoring SSOT)
          GA search modules + amalgam paste (bot product)
tournament/  benchmarks only
third_party/ Go referee reference only (not linked product)
```

- [ ] **Step 5: Commit**

```bash
git add docs/plans/2026-07-11-full-repo-refactoring-plan.md docs/plans/_check_hardening.sh
git commit -m "docs(plans): mark refactoring campaign Active; map ga_pure"
```

---

### Task 2: Add §13 Plan hardening (Wave-1 exit, freeze, amalgam, Path C fence)

**Files:**
- Modify: `docs/plans/2026-07-11-full-repo-refactoring-plan.md` (append after §12)
- Test: `./docs/plans/_check_hardening.sh`

**Interfaces:**
- Consumes: Task 1 header
- Produces: complete §13 with W1.1–W1.6, freeze table, amalgam rules, Path C fence

- [ ] **Step 1: Append §13 exactly as follows** (after §12 one-liner)

```markdown
---

## 13. Plan hardening (execution gates)

*Added 2026-07-11 — makes Waves 0–1 auditable. Does not change physics law.*

### 13.1 Wave 1 exit criteria (bot modularization)

Wave 1 is **done** only when **all** of the following hold:

| ID | Criterion |
|----|-----------|
| **W1.1** | `src/cg/internal/ga_prelude_and_search.inc` is **glue only** (includes + thin wiring) **or** ≤ **400** lines of non-comment, non-blank code |
| **W1.2** | Named seams exist as separate modules (names may vary): **evolve/population**, **evaluate**, **I/O or GetActions orchestration**, **CG_STANDALONE / amalgam stubs** |
| **W1.3** | `ga_pure` (or successor) remains pure scoring SSOT — no I/O, no RNG |
| **W1.4** | Amalgam genrule source list matches modules; amalgam smoke **PASS** |
| **W1.5** | Fast goldens + `./tools/run_truth_suite.sh --quick` **PASS** |
| **W1.6** | No new unflagged Fast call sites in modules labeled EXACT / champion / oracle |

**Not required for Wave 1:** full tournament suite, Gate A (unless a freeze-list file is touched), Path C / RL features, Wave 2 data clumps.

### 13.2 Fidelity / GATE freeze list (Waves 1–5)

Do **not** modify these in structure PRs (comment-only / cross-links OK):

| Path | Why |
|------|-----|
| `src/physics/fidelity_math.h` | ULP / rotate / thrust lattice |
| `src/physics/fidelity_world_step.h` | World step law |
| `src/core/constants.h` | Numeric law values |
| `sim/tolerance_policy.py` | GATE numbers |
| Gate tolerance tables in verification policy | Merge bar |

If a real bug requires touching a freeze-list file: **split PRs** — structure-only first, or Fidelity hat alone with Gate A + golden.

### 13.3 Amalgam acceptance (every bot-structure PR)

When any `src/cg/**` product source used by the amalgam genrule changes:

1. Genrule source list includes all modules.
2. Amalgam target builds.
3. `//src/cg:amalgam_fast_smoke_test` (or equivalent export smoke) **PASS**.
4. PR description notes paste remains **generated only**.

### 13.4 Path C / product fence (structure note)

Structure extracts must not add new Fast call sites to EXACT/champion paths. RL / Encode / Path C features are a **separate product hat**; they consume modular layout but are **out of scope** for Waves 1–5 (see Zero-Bias design docs under `docs/artifacts/`).

### 13.5 Supersedes (structure sequencing)

For **structure wave order**, this file is authoritative.  
`docs/SSOT_FORWARD_PLAN.md` remains an SSOT audit / history.  
`docs/FOWLER_2018_ZERO_SMELL_REFACTOR_PLAN.md` remains smell inventory / process notes.  
Living ownership register remains `docs/SSOT.md`.
```

- [ ] **Step 2: Also add amalgam line to §11 checklist**

Add as a new bullet in §11:

```markdown
- [ ] Amalgam genrule + smoke green if `src/cg` product sources moved?
```

- [ ] **Step 3: Run check (partial pass expected)**

```bash
./docs/plans/_check_hardening.sh
```

Expected: may still FAIL on plans README / docs README until Task 3; W1.1 and freeze and Amalgam acceptance should no longer fail.

- [ ] **Step 4: Commit**

```bash
git add docs/plans/2026-07-11-full-repo-refactoring-plan.md
git commit -m "docs(plans): Wave-1 exit, freeze list, amalgam gate (§13)"
```

---

### Task 3: Index + convention updates

**Files:**
- Modify: `docs/plans/README.md`
- Modify: `docs/README.md`
- Optional: one-line pointer at top of `docs/SSOT_FORWARD_PLAN.md` and `docs/FOWLER_2018_ZERO_SMELL_REFACTOR_PLAN.md`
- Test: `./docs/plans/_check_hardening.sh` → PASS

**Interfaces:**
- Consumes: §13 supersedes text
- Produces: status convention + structure sequencing pointer

- [ ] **Step 1: Update `docs/plans/README.md`**

After the Rules list, add:

```markdown
## Status values (campaign plans)

| Status | Meaning |
|--------|---------|
| **Active** | Current campaign; execute from this file |
| **Superseded by \<path\>** | Do not execute; history only |
| **Done** | Campaign finished; keep for history |

Update the Index table:

| Plan | Status | Topic |
|------|--------|--------|
| [`2026-07-11-full-repo-refactoring-plan.md`](2026-07-11-full-repo-refactoring-plan.md) | **Active** | Full-repository refactoring factors, waves, package plan |
```

- [ ] **Step 2: Update `docs/README.md`**

Change the `SSOT_FORWARD_PLAN` row to:

```markdown
| [`SSOT_FORWARD_PLAN.md`](SSOT_FORWARD_PLAN.md) | SSOT audit / history (2026-07-09). **Structure wave order:** see [`plans/2026-07-11-full-repo-refactoring-plan.md`](plans/2026-07-11-full-repo-refactoring-plan.md) |
```

Ensure the plans row still points at the Active campaign.

- [ ] **Step 3 (optional but recommended): older plans one-liner**

At the very top of `docs/SSOT_FORWARD_PLAN.md` and `docs/FOWLER_2018_ZERO_SMELL_REFACTOR_PLAN.md`, insert:

```markdown
> **Structure sequencing:** use [`plans/2026-07-11-full-repo-refactoring-plan.md`](plans/2026-07-11-full-repo-refactoring-plan.md) (Active). This file is not the wave driver.
```

- [ ] **Step 4: Run full hardening check**

```bash
./docs/plans/_check_hardening.sh
```

Expected: `hardening checks PASS`

- [ ] **Step 5: Commit**

```bash
git add docs/plans/README.md docs/README.md docs/SSOT_FORWARD_PLAN.md docs/FOWLER_2018_ZERO_SMELL_REFACTOR_PLAN.md
git commit -m "docs: Active plan status convention; structure sequencing pointers"
```

---

### Task 4: Final verification + cleanup

**Files:**
- Keep: `docs/plans/_check_hardening.sh` (durable verifier for the hardened plan)
- Optional: delete check script if policy prefers no `_` helpers — **prefer keep**

- [ ] **Step 1: Re-run check**

```bash
./docs/plans/_check_hardening.sh
```

Expected: PASS

- [ ] **Step 2: Sanity — no src/ changes**

```bash
git diff --name-only HEAD~4..HEAD 2>/dev/null | head -30
# or since branch start: only docs/ paths for this work
```

Expected: only `docs/**` paths for these commits.

- [ ] **Step 3: Final commit if check script was refined**

```bash
git add docs/plans/_check_hardening.sh
git commit -m "docs(plans): durable hardening verifier script" || true
```

---

## Self-review (spec coverage)

| Spec requirement | Task |
|---|---|
| Status Active | Task 1 |
| ga_pure in map | Task 1 |
| W1.1–W1.6 | Task 2 |
| Freeze list | Task 2 |
| Amalgam gate | Task 2 + §11 bullet |
| Path C fence | Task 2 §13.4 |
| Supersedes / index | Task 2 §13.5 + Task 3 |
| plans/README status | Task 3 |
| docs-only | Task 4 |
| Content check script | Tasks 1–4 |

Placeholder scan: none intentional.  
Type consistency: N/A (docs).

---

## Execution Handoff

After saving this plan, choose:

1. **Subagent-Driven (recommended)** — fresh subagent per task + review between tasks  
2. **Inline Execution** — execute tasks in this session with checkpoints  

**Which approach?**
