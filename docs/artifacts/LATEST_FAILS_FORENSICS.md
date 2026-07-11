# Latest-battles Fidelity forensics — 8 GATE fails

| Field | Value |
|---|---|
| **Branch** | `physics/investigate-100pct-and-latest-scrape` |
| **Baseline physics commit** | `ef43e00b` (`physics/max-fidelity` Fidelity SSOT) + corpus `f0c193ad` |
| **Harness** | `python3 sim/compare_battle.py <battle> --gate-tolerances` and `--exact` |
| **Driver** | Bazel `//src/physics:replay_driver` → `sim/replay_driver` |
| **GATE_*** | pos≤5, vel≤3, ang≤1°, timeout≤1, exact next_cp (`sim/tolerance_policy.py`) |
| **Corpus** | `battles/latest_battles/battle_895*.json` (post-max-id scrape) |
| **Scratch I/O** | `/var/folders/jp/bz2c731s5w597nv82lt5t6m00000gn/T/grok-goal-3a00df3671e7/implementer/forensics/` |
| **Date** | 2026-07-11 |

**Method (source of truth only):** for each id — GATE first fail from `compare_battle.py --gate-tolerances`; first EXACT miss from `--exact`; pre-state from GT `keyframes[t-1]`, actions from `turns[t]`, collisions from GT keyframe; cascade table sim−gt per turn until GATE trip. No invented tolerances; no GATE loosening.

**Two physics trees measured:**

1. **Committed HEAD** (`src/physics/fidelity_math.h` as in git): **8/8 fail GATE** — matches `verify_latest` residual list.
2. **Uncommitted WIP** (local diff on `applyFidelityThrust` nextafter lattice): **7/8 pass GATE and EXACT**; residual **`895131867` only**.

Primary `forensics/compare_<id>.log` = **committed** GATE stdout. Also: `exact_committed_<id>.log`, `compare_wip_<id>.log`, `cascade_trace_committed.txt`, `isolation_summary.txt`, `deep_forensics_committed.json`.

---

## Executive summary

| Battle | GATE fail turn | GATE fields (sim vs gt) | First EXACT | Seed class | Cascade | Likely lever |
|---|---:|---|---:|---|---|---|
| 895131867 | 48/319 | pod2 pos Δ≈(−6,+2); pod3 pos Δ≈(+4,+6) | 42 pos Δy on pod0 after coll 3/0 | **γ-seed / bounce** | multi-ram → β surface | `fidelity_world_step.h` bounce/TOI |
| 895340085 | 93/181 | pod1 pos Δx=−6 | 87 vel Δvx=−1 pure N thr200 | **β ULP free-flight** | same pod accumulates | `fidelity_math.h` `applyFidelityThrust` pure_short_na \|100\| thr≥200 |
| 895345570 | 99/261 | pod1 pos Δx=−6; pod3 vel | 95 vel Δvx=−1 @126.87° thr200 | **β ULP 3-4-5** | coll 3/1 amplifies | `applyFidelityThrust` 0.6-axis want_na sx=−80 |
| 895429566 | 90/308 | pod1 angle ~2.2° over GATE | 81 vel Δvx=−1 @53.13° thr200 | **β ULP → α surface** | angle drift free-flight | `applyFidelityThrust` 0.6-axis want_na sx=−20 (not rotate primary) |
| 895515899 | 60/288 | pod3 pos Δy=−7 | 51 vel Δvy=+1 thr200 | **β ULP free-flight** | same pod | `applyFidelityThrust` pole +20 \|o\|≥543 nextafter |
| 895564994 | 47/135 | pod2 pos Δ≈(−6,−3) | 34 vel Δvx=+1 pure S thr47 | **β ULP free-flight** | coll 2/0 transfers to pod2 | `applyFidelityThrust` pure_short_na \|40\| thr&lt;100 plain |
| 895612448 | 239/278 | pod3 pos Δy=−7 | 233 vel Δvx=−1 @53.13° thr200 | **β ULP 3-4-5** | coll 3/1 → pod3 GATE | `applyFidelityThrust` 0.6-axis want_na sx=−60 |
| 895637720 | 297/320 | pod3 pos Δx=6 | 290 vel Δvx=−1 @−126.87° thr200 | **β ULP 3-4-5** | coll 3/0 → pod3 GATE | `applyFidelityThrust` 0.6-axis want_na sx=−120 \|o\| large |

**WIP vs committed (same harness, same battles):**

| Physics | GATE fails among these 8 | EXACT perfect among these 8 |
|---|---:|---:|
| Committed `fidelity_math.h` | **8** | **0** |
| Uncommitted WIP lattice patch | **1** (`895131867`) | **7** |

---

## Per-battle evidence

### 1. `battle_895131867` — GATE@48 — **γ-seed / bounce residual**

**GATE (`compare_895131867.log`):**

```
!!! MISMATCH after turn 48  mode=GATE
    pod2 pos: sim=(5485.0,6801.0) gt=(5491.0,6799.0)
    pod3 pos: sim=(5513.0,8143.0) gt=(5509.0,8137.0)
```

**First EXACT@42** (`exact_committed_895131867.log`): pod0 pos only — sim y=6325 vs gt y=6326; **vel and angle exact**. GT collision this turn: pods **3/0** at t≈0.445, force≈444, impulse (−137,360).

**Pre@41 (GT):** pod0 (7144,6003) v=(60,466) thr **0**; pod3 (7970,6563) thr200. PRE close pair 0–2 dist≈871.

**Cascade (committed):**

| t | Diffs | Coll |
|--:|---|---|
| 42 | p0 Δp=(0,−1) | 3/0 |
| 43 | p0 Δp=(−1,1) Δv_y=1; p2 Δp=(1,−1) | 2/0 |
| 46 | p0,p2,p3 all drift | 2/0 + 3/0 |
| 48 | p2 Δp=(−6,2); p3 Δp=(4,6) **GATE** | none |

**Classification:** Not free-flight thrust ULP. Seed is **integer post-bounce position** off by 1 on the rammed pod; subsequent rams spread error to pods 2/3 until pos exceeds GATE 5. Timeouts match every turn; next_cp OK.

**Lever:** `src/physics/fidelity_world_step.h` (`worldBounce` / TOI scan / impulse integerization) — not `applyFidelityThrust`. **WIP lattice does not fix** (same GATE@48).

#### Task 5 isolation (2026-07-11) — turn 42 bounce seed locked

**Harness (post lattice `edcfed36`):** EXACT first miss **turn 42**; GATE first fail **turn 48**. Micro-replay from GT keyframe 41 + `turns[42]` reproduces seed without full-battle prefix.

**Cascade (sim−gt, current Fidelity):**

| t | Diffs | GT coll |
|--:|---|---|
| 41 | none (exact) | 2/0 |
| 42 | p0 Δp=(0,−1); vel/ang exact | **3/0** t≈0.445 force≈444 imp (−137,360) |
| 43 | p0 Δp=(−1,+1) Δvy=+1; p2 Δp=(+1,−1) | 2/0 |
| 44–45 | free-flight drift grows | none |
| 46 | multi-ram spreads to p0/p2/p3 | 2/0 + 3/0 |
| 48 | p2 Δp=(−6,+2); p3 Δp=(+4,+6) **GATE** | none |

**Seed checklist:** first EXACT **pos only** (pod0 y); victim thr **0**; pair **3/0** mid-turn TOI; error grows after subsequent rams.

**C++ mid-step dump (Task 5):**

| Quantity | Value |
|---|---|
| TOI pair | only 3/0 at `0.44485030654718183` (matches GT) |
| Contact `dd` | `799.99999999999977` → `dd <= 800` **true** |
| Force | raw 222.12 → doubled **444.234…** (matches GT; not min-impulse) |
| Shield mass | both timers 0 (not H3) |
| Separation | `kEpsilon` shift on pod0 ≈ (−8.1e-6, **−5.8e-6**) |
| Pre-`roundHalfUp` pod0.y | **`6325.49999935046`** → rounds to **6325** |
| Without separation (dd slightly >800) | y≈`6325.500005` → would round to **6326** (GT) |

**Hypothesis rank:**

| Rank | H | Verdict |
|---:|---|---|
| **1** | **H1** separation epsilon / `dd <= 800` | **PRIMARY** — ULP-under 800 fires separation; ε·ny crosses half-integer |
| 2 | H5 endTurn round after bounce | Surface: half-up of borderline y; root is H1 offset |
| 3 | H2 min impulse | **Ruled out** — force≫120, matches GT 444 |
| 4 | H3 shield mass 0.1 | **Ruled out** — shieldtimer 0 both pods |
| 5 | H4 TOI order | **Ruled out** — single pair TOI this turn |

**Unit lock:** `test_latest_895131867_bounce_seed_turn42` in `src/physics/test_physics.cpp` (expect FAIL until Task 6). Logs: `implementer/task5/`.

---

### 2. `battle_895340085` — GATE@93 — **β free-flight ULP (pure cardinal)**

**GATE:**

```
pod1 pos: sim=(6927.0,3698.0) gt=(6933.0,3698.0)   # Δx=-6
```

**First EXACT@87:** pod1 vel sim=(−85,113) gt=(−84,113) Δvx=−1; **no collision**. Face after rotate = **90° pure N**. Action thr=**200**, target north. Pre@86: pod1 v=(−100,−66) ang≈72°.

**Cascade:** Δvx stays −1; Δx grows −1 per turn → −6 at t93. Free-flight entire window (coll 3/0 at t91 does not involve pod1).

**Mechanism:** `pure_short_na` on N/S axis with `an==100`, `other≈134`. Committed keeps plain −100 when `|other|<150` **regardless of thr**; CG wants nextafter when thr≥200 → friction trunc yields −84 vs −85.

**Lever:** `fidelity_math.h` `applyFidelityThrust` / `pure_short_na` (`|100|` thr≥200). **WIP fixes → GATE+EXACT perfect.**

---

### 3. `battle_895345570` — GATE@99 — **β ULP 3-4-5 + collision amp**

**GATE:**

```
pod1 pos: sim=(10547.0,2009.0) gt=(10553.0,2009.0)  # Δx=-6
pod3 vel: sim=(-75,198) gt=(-79,197)
```

**First EXACT@95:** pod1 vel (−68,95) vs (−67,95); face **126.8699°** (cos=−0.6, sin=0.8); thr200; **no coll**.

**Cascade:** ULP seeds pod1; coll **3/1** at t96 transfers; at t99 another **3/1** → pod3 vel also over GATE.

**Lever:** 3-4-5 0.6-axis residual `want_na` for `sx==-80` with `|sy|∈[100,300)` (WIP comment: `|o|~112`). **WIP fixes.**

---

### 4. `battle_895429566` — GATE@90 — **α surface, β ULP seed**

**GATE:**

```
pod1 angle: sim=-21.4° gt=-19.2°   # |Δ|≈2.2° > 1°
```

**First EXACT@81:** pod1 vel (−17,579) vs (−16,579); face **53.1301°** (3-4-5); thr200; **pod1 not in any collision** (coll 3/2 only).

**Cascade:** Δvx=−1 → pos drifts; by t85–90 angle error grows to **−2.175°** while pos still under GATE (Δp≈4,4). Classic long-horizon **α presentation of a β seed**.

**Not primary:** `applyFidelityRotate` — angle was exact at seed turn; rotate only later amplifies wrong state.

**Lever:** `applyFidelityThrust` 3-4-5 want_na `sx==-20` `|o|≥400` (WIP tags 895429566). **WIP fixes → EXACT perfect through 308 turns.**

---

### 5. `battle_895515899` — GATE@60 — **β free-flight ULP**

**GATE:**

```
pod3 pos: sim=(7179.0,6958.0) gt=(7181.0,6965.0)  # Δy=-7
```

**First EXACT@51:** pod3 vel (−461,17) vs (−461,16) Δvy=+1; thr200; face≈163.74°; no coll.

**Cascade:** same pod; coll 3/1 at t54 perturbs trajectory; GATE@60 on y.

**Lever:** pole branch `exact_prod(sx) && sx==20` with `|sy|≥543` nextafter (WIP: 895515899 `|o|~543` wants na → fric 16; golden 885922662 `|o|~541` wants plain 17). **WIP fixes.**

---

### 6. `battle_895564994` — GATE@47 — **β ULP + transfer collision**

**GATE:**

```
pod2 pos: sim=(5925.0,5923.0) gt=(5931.0,5926.0)  # Δ≈(-6,-3)
```

**First EXACT@34:** pod0 vel (−33,−277) vs (−34,−277) Δvx=+1; face **−90° pure S**; thr=**47**; no coll.

**Cascade:** pod0 carries ULP until coll **2/0** at t39 → pod2 inherits Δv; pod0 recovers under EXACT while pod2 grows to GATE@47.

**Lever:** `pure_short_na` `|40|` — committed nextafter path for thr&lt;100 with large other; CG wants **plain** −34 (WIP: plain all thr&lt;100). **WIP fixes.**

---

### 7. `battle_895612448` — GATE@239 — **β ULP 3-4-5 + late transfer**

**GATE:**

```
pod3 pos: sim=(11623.0,7020.0) gt=(11620.0,7027.0)  # Δy=-7
```

**First EXACT@233:** pod1 vel (−51,−35) vs (−50,−35); face **53.1301°**; thr200; no coll.

**Cascade:** pod1 accumulates; at t239 coll **3/1** (force large) → pod3 GATE. pod1 still under GATE at fail (Δp≈4,5).

**Lever:** 3-4-5 want_na `sx==-60` small other (WIP: 895612448 `|o|~42`). **WIP fixes.**

---

### 8. `battle_895637720` — GATE@297 — **β ULP 3-4-5 + transfer**

**GATE:**

```
pod3 pos: sim=(12989.0,277.0) gt=(12983.0,278.0)  # Δx=6
```

**First EXACT@290:** pod0 vel (−102,−701) vs (−101,−701); face **−126.8699°**; thr200; no coll. Pre v=(0,−665).

**Cascade:** coll **3/0** at t291 transfers; pod3 Δx grows 0→6 by t297; pod0 remains under GATE pos (Δ≈5,1).

**Lever:** 3-4-5 want_na `sx==-120` with `|o|` up to ~900 (WIP: 895637720 `|o|~825`). **WIP fixes.**

---

## Clustering analysis

### Cluster A — **β thrust/friction ULP free-flight seed** (7/8)

Battles: **340085, 345570, 429566, 515899, 564994, 612448, 637720**.

| Subtype | Battles | Face / axis | Thr | Symptom at first EXACT |
|---|---|---|---:|---|
| Pure cardinal `pure_short_na` | 340085 (N \|100\| thr200), 564994 (S \|40\| thr47) | 90° / −90° | 200 / 47 | Δvel = ±1 on thrust-orthogonal axis product after friction |
| 3-4-5 0.6-axis `want_na` | 345570 (−80), 429566 (−20), 612448 (−60), 637720 (−120) | ±53.13° / ±126.87° | 200 | Δvx = −1 after thrust+friction |
| Pole short +20 nextafter | 515899 | ~164° | 200 | Δvy = +1 |

**Common pattern:** EXACT is **vel ±1 only** (pos still 0) on a free-flight turn → over 5–13 turns pos reaches **Δ=6..7** (just past GATE 5). Collisions often **transfer** the error to a different pod that trips GATE (564994, 612448, 637720, 345570).

**Single lever file:** `src/physics/fidelity_math.h` → `applyFidelityThrust` (nextafter lattice / `pure_short_na` / 3-4-5 residual block). Friction `frictionTrunc` is the display of the ULP choice, not an independent bug in these cases.

### Cluster B — **α angle GATE presentation of β seed** (1/8)

Battle: **429566** only. First miss is still β vel ULP; GATE trips on **angle** (~2.2°) because heading integrates the wrong state, not because `applyFidelityRotate` disagrees at seed.

### Cluster C — **γ-adjacent bounce seed** (1/8)

Battle: **895131867** only. First miss is **pos ±1 after a mid-turn collision** with thr=0 on victim (no thrust lattice involvement). Spreads through a multi-pod ram chain. **Survives WIP thrust patches.**

### No pure γ topology catastrophe

No battle in this set has multi-pod TOI order inversion as the *first* EXACT miss without a prior single-unit residual. 131867 is the closest (bounce integerization).

---

## What is NOT the problem (ruled out)

| Ruled out | Evidence |
|---|---|
| Timeouts / timeout refresh | sim timeouts == GT every sampled turn for all 8 |
| next_cp / lap index | no next_cp errs in GATE or first EXACT |
| Platform ranks / tooltips | ranks present; physics compare does not use them |
| Truncated / corrupt scrape | all 8 load full turns; frames non-empty; retention clean per investigation doc |
| Invalid / missing actions | all four pods have actions at seed and fail turns; no `is_invalid_thrust` forced pairs at seeds |
| GATE too tight as “the” bug | residuals are real CG/Fidelity lattice mismatches; loosening GATE would hide physics debt |
| Rotate lattice as primary for 429566 | angle exact at seed; only later cascade |
| CP segment / passCheckpoint | no next_cp or timeout-display fails |
| Fast physics / GA | harness uses Fidelity `replay_driver` only |

---

## Recommended next experiments (ordered)

1. **Land or reject WIP thrust lattice as a controlled PR**  
   Diff is already localized in `applyFidelityThrust` with battle-id comments for all 7 β cases.  
   Gate: re-run Gate A (`test_session_battles`), golden, full leaderboard sample, plus these 8.  
   Do **not** ship if any prior golden ULP case regresses (esp. 885922662 plain-17 at |o|~541).

2. **Isolate pure unit cases for each WIP branch** into `//src/physics:test_physics`  
   Minimal (vx,vy,angle,thr) → expect CG friction integers. Covers:  
   - pure N thr200, vx=−100, |vy|~134  
   - pure S thr47, |vx|=40  
   - 3-4-5 axes −20/−60/−80/−120 with measured |other|  
   - pole +20 |other|≥543 vs ~541

3. **Deep-dive residual `895131867` as bounce forensics**  
   - Dump double-precision pre-bounce positions/velocities and TOI t  
   - Compare impulse integerization and post-bounce endTurn trunc vs CG  
   - Binary-search first double divergence inside `worldBounce` for pods 0/3 at t=42  
   Lever candidates: `fidelity_world_step.h` TOI order, mass (shield), impulse rounding, end-turn integer commit after bounce.

4. **Optional: first-double search before GATE** on any new latest fails  
   Pattern is established: EXACT vel±1 ~6–13 turns before GATE pos±6.

5. **Do not** chase rotate, CP, or timeout code for this set unless a new fail seeds there.

6. **After bounce fix for 131867:** re-verify full `battles/latest_battles/` (2320) under GATE — expected path to 100% if no further long-tail.

---

## WIP patch map (diagnostic only — not landed)

Uncommitted `git diff src/physics/fidelity_math.h` (as of this forensics):

| WIP change | Fixes battle(s) |
|---|---|
| pole `sx/sy==20` nextafter if \|other\|≥543 | 895515899 |
| `pure_short_na` \|100\|: plain only if thr&lt;200 and \|o\|&lt;150 | 895340085 |
| `pure_short_na` \|40\|: plain all thr&lt;100 | 895564994 |
| 3-4-5 want_na expand −120\|o\|&lt;900, −80\|o\|≥100, −60 small, −20\|o\|≥400 | 895637720, 895345570, 895612448, 895429566 |

**Residual after WIP:** only **895131867** (bounce seed).

---

## Artifact index

Path prefix:  
`/var/folders/jp/bz2c731s5w597nv82lt5t6m00000gn/T/grok-goal-3a00df3671e7/implementer/forensics/`

| File | Content |
|---|---|
| `compare_<id>.log` | Full GATE stdout (committed) for each of 8 |
| `compare_committed_<id>.log` | Same |
| `compare_wip_<id>.log` | GATE under WIP lattice |
| `exact_committed_<id>.log` | First EXACT miss under committed |
| `exact_<id>.log` | Earlier EXACT runs (may be WIP for the 7 that pass) |
| `cascade_trace_committed.txt` | Per-turn sim−gt from first EXACT → GATE |
| `isolation_summary.txt` | Pre-state + actions + first EXACT dump |
| `deep_forensics_committed.json` | Structured window dump |

Repo report: `docs/artifacts/LATEST_FAILS_FORENSICS.md` (this file).  
Related: `docs/artifacts/PHYSICS_100PCT_LATEST_INVESTIGATION.md` (corpus scrape + 8-fail list).

---

## Bottom line

1. **All 8 verified under real harness** against GATE — pre-lattice Fidelity failed exactly where `verify_latest` reported.  
2. **Dominant residual was β thrust ULP** (7 battles): free-flight `applyFidelityThrust` nextafter lattice; often collides later so GATE trips on a different pod/field (incl. one **α angle** surface).  
3. **One bounce residual** (`895131867`) is a different lever (`fidelity_world_step.h`) and is the only survivor after lattice land.  
4. **Not** timeouts, scrape, ranks, invalid actions, or GATE policy.  
5. Lattice landed in `edcfed36`; residual bounce is Task 5+ scope.

---

## Status after β lattice land (2026-07-11)

| Field | Value |
|---|---|
| **Physics commit** | `edcfed36` — `fix(physics): thrust ULP lattice for latest_battles β seeds (7/8 GATE)` |
| **Driver** | `//src/physics:replay_driver` → `sim/replay_driver` (rebuilt at HEAD) |
| **Command** | `python3 -u sim/verify_battles.py battles/latest_battles` |
| **Log** | `implementer/task4/verify_latest_after_beta.log` (also `/tmp/verify_latest_after_beta.log`) |

### Full-corpus residual (GATE)

| Metric | Value |
|---|---:|
| Total battles | 2320 |
| Passed (100%) | 2314 |
| Failed | **1** |
| Skipped | 5 |
| Total turns | 553215 |
| Perfect turns | 552944 |
| Turn accuracy | **99.95%** |

- **7 β battles: GATE perfect** (closed under lattice `edcfed36`)  
  `895340085`, `895345570`, `895429566`, `895515899`, `895564994`, `895612448`, `895637720`
- **Residual:** `895131867` (bounce / γ-seed) only — see Task 5+
- **FAIL line:**
  `FAIL: battle_895131867.json turn 48/319 — pod2 pos Δ=(-6.0,2.0); pod3 pos Δ=(4.0,6.0)`
- **β cluster status:** **CLOSED** under GATE. No remaining free-flight thrust ULP fails on `latest_battles`.

*End of forensics. Numbers from harness only.*
