# Latest Battles 100% Turn Correctness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Achieve **100% GATE turn correctness** on all **2320** battles in `battles/latest_battles/` (currently **2307 pass / 8 fail / 99.80% turns**) without loosening `GATE_*` and without regressing Gate A / golden.

**Architecture:** Evidence-first, two-cluster fix. Cluster **β** (7 battles) is free-flight thrust ULP in `applyFidelityThrust` — already diagnosed and partially patched in an uncommitted WIP on `fidelity_math.h`. Land that lattice only behind battle-proven unit tests, then attack residual **γ-adjacent bounce** seed on `battle_895131867` in `fidelity_world_step.h`. Full `latest_battles` verify is the exit gate. Do not invent a second physics owner.

**Tech Stack:** C++17 Fidelity (`src/physics/fidelity_math.h`, `fidelity_world_step.h`), Bazel `//src/physics:test_physics` + `//src/physics:replay_driver`, Python `sim/compare_battle.py` / `sim/verify_battles.py`, GATE tolerances in `sim/tolerance_policy.py`.

**Evidence / specs (read before coding):**
- `docs/artifacts/LATEST_FAILS_FORENSICS.md` — per-battle first EXACT seed, cascade, levers
- `docs/artifacts/PHYSICS_100PCT_LATEST_INVESTIGATION.md` — corpus scrape + verify summary
- Scratch forensics logs: `/var/folders/jp/bz2c731s5w597nv82lt5t6m00000gn/T/grok-goal-3a00df3671e7/implementer/forensics/`

## Global Constraints

- **GATE_* frozen** (`sim/tolerance_policy.py`): pos ≤ **5.0**, vel ≤ **3**, ang ≤ **1.0°**, timeout ≤ **1**, exact `next_cp`. Do not loosen to “make 100%.”
- **Physics SSOT:** thrust lattice only in `src/physics/fidelity_math.h` (`applyFidelityThrust`); bounce/world only in `src/physics/fidelity_world_step.h`. Façades stay thin.
- **Regression must stay green:** Gate A `battles/test_session_battles` **312/312**; golden pass-tier via `verify_golden_corpus.py --tier pass`; existing `//src/physics:test_physics` edge suite (esp. golden ULP cases **885922662**-class pole plain-17 at |o|~541).
- **No Fast-as-oracle:** verify with Fidelity `replay_driver` only.
- **Timeouts / truncated / ranks** are out of scope as “physics fails.”
- **Branch:** implement on `physics/investigate-100pct-and-latest-scrape` (or a child branch off it). Prefer small commits after each green task.

## File structure (touched)

| File | Responsibility |
|---|---|
| `src/physics/fidelity_math.h` | `applyFidelityThrust` nextafter lattice (β cluster) |
| `src/physics/fidelity_world_step.h` | bounce / TOI / post-bounce commit (γ residual only if needed) |
| `src/physics/test_physics.cpp` | Unit knife-edge tests for each new lattice branch + bounce isolation |
| `src/physics/BUILD.bazel` | Only if new test target needed (prefer existing `test_physics`) |
| `docs/artifacts/LATEST_FAILS_FORENSICS.md` | Append “closed” section when each fail goes green |
| `battles/latest_battles/README.md` | Update verify numbers when 100% |
| Scratch only | Full verify logs — not committed binaries |

## Fail inventory (source of truth)

| ID | GATE turn | Class | First EXACT seed | Lever |
|---:|---:|---|---|---|
| 895340085 | 93 | β pure N | t87 vel Δvx=−1 thr200 | pure_short_na \|100\| thr≥200 |
| 895345570 | 99 | β 3-4-5 | t95 vel Δvx=−1 face 126.87° thr200 | 0.6-axis −80 +sy mid |
| 895429566 | 90 | β→α | t81 vel Δvx=−1 face 53.13° thr200 | 0.6-axis −20 +sy large |
| 895515899 | 60 | β pole | t51 vel Δvy=+1 thr200 | pole +20 \|o\|≥543 |
| 895564994 | 47 | β pure S | t34 vel Δvx=+1 thr47 | pure_short_na \|40\| thr&lt;100 plain |
| 895612448 | 239 | β 3-4-5 | t233 vel Δvx=−1 face 53.13° thr200 | 0.6-axis −60 small other |
| 895637720 | 297 | β 3-4-5 | t290 vel Δvx=−1 face −126.87° thr200 | 0.6-axis −120 large other |
| 895131867 | 48 | γ bounce | t42 pos Δy=−1 after coll 3/0 | worldBounce / post-bounce |

**WIP note:** Working tree may already contain β lattice edits in `fidelity_math.h` that clear 7/8. This plan still requires **tests first**, then **confirm/land** the lattice, then bounce work. Never ship lattice without unit + Gate A + golden.

---

### Task 1: Baseline freeze and working-tree inventory

**Files:**
- Read: `docs/artifacts/LATEST_FAILS_FORENSICS.md`
- Read: `src/physics/fidelity_math.h` (note uncommitted WIP)
- Scratch: write baseline command outputs only

**Interfaces:**
- Consumes: existing `sim/replay_driver`, GATE policy
- Produces: documented baseline numbers for implementer (8 fails or WIP-reduced fails)

- [ ] **Step 1: Capture git + physics state**

```bash
cd /Users/samsi/csb/mad_pod_arena
git branch --show-current
git status -sb
git diff --stat src/physics/fidelity_math.h src/physics/fidelity_world_step.h
```

Expected: branch under `physics/…`; possible uncommitted `fidelity_math.h` WIP.

- [ ] **Step 2: Ensure driver**

```bash
bazel build //src/physics:replay_driver
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver
chmod +x sim/replay_driver
```

Expected: `sim/replay_driver` executable.

- [ ] **Step 3: Baseline verify of the 8 fail ids (GATE)**

```bash
for id in 895131867 895340085 895345570 895429566 895515899 895564994 895612448 895637720; do
  echo "==== $id ===="
  python3 sim/compare_battle.py "battles/latest_battles/battle_${id}.json" --gate-tolerances 2>&1 | tee "/tmp/baseline_${id}.log" | tail -6
done
```

Record how many still show `!!! MISMATCH` vs `PERFECT MATCH`. If WIP already clears 7, note residual is only `895131867`.

- [ ] **Step 4: Stash or keep WIP deliberately**

If WIP is present and untested:

```bash
# Option A (recommended): leave WIP in tree but do not commit until Task 3–4 green
# Option B: stash, re-baseline committed, then re-apply after unit tests written
git stash push -m "wip-beta-lattice" -- src/physics/fidelity_math.h
```

Implementer chooses A unless reviewing pure committed behavior. Document choice in commit message of Task 4.

- [ ] **Step 5: Commit only docs if needed**

Usually no commit in this task. If you created a baseline note file under `docs/artifacts/`, commit separately:

```bash
git add docs/artifacts/LATEST_FAILS_FORENSICS.md  # if previously untracked
git commit -m "docs: track latest-battles GATE fail forensics"
```

---

### Task 2: Unit tests for β thrust ULP knife-edges (TDD — expect fail on committed lattice)

**Files:**
- Modify: `src/physics/test_physics.cpp` (add cases before `test_fidelity_edge_cases` registration)
- Modify: `src/physics/fidelity_math.h` only if tests require a tiny test hook (prefer **no** hook — call `applyFidelityThrust` + `frictionTrunc` directly)

**Interfaces:**
- Consumes: `csb::applyFidelityThrust`, `csb::frictionTrunc`, `csb::kDegToRad` (or radians directly)
- Produces: new static test functions invoked from `test_fidelity_edge_cases()`

**Angle helpers (use exact 3-4-5 / cardinal radians):**
- Pure N: `angle = M_PI/2` (cc=0, cs=1)
- Pure S: `angle = -M_PI/2` (cc=0, cs=-1)
- Face 53.13010235415598°: `atan2(0.8, 0.6)`
- Face 126.86989764584402°: `atan2(0.8, -0.6)`
- Face −126.86989764584402°: `atan2(-0.8, -0.6)`

- [ ] **Step 1a: Measure pre-thrust seeds from the real harness**

For each of the 7 β battle ids, produce **measured** constants (not invented):

```text
pre: vx, vy, angle_rad, thr
post_gt: vx, vy   # GT keyframe after that turn's friction (first EXACT miss field)
```

Sources (use any one, all must match harness):
1. Forensics: `…/implementer/forensics/isolation_summary.txt` + `exact_committed_<id>.log`
2. New tool: create `tools/extract_thrust_seed.py` that loads the battle, steps `CppPhysics` until first EXACT vel mismatch, prints a C++ snippet with doubles copied from sim state **before** thrust and GT after friction.

```bash
# Example when tool exists:
python3 tools/extract_thrust_seed.py \
  --ids 895340085,895345570,895429566,895515899,895564994,895612448,895637720 \
  --dir battles/latest_battles \
  | tee /tmp/latest_beta_seeds.txt
```

Commit `tools/extract_thrust_seed.py` if created (reuses `sim.battle_parser`, `sim.physics_driver`, EXACT tolerances from `sim.tolerance_policy`).

- [ ] **Step 1b: Write seven unit tests + one regression in `test_physics.cpp`**

Template (repeat per id; paste **only** harness-measured numbers):

```cpp
// Battle 895340085 — first EXACT t87; pure N thr200 short-axis nextafter (forensics).
// Numbers from extract_thrust_seed / isolation_summary (measured).
static void test_latest_895340085_thrust_seed() {
    double vx = /* measured pre-thrust */;
    double vy = /* measured pre-thrust */;
    const double ang = /* measured radians after rotate */;
    const int thr = 200;
    csb::applyFidelityThrust(vx, vy, ang, thr);
    vx = csb::frictionTrunc(vx);
    vy = csb::frictionTrunc(vy);
    EXPECT_EQ_D(vx, /* GT vx from exact log */);
    EXPECT_EQ_D(vy, /* GT vy from exact log */);
    std::cout << "latest_895340085_thrust_seed: ok\n";
}
```

Also add:

```cpp
// Regression: pole +20 with |other|≈541 must stay PLAIN (885922662 family).
// Capture seed from golden / existing edge suite; EXPECT fric 17 not 16.
static void test_regression_pole_pos20_other_below_543_plain() {
    // measured seed ...
    std::cout << "regression_pole_pos20_other_below_543_plain: ok\n";
}
```

**Rule:** Every expected `EXPECT_EQ_D` value must appear in a harness GT log for that battle/turn.

- [ ] **Step 2: Register tests in `test_fidelity_edge_cases()`**

```cpp
static void test_fidelity_edge_cases() {
    // ... existing ...
    test_latest_895340085_from_isolation();
    test_latest_895345570_from_isolation();
    test_latest_895429566_from_isolation();
    test_latest_895515899_from_isolation();
    test_latest_895564994_from_isolation();
    test_latest_895612448_from_isolation();
    test_latest_895637720_from_isolation();
    test_regression_pole_pos20_other541_plain();
}
```

- [ ] **Step 3: Run unit tests against pure committed lattice (expect FAIL if WIP stashed)**

```bash
# If WIP was stashed:
git stash push -m "wip" -- src/physics/fidelity_math.h  # if needed
bazel test --config=ci //src/physics:test_physics --test_output=errors
```

Expected: FAIL with `EXPECT_EQ_D` mismatch on at least one new latest test **if** lattice not yet correct. If WIP remains and tests pass immediately, still keep tests — they become regression locks.

- [ ] **Step 4: Commit tests only (if they fail on committed code)**

```bash
git add src/physics/test_physics.cpp
git commit -m "test(physics): latest_battles β ULP knife-edge units (expect lattice fix)"
```

If tests only pass with WIP, skip commit until Task 3 lands lattice + tests together.

---

### Task 3: Land β lattice in `applyFidelityThrust` (make unit tests pass)

**Files:**
- Modify: `src/physics/fidelity_math.h` (`applyFidelityThrust` only)
- Test: `src/physics/test_physics.cpp` (from Task 2)

**Interfaces:**
- Consumes: existing `exact_prod`, `pure_short_na`, pole/3-4-5 blocks
- Produces: lattice matching CG for the 7 β seeds; **no API change**

**Known correct lattice edits (from forensics WIP — apply carefully):**

1. **Pole +20:** nextafter only if `|other| >= 543` (895515899); keep plain for ~541 (885922662).
2. **`pure_short_na` |100|:** plain only if `thrust < 200 && |other| < 150` (895340085 thr200 wants na).
3. **`pure_short_na` |40|:** plain all `thrust < 100` (895564994 thr47).
4. **3-4-5 0.6-axis want_na expand:**
   - `sx==-120` / `sy==-120`: `|o|` upper to **900** (895637720)
   - `sx==-80 && sy>0 && |o|∈[100,150)` (895345570) — **sign-sensitive**
   - `sx==-60 && |o|<150` (895612448)
   - `sx==-20 && sy>0 && |o|>=400` (895429566) — **sign-sensitive**
   - Mirror for y_is_06 with signs swapped accordingly

- [ ] **Step 1: Apply lattice (or unstash WIP) into `fidelity_math.h`**

If WIP already matches forensics map:

```bash
git stash pop   # only if stashed in Task 1
```

Else edit `applyFidelityThrust` in `src/physics/fidelity_math.h` following the WIP map in `LATEST_FAILS_FORENSICS.md` § “WIP patch map”. Keep battle-id comments on each branch.

- [ ] **Step 2: Run unit tests**

```bash
bazel test --config=ci //src/physics:test_physics --test_output=errors
```

Expected: `test_physics: ALL_PASSED_v4_edge` (or updated banner) — all new + old edges green.

- [ ] **Step 3: GATE-verify the 7 β battles individually**

```bash
for id in 895340085 895345570 895429566 895515899 895564994 895612448 895637720; do
  python3 sim/compare_battle.py "battles/latest_battles/battle_${id}.json" --gate-tolerances 2>&1 | tail -4
done
```

Expected: each ends with `=== PERFECT MATCH (GATE) through all … turns ===`

- [ ] **Step 4: Gate A + golden regression**

```bash
bazel build //src/physics:replay_driver
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
```

Expected: Gate A Failed:0; golden pass-tier 100%.

- [ ] **Step 5: Commit**

```bash
git add src/physics/fidelity_math.h src/physics/test_physics.cpp
git commit -m "fix(physics): thrust ULP lattice for latest_battles β seeds (7/8 GATE)"
```

---

### Task 4: Full latest_battles verify after β land (measure residual)

**Files:**
- Scratch log only
- Update: `docs/artifacts/LATEST_FAILS_FORENSICS.md` (mark β closed)

- [ ] **Step 1: Full corpus verify**

```bash
python3 -u sim/verify_battles.py battles/latest_battles 2>&1 | tee /tmp/verify_latest_after_beta.log
```

Expected (target after Task 3): **Failed ≤ 1** (only `895131867` if bounce remains); ideally Failed:0 if bounce already coincidentally fixed (unlikely).

- [ ] **Step 2: Confirm residual list**

```bash
grep '^FAIL:' /tmp/verify_latest_after_beta.log
```

Expected: at most  
`FAIL: battle_895131867.json …`

- [ ] **Step 3: Update forensics doc**

Append a section:

```markdown
## Status after β lattice land (YYYY-MM-DD)
- 7 β battles: GATE perfect
- Residual: 895131867 (bounce) only — see Task 5+
```

- [ ] **Step 4: Commit doc update**

```bash
git add docs/artifacts/LATEST_FAILS_FORENSICS.md
git commit -m "docs: mark latest_battles β cluster closed under GATE"
```

---

### Task 5: Bounce residual `895131867` — reproduce and isolate

**Files:**
- Read: `src/physics/fidelity_world_step.h` (`worldBounce`, `newCollideTime`, `simulateFidelityWorld`)
- Scratch: cascade dumps
- Modify later: `fidelity_world_step.h` and/or post-bounce path in math only if proven

**Interfaces:**
- Consumes: `simulateFidelityWorld`, `worldBounce`
- Produces: written isolation of first EXACT miss at **turn 42** (pos Δy=−1 on pod0 after coll 3/0)

- [ ] **Step 1: Exact and GATE compare**

```bash
python3 sim/compare_battle.py battles/latest_battles/battle_895131867.json --exact 2>&1 | tee /tmp/exact_895131867.log
python3 sim/compare_battle.py battles/latest_battles/battle_895131867.json --gate-tolerances 2>&1 | tee /tmp/gate_895131867.log
```

Expected: first EXACT near turn **42**; GATE at **48**.

- [ ] **Step 2: Dump turn 41–48 state with a small Python script (harness-driven)**

Create scratch script (do not commit unless useful long-term) that:
1. Loads battle via `sim.battle_parser.load_battle`
2. Steps `CppPhysics` turn-by-turn
3. Prints per pod sim vs gt pos/vel/ang for turns 41–48
4. Prints GT collision metadata if available in keyframe

```bash
python3 /tmp/dump_895131867_window.py 2>&1 | tee /tmp/dump_895131867.txt
```

- [ ] **Step 3: Classify bounce seed**

Fill this checklist from dump:
- [ ] Is first EXACT **only pos** (vel/angle exact)? (forensics says yes)
- [ ] Is victim thr **0** that turn? (forensics: pod0 thr0)
- [ ] Collision pair **3/0**, mid-turn TOI?
- [ ] Does error grow only after subsequent rams (2/0, 3/0)?

- [ ] **Step 4: Hypothesis rank (pick one primary)**

| H | Mechanism | Where to look |
|---|---|---|
| H1 | Post-bounce position separation epsilon / half-overlap | `worldBounce` `dd <= 800` branch |
| H2 | Impulse force uses min-impulse branch wrong for this pair | `force < kMinImpulse` |
| H3 | Shieldtimer mass 0.1 wrong on a pod | `shieldtimer == 4` checks |
| H4 | TOI order tie-break differs from CG | scan order i=3..1 |
| H5 | endTurn round/trunc after partial-time integrate | `worldEndTurnPod` after multi-coll |

- [ ] **Step 5: Write a minimal failing unit/replay test**

Prefer **replay micro-test**: extract init state at turn 41 from harness and one step with the four actions of turn 42; assert post-step pod0.y matches GT.

If pure unit is possible:

```cpp
// Pseudocode — fill doubles from dump_895131867.txt
static void test_latest_895131867_bounce_seed_turn42() {
    // Build WorldPod[4] from GT keyframe 41
    // Apply same velocity increments as post-applyMove for turn 42
    // Call simulateFidelityWorld for one turn OR call worldBounce once with measured TOI
    // EXPECT pod positions match GT keyframe 42 within 0 (exact) on first fail field
}
```

- [ ] **Step 6: Commit isolation notes + failing test**

```bash
git add src/physics/test_physics.cpp docs/artifacts/LATEST_FAILS_FORENSICS.md
git commit -m "test(physics): isolate 895131867 bounce seed (turn 42)"
```

---

### Task 6: Fix bounce residual (only if Task 5 proves lever)

**Files:**
- Modify: `src/physics/fidelity_world_step.h` (preferred) **or** a single documented post-bounce snap in math if isolation shows commit-only mismatch
- Test: `src/physics/test_physics.cpp`

**Interfaces:**
- Must not change bounce mass semantics except for proven CG mismatch
- Must re-run Gate A + golden + all 8 + full latest

- [ ] **Step 1: Implement minimal fix for proven H\***

Example only if H1 proven (do **not** apply blindly):

```cpp
// In worldBounce — only change what isolation proved.
// Example pattern: adjust separation epsilon application order, or
// re-round positions after bounce to match CG integer commit timing.
```

**If no safe minimal fix:** document blocker in forensics and keep battle in a `expected_fail` list for latest corpus **without** claiming 100%. Plan exit then becomes “7/8 latest hard-fails closed; 1 bounce deferred” — **not** the full goal. Prefer fix.

- [ ] **Step 2: Unit / micro-replay test green**

```bash
bazel test --config=ci //src/physics:test_physics --test_output=errors
```

- [ ] **Step 3: Single-battle GATE+EXACT**

```bash
python3 sim/compare_battle.py battles/latest_battles/battle_895131867.json --gate-tolerances
python3 sim/compare_battle.py battles/latest_battles/battle_895131867.json --exact
```

Expected: PERFECT MATCH GATE (and EXACT if claimed).

- [ ] **Step 4: Full regressions**

```bash
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
python3 -u sim/verify_battles.py battles/latest_battles 2>&1 | tee /tmp/verify_latest_final.log
```

Expected latest:

```
Failed:           0
Turn accuracy:    100.00%
```

- [ ] **Step 5: Commit**

```bash
git add src/physics/fidelity_world_step.h src/physics/test_physics.cpp
git commit -m "fix(physics): bounce seed for 895131867; latest_battles GATE 100%"
```

---

### Task 7: Exit verification package (definition of done)

**Files:**
- Update: `battles/latest_battles/README.md`
- Update: `docs/artifacts/PHYSICS_100PCT_LATEST_INVESTIGATION.md` (status section)
- Update: `docs/artifacts/LATEST_FAILS_FORENSICS.md` (all 8 closed)

- [ ] **Step 1: Full exit suite (copy/paste block)**

```bash
set -euo pipefail
cd /Users/samsi/csb/mad_pod_arena
bazel test --config=ci //src/physics:test_physics --test_output=errors
bazel build //src/physics:replay_driver
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
python3 -u sim/verify_battles.py battles/latest_battles 2>&1 | tee /tmp/verify_latest_exit.log
# Optional breadth:
# python3 -u sim/verify_battles.py battles/leaderboard_battles 2>&1 | tee /tmp/verify_lb_exit.log
grep -E 'Failed:|Turn accuracy|ALL .* PASSED' /tmp/verify_latest_exit.log
```

**Must observe for latest:**
- `Failed:           0`
- `Turn accuracy:    100.00%`
- Passed count = tested count (skips only for non-battles if any)

**Must observe for Gate A:**
- `Failed:           0` under `--gate`

- [ ] **Step 2: Update README numbers**

In `battles/latest_battles/README.md`, set GATE verify line to **2320 pass / 0 fail / 100%** (or current tested count).

- [ ] **Step 3: Final commit**

```bash
git add battles/latest_battles/README.md docs/artifacts/*.md
git commit -m "docs: latest_battles GATE 100% turn correctness achieved"
```

---

### Task 8 (optional stretch): EXACT progress without blocking GATE exit

**Not required for goal exit.** If time remains:

- [ ] Run `--exact` on the 8 battles; file any EXACT-only fails as separate research.
- [ ] Do not change GATE to chase EXACT.

---

## Self-review

| Spec need | Task |
|---|---|
| 100% turn correctness on new downloads | Tasks 3–4 (β), 5–6 (bounce), 7 (full verify) |
| Evidence-based, harness truth | Tasks 1, 5; forensics as input |
| No GATE loosen | Global constraints + Task 7 grep |
| No Gate A / golden regression | Tasks 3.4, 6.4, 7.1 |
| TDD / unit locks | Tasks 2–3, 5–6 |
| Bounce separate from ULP | Tasks 5–6 isolated |
| Docs updated | Tasks 4, 7 |

**Placeholder scan:** Steps require filling pre-state doubles from forensics isolation dumps (real files under implementer/forensics). That is intentional data binding, not “TBD logic.” Implementer must paste numbers from harness dumps, not invent them.

**Risk callout:** β lattice is multi-predicate; one wrong band regresses old goldens (885922662 pole plain). Task 2 regression test + Task 3.4 golden are mandatory.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-11-latest-battles-100pct-turn-correctness.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks, fast iteration  
2. **Inline Execution** — execute tasks in this session with checkpoints  

Which approach?
