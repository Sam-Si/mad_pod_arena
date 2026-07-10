# Physics Super-Accuracy — Complete Findings, Fixes & Quest Log

| Field | Value |
|---|---|
| **Branch** | `physics/max-fidelity` |
| **Tip (at writing)** | `1f483cee` — reference ultimate-engine fidelity hints |
| **Canonical Fidelity code** | [`src/physics/physics.h`](../src/physics/physics.h) (`namespace csb`) |
| **Driver** | [`src/physics/replay_driver.cpp`](../src/physics/replay_driver.cpp) → `sim/replay_driver` |
| **Harness** | [`sim/verify_battles.py`](../sim/verify_battles.py), [`sim/verify_quick_accuracy.py`](../sim/verify_quick_accuracy.py) |
| **Governance** | [`docs/VERIFICATION_TRUTH_POLICY.md`](VERIFICATION_TRUTH_POLICY.md) |
| **Goal** | Replicate CodinGame Mad Pod Racing / Coders Strike Back physics as closely as possible — **super-strict**, not “good enough for bots only” |

This is the **single narrative document** for everything learned and changed on the fidelity quest: methodology mistakes, measured baselines, outcome vs turn milestones, reference-engine ports, remaining debt, and how to keep going until perfection.

Related shorter docs (do not contradict this one; this file is the master log):

- [`PHYSICS_FIDELITY_MILESTONES.md`](PHYSICS_FIDELITY_MILESTONES.md) — milestone table  
- [`PHYSICS_FIDELITY_RESEARCH.md`](PHYSICS_FIDELITY_RESEARCH.md) — research appendix (tooltips, scores)  
- [`physics-verification.md`](physics-verification.md) — long-form verified rules  

---


## 0. Systematic verification (docs vs live code) — 2026-06-28

**Method:** Phase-1 evidence first (cross-check docs ↔ `logs/fidelity_milestones/*` ↔ harness ↔ fresh runs on `physics/max-fidelity`). Implementation follows only after doc hygiene + labeled M1.

| Document | Accuracy | Role today |
|---|---|---|
| [`PHYSICS_FIDELITY_RESEARCH.md`](PHYSICS_FIDELITY_RESEARCH.md) | Strong on **semantics** (scores, tooltips, layers, gate 100% turns, C++ tol mismatch). **Stale counts** post-`1f483cee` (12→**10** fails; divergences 32→**~34**/44 turn-perfect) | Appendix |
| [`PHYSICS_FIDELITY_MILESTONES.md`](PHYSICS_FIDELITY_MILESTONES.md) | Was **materially stale** (~87% M1, “12 divergences”, obsolete “next actions”) — **refreshed + superseded banner** | Short index only |
| **This file** | Closest to live tip | **Master log** |

### Live gate truth (re-verified)

| Check | Result |
|---|---|
| test_session M1+M2 (GATE, tooltip-aware) | **312/312** outcomes, **312/312** turn-perfect, **46364/46364** turns |
| CI Gate A | **312/312** |
| CI Gate B (golden pass 188) | **188/188** |
| golden all 200 | outcomes **197/200**, turn-perfect **190/200**, turns **~97.51%** |
| divergences (44 battles) | turn-perfect **~34/44**, outcomes **~41/44** |

### Claim ledger (verified)

| Claim | Verdict |
|---|---|
| A. test_session 100% turn-perfect under GATE | **True** (reproduced) |
| B. Outcomes lag at ~87% while turns perfect | **Historical only** (progress-only metric in `baseline_gate.json` 272/312); **live M1 = 100%** with tooltips |
| C. scores/ranks ≠ pure progress; (0,2)/(2,0) dominate | **True** — histogram **124 / 116 / 31 / 41**, series-like **240/312** exact match |
| D. Residual outcome fails were platform tooltips | **True** for cited test_session examples; **not** for **3** golden outcome residuals (co-occur with **turn** fails) |
| E. ~97%+ outcomes via tooltips without `physics.h` | **Met and exceeded** on gate (100%); **caveat:** dual-elim M1 uses **CG ranks fallback** (~31 battles) — partly tautological |
| F. 12 characterized turn fails | **Was true**; now **10** after reference ports |

### M1 honesty (mandatory)

Harness prints `M1_MODE=tooltip_aware`. “100% outcomes” means alignment with `log.winner` under tooltip + finish + timeout + progress + **ranks on dual elimination** — **not** “physics independently predicted every platform ranking without reading CG metadata.”

### What is going on (root narrative)

1. Keyframe replication (M2) on the **merge gate is solved under GATE** — strongest reproducible claim.  
2. Outcome confusion was mostly **oracle design** on test_session — verified via tooltips and scores.  
3. Docs are multi-generation; trust **live harness + this master log** over stale MILESTONES prose (now patched).  
4. Remaining work = **10 concrete battles** + unknown full-leaderboard tail — not “physics broken everywhere.”  
5. Perfection under **0.01 / 0.001 rad** is a **different** problem than GATE perfection.


## 1. Mission statement

We want **super-accurate physics**: given exact init state and exact player actions from a real CodinGame battle JSON, our C++ Fidelity engine must reproduce **the same committed keyframe state** (and, where defined, the **same match outcome**) as the CG server / viewer.

**Order of operations (locked):**

1. **M1 — Battle outcomes** first (correct winner under CG rules)  
2. **M2 — Turn accuracy** second (every turn within tolerance)  
3. **M3 — Breadth** (full leaderboard + stricter tolerances)  
4. **M4 — Perfection** (M1+M2+M3 on all retained non-timeout corpora)

**Non-goals for this branch:** GA Elo, Fast search speed, bot heuristics, SSOT docs-only churn — unless required to prove Fidelity.

---

## 2. What “accurate” means (three layers we mixed up — and fixed)

CodinGame `gameResult` JSON is **not** one oracle. It has **three layers**:

| Layer | What it is | Physics-replicable? | How we measure |
|---|---|---|---|
| **A. Keyframe state** | Pod `x,y,vx,vy,angle,next_cp` + timeouts in keyframe `view` | **Yes — primary Fidelity job** | `verify_battles.py` / `verify_quick_accuracy.py` turn loop |
| **B. Referee race rules** | Finish (`won`), sole team timeout, 500-turn progress | **Yes** | `pod.won`, timeouts, Go progress score |
| **C. Platform match outcome** | `ranks`, `scores`, **tooltips** (`invalid action`, dual elimination) | **Only with tooltip/platform rules** | Must parse tooltips; ranks alone mislead |

### 2.1 Early mistake (corrected by research)

We initially treated **CG `ranks` vs “max `next` progress”** as the M1 metric. That produced **false physics failures**:

- **18/312** test_session battles were **100% turn-perfect** but “failed outcomes.”  
- Tooltips showed **`$i: invalid action`** (that player loses) or **both** “did not reach the next checkpoint” with **scores `[0,0]`** while ranks still ordered P0 above P1.  
- **`scores` like `(0,2)` / `(2,0)`** dominate the corpus (**240/312**) — platform points, not a simple 1–0 progress scoreboard.

**Fix:** M1 winner = tooltips first → finish/`won` → sole timeout → Go progress → CG `ranks` on dual elimination.  
**Result:** **312/312 outcomes + 312/312 turn-perfect** on test_session (measured).

### 2.2 Gate tolerances (merge truth)

From `sim/tolerance_policy.py` (frozen unless policy + checker co-PR):

| Constant | Value | Field |
|---|---:|---|
| `GATE_POS_TOL` | **5.0** | position axes |
| `GATE_VEL_TOL` | **3.0** | velocity axes |
| `GATE_ANG_TOL_DEG` | **1.0°** | shortest arc |
| `GATE_TIMEOUT_TOL` | **1** | per player |
| `next_cp` | **exact** | `sim.next % n_cp == gt.next_cp` |

**C++ diagnostic** `verify_battles.cpp` uses **0.01 / 0.01 / 0.001 rad** — **not** merge-authoritative. Mass fails under that tool ≠ mass Fidelity failure.

**EXPLORE_*** (±1 pos/vel) is for forensics, not CI gate.

### 2.3 Frame / turn mapping (must not get wrong)

```
Frame 0        = init keyframe (NOT a game turn)
Frame 2T+1     = player 0 actions for turn T
Frame 2T+2     = player 1 actions + keyframe AFTER turn T
game_turn = (frame_index - 1) // 2
```

One game turn = **two** JSON frames. Wrong framing invents “physics bugs.”

---

## 3. Authoritative sources (ranked)

| Rank | Source | Role |
|---:|---|---|
| 1 | Real CG battle **keyframes** (gate corpora) | Empirical commit oracle |
| 2 | `src/physics/physics.h` | In-repo Fidelity SSOT — **edit here only** for CG match |
| 3 | `docs/VERIFICATION_TRUTH_POLICY.md` + `sim/tolerance_policy.py` | What “green” means |
| 4 | High-fidelity **reference CG-bot paste** (`namespace csb` in ultimate engine) | Numerics / rotate / thrust / CP knife-edges |
| 5 | Go referee `third_party/referees/coders-strike-back-referee/csbref.go` | Algorithm sketch — **not** identical on every knife-edge |
| 6 | `docs/physics-verification.md` | Narrative (may lag code; verify against `physics.h`) |
| 7 | `docs/rules.md` | Player rules — **wrong on boost sharing** |
| 8 | Fast / GA / standalone bot physics | **Not** gate oracle |

**Official Java `Referee.java` is not vendored** — we cannot bit-diff the server FPU.

---

## 4. Corpora inventory (what we actually run against)

| Corpus | ~Count | Role | Blocks merge? |
|---|---:|---|---|
| `battles/test_session_battles/` | **312** | Gate **(A)** primary | **Yes — 100% required** |
| `battles/golden_physics_battles/` | **200** | Stratified regression; **188** pass tier = Gate **(B)** | Pass tier yes |
| `battles/quick_physics_accuracy/` | **200** | Fast local loop (≈ golden IDs) | No |
| `battles/leaderboard_physics_divergences/` | **44** + sidecars | Knife-edge curriculum | No |
| `battles/leaderboard_battles/` | **~17 521** root (+ rank subdirs) | Breadth | Nightly / research |
| `*_timeouts/` | hundreds | Agent timeouts — **exclude from physics %** | Retention only |

**Retention:** battle id **> `870230019`** (`battles/RETENTION.md`).

**Compound merge gate (`physics-accuracy`):**

```text
(U)  bazel test --config=ci //src/physics:test_physics
(A)  MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
(B)  MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
```

Copy Bazel `//src/physics:replay_driver` → `sim/replay_driver` first.

---

## 5. Live measured baselines (honesty log)

All numbers below were produced by **running** the harness on this machine/branch — not copied from stale docs alone.

### 5.1 Current tip (`1f483cee`) — headline scores

| Corpus | Outcomes (tooltip M1) | Turn-perfect battles | Turn accuracy |
|---|---:|---:|---:|
| **test_session (312)** | **312/312 = 100%** | **312/312 = 100%** | **46364/46364 = 100%** |
| **golden all (200)** | **197/200 = 98.5%** | **190/200 = 95%** | **35513/36420 ≈ 97.51%** |
| **golden pass tier (188)** | — | **188/188 gate B** | — |
| **Leaderboard sample 500** (seed 42, earlier commit) | **488/500 = 97.6%** | **490/500 = 98.0%** | **99.18%** turns |

### 5.2 Progression on this branch

| Commit | What changed | test_session turns | Golden turn-perfect |
|---|---|---:|---:|
| `7ec5cacb` | SSOT: arena on `csb::Game`, delete `csb_physics.h` | (prior) 312/312 claimed | — |
| `f525470c` | Outcome harness: `won` + Go progress | 100% turns; outcomes ~87%→94% | 188/200 |
| `7f972f4b` | Research + **tooltip M1** | **100% outcomes + turns** | 188/200 |
| `1f483cee` | **Reference ultimate-engine hints** in `physics.h` | **100% maintained** | **190/200** (+2) |

### 5.3 What we did **not** run yet

- Full **17 521** leaderboard root (sample only; full run is the next breadth measurement).  
- Full C++ `verify_battles` at 0.01 tol as success metric (intentionally non-authoritative).

---

## 6. Fidelity rules we treat as true (C++ Fidelity)

Summarized from battles + Go + reference engine + `physics.h` comments.

### 6.1 Constants

| Name | Value |
|---|---:|
| Pod radius | 400 (collide centers ≤ 800, `rsq = 640000`) |
| CP radius | 600 (`rsq = 360000`, pass if path **strictly inside** segment test) |
| Max thrust | 200 |
| Boost | 650 once **per pod** |
| Max rotate | 18°/turn |
| Friction | `trunc(v * 0.85)` |
| Position commit | `floor(p + 0.5)` |
| Min impulse | 120 (floor or double) |
| Shield timer | 4; inv-mass **0.1** only when timer **== 4** (activation frame) |
| Timeout | team 100; reset stores **101** so post-`--` frame shows 100 |
| Default laps | 3; `globalCp = laps×track + track[0]` |
| Max game turns (CG) | 500 then progress tiebreak |

### 6.2 Per-pod action order (then world step)

1. Parse move (`SHIELD` / `BOOST` / int thrust).  
2. **InvalidInput** (thrust &lt; 0 or &gt; 200, non-keyword): **no** rotate, thrust, or shield (battle-verified; **differs** from reference paste that treats negative as shield).  
3. SHIELD → `shieldtimer = 4`; BOOST consumes per-pod flag (even if shield zeros thrust).  
4. Shield cooldown forces thrust 0.  
5. **target == position** → skip rotate+thrust (shield already applied).  
6. **First rotate** (`!hasRotated`): face **exact** `atan2(target)` (reference-aligned).  
7. Else rotate ±18° with trunc-normalized delta; within limit **snap angle to target atan2**.  
8. Thrust along facing with **trig pole snap** + velocity ULP snap.  
9. World: continuous collision sweep; bounce; mid-turn CP on **bounced** pods only; `endTurn`; end CP; optional **exact `dist² == CP_RSQ`** pass; timeouts `--`.

### 6.3 Intentional divergences from pure Go

| Topic | Go | Our Fidelity (viewer / battles) |
|---|---|---|
| First rotate | Global `turnCount==0` | **Per-pod** `hasRotated` |
| Mid-turn CP | Often all pods | **Bounced pods only** + end segment for others |
| Invalid input | May lose game in harness | Soft invalid in replay path |
| Maps | 13 jittered | Live maps in JSON; tournament **18** in `src/core/maps/catalog.h` |

A line-by-line Go port **fails gate A**. Hybrid is required.

---

## 7. All findings (organized)

### Finding F1 — Gate corpus is already turn-perfect under GATE

**Evidence:** Repeated full runs of 312 battles / 46 364 turns → **100%**.  
**Implication:** Systemic “C++ physics is wrong everywhere” is **false** for the merge definition. Remaining work is **long-tail knife-edges**.

### Finding F2 — “Outcome fails” were mostly oracle semantics

**Evidence:** 18 turn-perfect battles disagreed on winner until tooltips were applied → **0** outcome fails on test_session.  
**Classes:**

- Invalid action tooltip → opponent wins (even if progress favors loser).  
- Dual elimination tooltips + scores `[0,0]` → use **CG ranks** for platform ordering.  
- Single elimination tooltip → opponent wins.  
- Scores `(0,2)` / `(2,0)` = platform points, not progress.

### Finding F3 — Dual physics is real and must not pollute Fidelity

| Implementation | Purpose | Gate? |
|---|---|---|
| `csb::Game` in `physics.h` | CG Fidelity | **Yes** |
| `PhysicsSimulator` (`engine`) | Degrees, Fidelity-ish | No |
| `GAPhysicsSimulator` / GA in bot | Fast search (mass 10, double impulse, often no CP) | No |
| `cg_bot` `CG_STANDALONE` inline | Submit paste Fast | No |
| Reference paste `ultimate_ga` | Bot search | No |

Arena outcomes already use Fidelity `Game` post-SSOT. **Search Fast is allowed to differ.**

### Finding F4 — Long-tail turn fails are finite and classifiable

After reference ports, **10** golden battles still fail turns (was 12). Taxonomy:

| Class | Nature | Examples |
|---|---|---|
| **α Angle** | First fail ~1.0–3.3°; pos often still inside GATE | `885827873`, `885928301`, `887820683`, `887715689`, `886469116` |
| **β Small pos** | Δ 6–11 on one axis | `891370461`, `886449550`, `890670385`, `890666841` |
| **γ Collision catastrophe** | Huge Δ in one turn (branch flip) | **`885912413` remains**; `885624120` & `882151685` **fixed** by reference hints |

Timeouts / `next_cp` often still match on the **first** GATE-breach turn — root may be earlier sub-GATE drift.

### Finding F5 — Multi-objective numerics (Pareto surface)

Documented in `physics.h` comments and history:

- Snap band `4e-14` on velocity; `1e-12` regresses gate A.  
- Skip snap at `|round(v)|==180`; skipping 160 regresses another pass battle.  
- `nextafter` on exact ±100 friction regresses gate A (`885827873` class still hard).  
- Post-bounce velocity snap flipped `885912413`-class outcomes.  
- Inclusive CP `<=` falsely passes `884515945` t9; strict `<` is required for segment test; **exact equality on committed position** is a separate post-pass.

**Every “clever” fix must re-run (U)+(A)+(B).**

### Finding F6 — Reference ultimate engine is a Fidelity goldmine (with traps)

The user-supplied CG-bot monolith contains a strong `namespace csb` Fidelity core **plus** Fast GA. We **ported Fidelity numerics only**.

**Do not port blindly:**

- Negative thrust → shield (loses our invalid-input battles).  
- Fast `CP_RADIUS_SQ = 358801` search fudge.  
- Double impulse / mass 10 GA bounce as “Fidelity.”

### Finding F7 — Leaderboard breadth is ~98% battle-perfect under GATE (sample)

500 random root battles: **98%** fully turn-perfect, **99.18%** turns. Order-of-magnitude **~2%** long tail (~350 battles if uniform on 17.5k) — consistent with “tens to low hundreds of divergences,” not thousands.

### Finding F8 — Documentation lag is real

Stale claims (agent_pack 161/39 golden, “44 all fail,” boost not consumed during shield in one paragraph) **must not** override live runs + `physics.h`.

---

## 8. All fixes applied on `physics/max-fidelity` (chronological)

### Fix set A — SSOT foundation (`7ec5cacb`, main → branch base)

- Deleted experimental `src/engine/csb_physics.h` / `test_physics.cpp`.  
- Arena runs **`csb::Game` Fidelity** (`step({Fidelity})`).  
- Introduced `src/core/` (constants, progress, 18-map catalog).  
- Verification policy + tolerance module + quick accuracy corpus scaffolding.  
- **Preserved** frozen gate job id and `GATE_*` numbers.

### Fix set B — Measurement & M1 oracle (`f525470c`, `7f972f4b`)

| Change | File(s) | Why |
|---|---|---|
| Expose `pod.won` on STEP lines | `replay_driver.cpp`, `physics_driver.py` | Detect race finish |
| Go progress score | `verify_quick_accuracy.py` | `next * 1e6 - dist(globalCp[next])` |
| **Tooltip-aware winner** | `verify_quick_accuracy.py` | Invalid action / elimination / dual-elim ranks |
| Milestone + research docs | `docs/PHYSICS_FIDELITY_*.md` | Honest metrics |

**Result:** test_session **M1+M2 = 100%** under GATE.

### Fix set C — Reference ultimate-engine Fidelity hints (`1f483cee`)

Ported into `src/physics/physics.h` from the high-fidelity `namespace csb` paste:

| Technique | Detail |
|---|---|
| **Rotate** | `delta - 2π·trunc(delta/2π)`, clamp ±18°, else `angle = atan2(target)` |
| **First rotate** | `angle = atan2(target)` (no `angle=0; diffAngle` detour) |
| **Thrust trig snap** | If `(cos,|sin|)` within `5e-16` of `(-0.28, 0.96)`, force exact pole; then velocity ULP snap |
| **`newCollide`** | `disc <= 0` → no collision (tangent) |
| **Exact CP radius** | After `endTurn`, if `dist² == 600²` and not `won`, `passCheckpoint` |
| **`setPodState` / hasRotated** | `hasRotated = false` only for init sentinel `≈ -0.0174533` or `≈ 0` |

**Kept (battle-proven, contradicts reference paste):**

- Invalid thrust: **no** shield activation.  
- Segment CP: **strict** `<` (plus exact equality pass only on **committed** position).  
- Mid-turn CP: **bounced pods only** (viewer-tuned).

**Measured gain:** golden turn-perfect **188 → 190/200**; fixed **`882151685`** and **`885624120`** (class γ). Gate A/B **still green**.

---

## 9. Remaining debt (explicit kill list)

### 9.1 Turn fails (M2) — 10 battles

| Battle ID | Last known first fail (0-based) | Primary class |
|---|---:|---|
| `885827873` | 86 | α angle ~1.1° |
| `885928301` | 86 | α angle ~1.0° |
| `886469116` | 130 | α angle ~3.3° |
| `887820683` | 174 | α angle ~1.1° |
| `887715689` | 422 | α angle ~1.4° (very late) |
| `890666841` | 147 | β/α pos+ang |
| `890670385` | 234 | β pos/vel |
| `886449550` | 356 | β pos |
| `891370461` | 313 | β pos Δy=6 |
| `885912413` | 69 | **γ catastrophe** (Δ 100+, ang 18°+) |

**Outcome-only leftovers on golden (3)** often co-occur with turn fails (`885912413`, `887820683`, `890666841`) — fixing turns may auto-fix outcomes.

### 9.2 Breadth (M3)

- Re-run **full** `leaderboard_battles` root with `verify_quick_accuracy.py --gate --recursive` (long).  
- Refresh `leaderboard_physics_divergences` manifests (some rows stale vs promotions).  
- Optionally align C++ diagnostic tolerances with GATE for less confusion (process fix).

### 9.3 Strict perfection (M2b / M4)

- EXPLORE ±1 pos/vel on gate set.  
- Angle commit strategy that does not regress 312/312 (research-grade; high risk).  
- Obtain or instrument real CG server behavior if ever available.

---

## 10. Quest playbook (how we keep going to super-accuracy)

### 10.1 Invariants (never violate)

1. Edit **CG-matching behavior only in `src/physics/physics.h`** (plus driver/harness for I/O).  
2. After every physics edit, run **(U) + (A) + (B)** before celebrating.  
3. Prefer **root-cause turn bisect** over tolerance widening.  
4. Do not claim 100% without a **fresh** corpus run.  
5. Do not “fix” Fast/GA and call it Fidelity.

### 10.2 Per-bug loop

```bash
# 1. Build driver
bazel build //src/physics:replay_driver //src/physics:test_physics
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver

# 2. Forensic one battle
python3 sim/compare_battle.py battles/golden_physics_battles/battles/battle_885827873.json
# or with gate tolerances:
python3 sim/compare_battle.py --gate-tolerances path/to/battle.json

# 3. Change physics.h (minimal)

# 4. Gate
bazel test --config=ci //src/physics:test_physics
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass

# 5. Score M1+M2 on golden / quick
python3 sim/verify_quick_accuracy.py --gate --dir battles/golden_physics_battles/battles

# 6. If a former fail is solid green on full golden, promote expected_fail → pass in manifest
```

### 10.3 Priority order for the 10

1. **α angle cluster** (5) — shared rotate/thrust ULP; highest leverage if one fix hits many.  
2. **β small pos** (4) — often secondary to angle; bisect for first double divergence.  
3. **`885912413` γ** — collision topology; highest risk; isolate pair times vs GT.  
4. Full leaderboard measurement + manifest refresh.  
5. Stricter tolerances only after GATE 200/200.

### 10.4 Success criteria for “done”

| Level | Definition |
|---|---|
| **Ship-grade Fidelity** | M1+M2 100% on test_session **and** golden pass tier; divergences folder empty or all pass; documented sample leaderboard ≥99.5% battles |
| **Super-accurate** | Above + golden **200/200** turn-perfect + full leaderboard ≥99.9% battles under GATE |
| **Bit-exact fantasy** | Every double matches CG server FPU — **not claimed** without server binary |

---

## 11. Architecture snapshot (Fidelity path only)

```
CG battle JSON
    → sim/battle_parser.py  (frames → init, turns, keyframes, tooltips, ranks)
    → sim/physics_driver.py (stdin protocol)
    → replay_driver  →  csb::Game in physics.h
         applyAction ×4  →  nextTurn / simulateWorld
    → compare vs keyframe (GATE_*)
    → outcome: tooltips → won → timeout → progress → ranks
```

**Do not** use `GAPhysicsSimulator` or standalone Fast for gate claims.

---

## 12. Reference ultimate-engine — port decision matrix

| Feature in paste | Ported? | Reason |
|---|---|---|
| `SNAP_COS` / `SNAP_SIN` thrust | **Yes** | Class α / ULP |
| Trunc rotate + snap to target | **Yes** | Long-horizon angle |
| First rotate = atan2 target | **Yes** | Matches high-fidelity bot |
| `disc <= 0` in `newCollide` | **Yes** | Tangent collisions |
| Exact `dist² == CP_RSQ` after commit | **Yes** | Measure-zero CP |
| Init angle sentinel → first turn | **Yes** | `hasRotated` seeding |
| Mid-turn CP on collision participants | Already had | Viewer-tuned |
| Bounce inv-mass 0.1 @ timer==4 | Already had | Go-aligned |
| Negative thrust = shield | **No** | Regresses invalid-input battles |
| Thrust &gt;200 early return without rotate | Partial / ours stricter | `invalid_input` |
| Unrolled 6-pair scan | No (equiv. nested loops) | Same order possible |
| AVX / O3 pragmas | No | Build flags elsewhere |
| Fast GA / beam / NN | No | Not Fidelity |
| `CP_RADIUS_SQ = 358801` search | No | Fast-only fudge |

---

## 13. Known contradictions & traps (read before “fixing”)

1. **`docs/rules.md`**: boost “shared between pods” — **false** for CG (per pod).  
2. **`physics-verification.md`**: one paragraph said boost not consumed under shield — **false** vs Go + our code.  
3. **Go first-turn global vs per-pod hasRotated** — battles win; Go loses on some traces.  
4. **C++ 0.01 verifier** — fails many GATE-pass battles; ignore for M2 claims.  
5. **PRED_ASSERT in stderr** — bot prediction logs, **not** CG truth.  
6. **Agent_pack snapshots** — stale fail counts.  
7. **Widening GATE** to “get 100%” is **not** super-accuracy; it is metric fraud.

---

## 14. Command cheat sheet

```bash
git checkout physics/max-fidelity
git pull

bazel build //src/physics:replay_driver //src/physics:test_physics
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver

# Full compound gate
bazel test --config=ci //src/physics:test_physics
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
python3 sim/check_verification_policy.py

# M1 + M2 dashboard
python3 sim/verify_quick_accuracy.py --gate --dir battles/test_session_battles
python3 sim/verify_quick_accuracy.py --gate --dir battles/golden_physics_battles/battles
python3 sim/verify_quick_accuracy.py --gate --dir battles/leaderboard_physics_divergences/battles

# Single battle
python3 sim/compare_battle.py battles/golden_physics_battles/battles/battle_885912413.json
```

---

## 15. Commit map (this quest)

| SHA | Summary |
|---|---|
| `7ec5cacb` | SSOT: Fidelity arena, core, policy (branch base from main) |
| `f525470c` | M1 harness: `won` + Go progress; milestones doc |
| `7f972f4b` | Deep research; tooltip M1 → **312/312 outcomes+turns** |
| `1f483cee` | Reference Fidelity hints → **190/200** golden turns; −2 divergences |

---

## 16. Bottom line

1. **Super-accuracy is achievable as a finite engineering problem**, not an infinite mystery: gate corpus is **already 100%** under GATE; golden has **10** characterized turn fails left.  
2. **Outcomes required platform semantics (tooltips)**; without that we lied to ourselves about physics.  
3. **Reference ultimate-engine `namespace csb`** supplied real CG numerics (rotate/thrust/CP) that fixed **two catastrophe battles** without losing gate A/B.  
4. **Do not confuse Fast/GA, C++ 0.01 diagnostics, or ranks-without-tooltips with Fidelity failure.**  
5. **Path to perfection:** kill class α → β → `885912413` γ; promote golden fails; full leaderboard measure; only then tighten tolerances.

This document is the quest log. Update it when a fail is killed or a measurement is refreshed — **with commands and counts, not vibes.**

---

*End of PHYSICS_SUPER_ACCURACY.md — master fidelity log for `physics/max-fidelity`.*
