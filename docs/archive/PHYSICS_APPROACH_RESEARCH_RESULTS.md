# Thorough approach research — what was actually run (2026-06-28)

Branch: `physics/max-fidelity`. This document **completes** research items previously oversold as “tried” but not executed.

## 1. Global config bake-off (8 knobs) — already measured

See `logs/fidelity_milestones/approach_bakeoff.json`.

| Result | Meaning |
|---|---|
| Best **gate-safe** | `rot_snap_tight_1deg` (shipped `e0dd9db8`) |
| Best **TDD cases if ignore locks** | incremental / no-vel-snap → **1/10** (`891370461`) + **gate A/B fail** |
| Clears TDD 10/10? | **No config did** |

## 2. One-step GT isolation (NEW — actually run)

**Method:** Full replay until first GATE fail at turn `T`. Then:

1. Seed pods from **GT keyframe `T-1`** (pos/vel/ang/timeouts) with **global `next`** taken from our sim at `T-1` when local CP agrees.  
2. Apply **only** battle actions for turn `T`.  
3. Compare to GT keyframe `T` under GATE.

**Interpretation:**

| One-step result | Meaning |
|---|---|
| **PASS** (no GATE error) | Fail turn’s rotate/thrust/collide is **OK from correct state**; full-replay fail is **prior sub-GATE drift** |
| **FAIL** (GATE error remains) | Bug is **isolated to that turn’s functions** even with perfect prior GT state |

### Results (all 10 TDD battles)

| Battle | First **any** drift turn | First **GATE** fail turn | One-step from GT@T−1 | Class |
|---|---:|---:|---|---|
| **885827873** | 83 | 86 | **PASS** | **Prior drift** (angle class on fail line is consequence) |
| **885928301** | 82 | 86 | **PASS** | **Prior drift** |
| **886469116** | 121 | 130 | **PASS** | **Prior drift** |
| **887715689** | 416 | 422 | **PASS** | **Prior drift** |
| **887820683** | 166 | 174 | **PASS** | **Prior drift** |
| **891370461** | 307 | 313 | **PASS** | **Prior drift** (pos Δy=6 from earlier vel) |
| **885912413** | **21** | **21** | **FAIL** | **This-turn functions** (early; not only t69 narrative) |
| **886449550** | **147** | **147** | **FAIL** | **This-turn functions** |
| **890666841** | **139** | **147** | **FAIL** | **This-turn** (drift starts 139, still fails one-step @147) |
| **890670385** | **227** | **234** | **FAIL** | **This-turn** (BOOST/SHIELD/collide) |

**Counts:** **6/10 prior-drift dominated**, **4/10 true single-turn isolation fails**.

### Implication (corrects earlier oversimplification)

Saying “**applyRotate snap-to-atan2 is the bug on the fail turn**” is **incomplete**:

- For **6 battles**, if we **seed exact GT state** at `T-1`, **turn `T` passes GATE** with **current** physics (including tight-snap rotate).  
- So the **binding problem** is **accumulated sub-GATE error** (often **angle drift** for many turns while still &lt;1°), not that turn’s formula alone.  
- For **4 battles**, even perfect GT prior state **cannot** complete turn `T` under GATE → **real bugs in that turn’s** rotate/thrust/collide path.

**First drift often precedes GATE fail by several turns** (e.g. 83→86, 121→130, 166→174, 307→313, 416→422) — classic **slow angle/vel bleed**.

## 3. Collision probe `885912413` (NEW — partial)

Pre-turn-69 pair geometry (sim state after completing turns through 68) and actions on fail turn are in `thorough_research.json` → `collision_885912413`.

Research use: pairs with **dist ≲ 800** and **approaching (`rel_dot < 0`)** are candidates for `newCollide`/`bounce` divergence; compare to GT Δpos/Δvel 68→69.

**Note:** One-step/drift analysis shows this battle **first GATE-fails at turn 21**, not 69 — earlier narrative “catastrophe at 69” was **first fail under continuous full-replay from init with GATE-only reporting**; integer/ang **drift** and **GATE** may both trip at **21** on current tip. Treat **21** as the true isolation point for this ID on current code.

## 4. Approaches still not fully tried (honest backlog)

| Item | Status after this pass |
|---|---|
| One-step GT suite | **Done (research script)** — should be promoted to `sim/tests/` |
| Config bake-off rotate/vel/disc | **Done (8 configs)** |
| Reference `namespace csb` second driver | **Still not built** |
| Full inverse-impulse solve on γ | **Still not done** (probe only) |
| Degree-space rotate | **Not tried** |
| Empirically fitted lerp(k) | **Not tried** |
| Full Go all-pods CP as scored row | **Prior ad-hoc only** (fails golden B) |

## 5. What to implement next (priority from evidence)

1. **Prior-drift class (6 battles):** find **first_drift turn** functions; fix **early** angle/vel bias (not only fail-turn rotate). One-step passing means **don’t only rewrite fail-turn snap**.  
2. **Isolated-turn class (4 battles):** debug **that turn** with GT seed — 885912413@21, 886449550@147, 890666841@147, 890670385@234.  
3. **Reference driver head-to-head** still highest value unpaid mega-experiment.  
4. Keep **gate A + golden B** as hard locks when scoring “best approach.”

## 6. Bottom line

Thorough research **changes the story**:

- **Not** “one function wrong on the printed fail line for all 10.”  
- **Yes** “**6 failures are mostly accumulated drift**; **4 are true single-turn physics bugs**.”  
- Global rotate knobs **cannot** clear the 6 if they only change the last turn; need **earlier** divergence fixes.  
- Bake-off still shows **no gate-safe config clears TDD 10/10**.

Artifact: `logs/fidelity_milestones/thorough_research.json`
