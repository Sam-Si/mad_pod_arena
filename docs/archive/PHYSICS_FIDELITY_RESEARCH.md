# Physics fidelity — thorough research report

> **SNAPSHOT STATUS (systematic verification 2026-06-28):** This document is an accurate **historical** measurement of the pre–`1f483cee` research pass (semantics of scores/tooltips/layers, gate 100% turns, C++ tol mismatch).  
> **Stale counts after tip `1f483cee` / `0e0141ae`:** golden turn fails are **10** not 12 (`882151685` and `885624120` fixed); divergences turn-perfect **~34/44** not 32/44; “outcome fails = mostly oracle” **no longer applies** to the **3** golden outcome residuals (those co-occur with **real turn fails**).  
> **Live dashboard:** [`PHYSICS_SUPER_ACCURACY.md`](PHYSICS_SUPER_ACCURACY.md). **Do not use** [`PHYSICS_FIDELITY_MILESTONES.md`](PHYSICS_FIDELITY_MILESTONES.md) old ~87% outcome figures (that file is now refreshed or superseded).


**Branch:** `physics/max-fidelity`  
**Date:** 2026-06-28  
**Method:** Live runs of C++ `physics.h` via `replay_driver` + Python harness; full scans of gate corpora; forensics on every residual fail; 500-battle leaderboard sample; tooltip/score/rank analysis; cross-check with Go referee and in-repo docs.

This document replaces hand-wavy claims with **measured** evidence and **falsifiable** hypotheses.

---

## 1. Research questions

1. Can C++ Fidelity match **every turn** of **every retained battle** under GATE tolerances?
2. Can physics-derived **winners** match CG **`ranks`** on every battle (M1)?
3. Why does any gap remain — engine bug, comparer policy, or **non-physics CG metadata**?
4. What is the correct milestone order toward “perfection”?

---

## 2. What was actually executed (not inferred)

| Experiment | Scope | Result (GATE pos≤5, vel≤3, ang≤1°, timeout≤1, exact `next_cp`) |
|---|---|---|
| Full `test_session_battles` | **312/312** battles | **Turn-perfect 312/312**, turns **46364/46364 = 100%** |
| Full `golden_physics_battles` | **200/200** | Turn-perfect **188/200 (94%)**, turn-acc **97.20%** |
| Full divergences folder | **44/44** | Turn-perfect **32/44**, turn-acc **91.24%** |
| Leaderboard **random 500** (seed 42, root `battle_*.json` only) | **500** of **17521** | Outcome **488/500 (97.6%)**, turn-perfect **490/500 (98.0%)**, turn-acc **99.18%** |
| Outcome fails deep JSON | **18** test_session cases | All **turn-perfect**; taxonomy below |
| Turn fails deep JSON | **12** golden IDs | Per-turn field deltas recorded |
| Scores histogram | **312** test_session | Dominant `(0,2)` / `(2,0)` — platform points, not “1 = race win” only |
| Tooltip parse | residual outcomes | **`invalid action`** and **dual elimination** explain most CG vs progress fights |

Artifacts: `logs/fidelity_milestones/{baseline_gate,outcome_fail_deep,turn_fail_deep,leaderboard_sample500}.json`.

**Not yet run in this research pass:** full **17 521** leaderboard (would be ~8–15 minutes at ~30 battles/s; sample is unbiased enough to bound rates). Timeout corpora excluded by design (agent failure, not physics).

---

## 3. Critical semantic split (why “outcomes” confused us)

CodinGame `gameResult` JSON mixes **three layers**:

| Layer | Fields | Physics-replicable? |
|---|---|---|
| **A. Keyframe state** | pod `x,y,vx,vy,angle,next_cp`, timeouts in `view` | **Yes** — this is Fidelity’s job |
| **B. Referee end-of-race rules** | finish (`won`), sole team timeout, 500-turn progress | **Yes** — implementable in C++ / harness |
| **C. Platform match outcome** | `ranks`, `scores`, **tooltips** (`invalid action`, dual “did not reach checkpoint”) | **Partially** — often **not** equal to (B) alone |

### 3.1 Evidence that `scores` / `ranks` are not pure progress

On **312** test_session battles, `scores` patterns:

| Pattern | Count | Interpretation |
|---|---:|---|
| `(0.0, 2.0)` | 124 | Winner gets **2** platform points (league / multi-game style) |
| `(2.0, 0.0)` | 116 | Symmetric |
| `(0.0, 0.0)` | 31 | Both zero — often **dual elimination** |
| `(1.0, 2.0)` / `(2.0, 1.0)` | 41 | Non-binary point spreads |

**240/312** are “one player 0, other ≥2” — **not** a simple 1–0 race scoreboard. Using `ranks` alone without tooltips **mis-specifies M1**.

### 3.2 Tooltip-grounded outcomes (decisive research finding)

On residual “physics progress ≠ CG ranks” cases with **perfect turns**, tooltips show:

| Example battle | Tooltip / view | CG ranks winner | Physics progress winner | Explanation |
|---|---|---:|---:|---|
| `891669868` | **Both** `$0` and `$1` “did not reach the next checkpoint in time”; scores `[0,0]` | 0 | 1 (distance) | **Dual elimination**; CG still ranks P0 above P1 with **0–0 scores** |
| `891670128` | `$0: invalid action` | 1 | 0 (more CPs) | **Invalid action loss** for P0 — not progress |
| `891670250` | `$1: invalid action` + view `InvalidInput` | 0 | 1 | **Invalid action loss** for P1 |
| `891684936` | `$1: invalid action` + `InvalidInput` | 0 | 1 | Same |
| `891685190` | `$0: invalid action` (only **11** turns) | 1 | 0 | Early agent fault; state still turn-matched |
| `891670242` | Only `$0` checkpoint timeout | 1 | 0 (equal next=2, timeouts −1/0) | **Single elimination** of P0 |

So a large class of “outcome fails” is **correct keyframe physics** + **wrong M1 oracle** (we compared progress to `ranks` while CG ended on **rules B+C**).

**M1 definition must be:**

1. If tooltip **invalid action** on player `i` → winner = `1-i` (even if progress disagrees).  
2. If tooltip **exactly one** “did not reach checkpoint” on `i` → winner = `1-i`.  
3. If **both** eliminated → use `ranks` (platform tie order) or document as draw with forced ranking.  
4. Else finish (`won`) / sole timeout / Go progress.

With tooltip rules (measured in follow-up script): **~97%+** agreement on test_session is achievable **without changing `physics.h`**. Remaining gaps need more tooltip variants or true physics bugs on non–turn-perfect sets.

---

## 4. Turn accuracy research (M2) — where C++ still diverges

### 4.1 Gate corpus is already perfect under GATE

**test_session: 100% turns.** Any claim “C++ can’t do 100% anywhere” is **false for the merge-gate corpus under GATE_***.

### 4.2 The 12 golden / divergence turn fails (live forensics)

All paths resolved under golden or divergences folders. First failing turn (0-based) and **dominant field** (GATE breach):

| Battle | Fail turn | Primary breach | Character |
|---|---:|---|---|
| `885827873` | 86 | **angle 1.135°** only | Barely over 1°; pos≤3, vel≤1 — **pure angle / ULP** |
| `885928301` | 86 | **angle 1.024°** only | Same class |
| `887820683` | 174 | **angle 1.143°** | Angle-only |
| `887715689` | 422 | **angle 1.426°** | Very late; ang accumulated (max ang° saw 12° internally before gate trip on one pod) |
| `886469116` | 130 | **angle 3.31°** | Angle with pos still ≤5 |
| `891370461` | 313 | **pos Δy=−6** | Just over GATE pos 5; vel OK |
| `886449550` | 356 | **pos Δ=(4,6)** | Just over; ang noise present |
| `890670385` | 234 | **pos/vel** small | Δpos 7, Δvel 4 |
| `890666841` | 147 | **pos + ang** | Mixed small |
| `882151685` | 137 | **pos/vel multi-pod** | Collision aftermath (pod3 vel Δ 15) |
| `885624120` | 216 | **pos/vel catastrophe** | Δy −76, Δvy −102 — **branch flip** |
| `885912413` | 69 | **catastrophe multi-pod** | Δ 100+ units, ang 18°+ — **collision topology** |

**Taxonomy of M2 residue:**

1. **Class α — Sub-degree / ~1° angle drift (5 battles)**  
   - Mechanism: double angle never committed; `sin`/`cos` + rotate accumulation; `snapNearInteger` on thrust is platform-tuned (macOS arm64 comments in `physics.h`).  
   - GATE ang tol = 1.0° → fails at 1.02–1.4°.  
   - **Not** wrong CP or wrong collision on first fail line.  
   - Fix path: higher-fidelity angle integration **without** regressing 312/312 (historically fragile).

2. **Class β — Single-axis position 6–11 units (3–4 battles)**  
   - Integer commit off-by-one cascade after almost-right doubles.  
   - Often co-travel with mild vel/ang error.  
   - Fix path: find **earliest** double divergence (binary search turns), not only first GATE breach.

3. **Class γ — Catastrophic collision branch (2 battles: `885624120`, `885912413`)**  
   - One turn: tens–hundreds of units wrong → earlier `newCollide` on/off or impulse differ.  
   - Highest value for “true” physics bugs; highest regression risk.

**Timeouts and `next_cp` matched on first fail for all 12** in this run — residue is **not** primarily CP policy on these IDs (CP class was largely fixed earlier; former divergences promoted to pass).

### 4.3 Leaderboard sample bounds

On **500** random root leaderboard battles:

- **98%** battles fully turn-perfect under GATE  
- **99.18%** of turns matched  
- **~2%** battles have ≥1 bad turn (≈10 battles in sample → order **~350** battles if rate holds on 17.5k — consistent with historical ~44 tracked divergences order of magnitude, not “thousands of wrong games”)

So C++ Fidelity is **already extremely close** on breadth; perfection is a **long-tail** problem.

### 4.4 C++ `verify_battles` vs Python GATE (process, not physics)

`comparePod` defaults **0.01** pos/vel and **0.001 rad (~0.057°)** angle. That is **orders of magnitude stricter** than GATE and stricter than integer commit granularity. Mass “C++ verifier fails” **do not** prove mass physics failure; they prove **comparer mismatch**. Authoritative merge path remains Python GATE + golden pass tier.

---

## 5. Why 100% on *everything* is hard (refined, evidence-based)

### 5.1 Engine / numerics (real)

- Continuous-time multi-collision; knife-edge distance ~800.  
- Angle is non-committed double; long horizons (400+ turns) amplify 1 ULP.  
- Fixes are **multi-objective**: `physics.h` comments document regressions (snap band, friction ±100, post-bounce snap, inclusive CP).  
- No vendored Java referee — oracle is **keyframes**, not bit-identical server FPU.

### 5.2 Oracle / product semantics (real, under-appreciated)

- **`ranks`/`scores` encode platform match results**, including invalid actions and ranking after dual timeout — **orthogonal** to “did our doubles match keyframes?”  
- PRED_ASSERT in stderr is **bot prediction logging**, not CG server truth (noise for research).  
- Mid-turn CP policy in `physics.h` is **viewer-tuned**, not pure Go — intentional for gate A.

### 5.3 Scope inflation (process)

- Counting timeout folders, Fast/GA simulators, or C++ 0.01 verifier as “physics accuracy” **inflates failure**.  
- Dual physics (Fidelity vs GA Fast) means **submit bot kinematics ≠ gate physics** by design.

---

## 6. Milestone status (research-backed)

| Milestone | Target | Status |
|---|---|---|
| **M1 outcomes** | 100% vs CG winner | **Blocked on wrong metric if using progress-only vs `ranks`.** Tooltip-aware rules explain most residuals; implement in harness → expect **≥97%** test_session without `physics.h` changes; remainder = edge tooltips / true physics on imperfect-turn battles |
| **M2 turns (GATE)** | 100% battles | **Done on test_session (312).** Golden **188/200**. Leaderboard sample **~98%** battles |
| **M2b turns (strict)** | EXPLORE ±1 or C++ 0.01 | **Not done**; angle class α fails first |
| **M3 full leaderboard** | Measure all 17.5k | **Sample only**; full run pending |
| **M4 perfection** | M1+M2 everywhere | Requires α/β/γ fixes + correct M1 oracle |

**Revised priority (against prior narrative):**

1. **Fix M1 oracle** (tooltips + elimination + ranks on dual elim) — measurement honesty.  
2. **M2 class α** angle (largest count of “almost perfect” fails).  
3. **M2 class γ** collision catastrophes (true hard physics).  
4. **M2 class β** small pos.  
5. Full leaderboard measurement + promote golden fails as they die.

Turn-perfect on gate set is **ahead of** outcome alignment when outcomes are defined as CG `ranks` without tooltips — the opposite of “physics is mostly wrong.”

---

## 7. Implications for “keep going until perfection”

- **Perfection of keyframe replication (M2)** is the right physics goal; gate corpus shows it is **achievable at GATE on 46k turns**.  
- **Perfection of `ranks` (naive M1)** requires **platform rules**, not only `simulateWorld`.  
- **Perfection under 0.01/0.001rad** is a **different, harder** problem (angle commit / libm), not the same as GATE perfection.  
- Remaining **~2%** leaderboard battles and **12** golden fails are the **entire** known M2 debt under GATE — a finite, enumerable set, not an unknown void.

---

## 8. Data files for audit

| File | Contents |
|---|---|
| `logs/fidelity_milestones/baseline_gate.json` | First suite totals |
| `logs/fidelity_milestones/outcome_fail_deep.json` | 18 outcome forensics |
| `logs/fidelity_milestones/turn_fail_deep.json` | 12 turn fail field deltas |
| `logs/fidelity_milestones/leaderboard_sample500.json` | 500-battle sample rates |
| `docs/PHYSICS_FIDELITY_MILESTONES.md` | Working milestone plan |
| `docs/PHYSICS_FIDELITY_RESEARCH.md` | This report |

---

## 9. Conclusion

Skepticism was warranted on **unmeasured** statistics. After thorough **measured** research:

1. **C++ Fidelity already achieves 100% turn accuracy on the full CI gate corpus (312 battles / 46 364 turns) under GATE.**  
2. **Breadth is ~98% battle-perfect / ~99.2% turns on a 500-battle leaderboard sample** — long tail, not systemic failure.  
3. **Most “outcome” disagreement is CG tooltip/platform semantics** (invalid action, dual elimination, 2-point scores), **not** failed keyframe replication.  
4. **Finite M2 debt:** 12 characterized divergences (angle / small pos / 2 collision disasters).  
5. Path to perfection is **tooltip-correct M1 + systematic α→γ physics fixes**, with gate A held green — not a vague rewrite.

