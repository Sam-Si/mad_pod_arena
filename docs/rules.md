# Mad Pod Arena — Game & Physics Rules

> **Source of truth:** first-party Fidelity physics under `src/`  
> (`src/core/constants.h`, `src/physics/fidelity_math.h`, `src/physics/fidelity_world_step.h`, `src/physics/physics.h`, `src/engine/arena.cpp`).  
> CodinGame’s public statement is **secondary**. Where it disagrees with this repo, **code wins** and the conflict is called out.  
> Behavioral proof is not prose: `./tools/run_truth_suite.sh` and merge gate A/B.

This document merges:

1. The **public CodinGame rules** (Mad Pod Racing / Coders Strike Back), and  
2. The **hidden / expert rules** recovered from the community Go referee, battle keyframes, and this codebase.

---

## At a glance

| | |
|---|---|
| **Sport** | Real-time pod race with elastic physics combat |
| **Teams** | 2 teams × **2 pods** each (4 pods total) |
| **Map** | 2–8 checkpoints in a loop; default **3 laps** |
| **Playfield** | 16000 × 9000 (pods may leave the rectangle) |
| **Win** | First team to finish the multi-lap route with **one** pod |
| **Lose** | Other team wins · team CP-timeout · invalid output · agent process timeout · someone else wins |
| **Turn budget (CG)** | First turn ≤ **1000 ms**, later ≤ **75 ms** |
| **Max game length** | **500** completed turns (`kMaxGameTurns`) → progress ranking / draw |

**Public “new rules” note (website):** max thrust is **200** (legacy was 100). Boost is still **650**.

---

## 1. The goal (public + precise)

You race a circuit of **checkpoints**. Complete the required **laps** by visiting every checkpoint in order, then returning through the start. **The first team that has any pod finish the full multi-lap route wins.**

Only **one** of your two pods needs to finish. The second pod may race, block, ram, or shield—whatever wins the match.

```text
  CP0 ──► CP1 ──► … ──► CPn-1 ──► (back to CP0)  ×  laps
                finish line = last global checkpoint (extra CP0)
```

---

## 2. Who is racing

| Role | Count | Notes |
|---|---|---|
| **Pods** | 4 | Indices **0–1 = team 0**, **2–3 = team 1** |
| **Teams** | 2 | Share a **team checkpoint-timeout** counter (not per pod) |
| **Checkpoints / lap** | **2–8** | Circles of radius **600** |
| **Laps** | default **3** | Builds a *global* CP list (see §4) |

Each pod state (Fidelity):

| Field | Meaning |
|---|---|
| Position `(x, y)` | Double during the turn; **round-half-up** at end of turn |
| Velocity `(vx, vy)` | Double during the turn; **trunc(v × 0.85)** at end of turn |
| Facing `angle` | **Radians** in Fidelity; **never** integer-committed. CG bot I/O shows **degrees** (0° = East, 90° = South) |
| `next` | Index into the **global** multi-lap list (not only local 0…n−1) |
| `shieldtimer` | `0` = normal; **`4` on activation frame**, then counts down each end-turn |
| `boosted` | **Per pod** (`0` unused / `1` used) — see folklore table §15 |
| `won` | Finished the global route |
| `hasRotated` | `false` until first non-skipped rotate (enables first-turn snap) |

---

## 3. The arena

| Quantity | Value | Notes |
|---|---|---|
| Playfield (CG marketing) | **16000 × 9000** | Origin top-left in CG docs |
| Pod collision radius | **400** | Pair collides when centers ≤ **800** apart |
| Checkpoint radius | **600** | Pass when movement path enters disk (**strict** `< 600`) |
| Out of bounds | **Allowed** | Pods may leave the rectangle with full physics |
| Tournament maps (this repo) | **18** fixed layouts | `src/core/maps/catalog.h` — live CG can use other layouts |

---

## 4. Checkpoints & progress (hidden structure)

### 4.1 Single lap vs global list

A track is one lap of checkpoints `[CP0, CP1, …, CPk-1]`.

For `laps` laps the referee builds a **global** list:

```text
global = (track × laps)  +  [CP0 again as final finish]
```

Example: 4 CPs, 3 laps → global length `4×3 + 1 = 13`.

| Moment | `next` (global) | Local viewer id |
|---|---|---|
| Race start | **1** (first target after start) | usually `1` |
| After each pass | `next + 1` | `next % track_size` |
| Finish | `next >= global.length` → clamp, set `won` | often `0` / end sentinel |

When `next` reaches the end of the global list, the pod **wins for its team** (`won = true`).  
Only one pod needs to finish.

Viewer / bot I/O show **local** index `next % track_size` as `nextCheckPointId`.  
Fidelity simulates on the **global** index.

Helpers: `src/core/progress.h` (`LocalNext`, `Decode`, `GlobalNext`).

### 4.2 How a checkpoint counts (hidden)

A pass is **not** “center currently inside the circle at end of turn only.”

It is a **segment test** (closest point on the free-flight path to the CP):

1. Segment from **start of free-flight** (start of turn, or last bounce position) to **current** position after motion.  
2. Closest point on that segment to the CP center.  
3. Pass iff **strict** `dist² < 600²`.  
   - Touching exactly **600** does **not** count on the segment test.  
4. **Rare edge case (Fidelity):** if after end-of-turn commit the pod sits with `dist² == 600²` exactly, Fidelity still advances the checkpoint (exact-boundary epilogue).

After a bounce mid-turn, the free-flight origin for later segment tests restarts from the **post-bounce** position.

### 4.3 Team timeout reset on pass (hidden)

When **any** pod of a team passes a checkpoint:

| Implementation | Timeout write |
|---|---|
| Community Go referee | sets team timeout to **100**, then end-of-turn **−1** |
| This Fidelity engine | sets team timeout to **101**, then end-of-turn **−1** |

Both yield **100 on the next displayed frame**, matching CG.  
Public wording: “if none of your pods make it to their next checkpoint in under **100** turns, you are eliminated.”

Timeout is **per team**, not per pod. One pod hitting CPs keeps the whole team alive.

---

## 5. One game turn (the heart of the rules)

All four pods act **simultaneously** each turn:

```text
  1. READ state  →  both teams choose actions for both pods
  2. APPLY MOVE to each pod (rotate + accelerate)   ← positions not yet integrated
  3. WORLD STEP  →  collisions, motion, CP passes, friction, commit
  4. TERMINALS   →  win / timeout / max turns
```

Official expert order (website) matches this:

```text
  Rotation → Acceleration → Movement (+ collisions) → Friction → integer commit
```

### 5.1 Player action (public protocol)

Each pod commands:

```text
  target point (tx, ty)   +   thrust mode
```

| Thrust mode | Effect |
|---|---|
| Integer **0–200** | Accelerate by that amount along facing after rotate |
| **`BOOST`** | If this pod has not boosted yet: thrust **650**, mark boost used. If already used: treated as **200** |
| **`SHIELD`** | Set shield timer to **4**, force thrust **0** this turn |

Optional trailing text after the three tokens is ignored by parsers that accept comments.

### 5.2 Invalid / incomplete output (hidden — platform + engine)

Fidelity `parseMove` / `applyAction`:

| Input | Engine effect |
|---|---|
| Incomplete line (no thrust token) | `valid=false`, thrust **0**, no shield |
| Non-numeric non-keyword thrust | `invalid_input`, skip rotate+thrust |
| Numeric thrust **&lt; 0 or &gt; 200** | `invalid_input` — **skip rotate+thrust**; **does not** activate shield |
| `SHIELD` / `BOOST` keywords | Always valid as protocol tokens |

**Platform (CodinGame match result):** “invalid action” for a player is a **lose condition** (tooltip `$i: invalid action`) even if keyframe physics could still step.  
**Agent process timeout** (`$i: timeout!`) is a **different** lose condition from the 100-turn checkpoint timer.

See outcome hierarchy in §8.

### 5.3 Apply move — full order (code SSOT)

Order in `applyFidelityMove` (`fidelity_math.h`):

1. **`invalid_input`** → return immediately (no shield, no rotate, no thrust).  
2. **`SHIELD`** → `shieldtimer = 4`, `t = 0`.  
3. **`BOOST`** → if `boosted == 0`: `boosted = 1`, `t = 650`; else `t = 200`.  
4. If **`shieldtimer > 0`** (including activation frame) → **`t = 0`** (cannot accelerate while shield countdown is active).  
5. If **target equals current position** (`tx == x && ty == y`) → **skip rotate and thrust** (shield already applied if requested).  
6. **Rotate**
   - First successful rotate (`hasRotated == false`): face geometric target via `atan2` immediately (**no 18° clamp**). Mark `hasRotated = true`.  
   - Later: rotate toward target by at most **18°** per turn (see §5.4).  
7. **Thrust** along facing: add `t · (cos θ, sin θ)` to velocity (Fidelity applies ULP snap lattice — §14).

### 5.4 Rotation details (hidden)

Public rule: “max **18°** per turn, except the first round.”

Code-precise:

| Case | Behavior |
|---|---|
| First rotate ever for that pod | Snap to `atan2(ty−y, tx−x)` |
| Later turns, \|Δangle\| **&lt; 18°** | Facing becomes **exact target angle** (not `angle += da`) |
| Later turns, \|Δangle\| **≥ 18°** | Add/subtract **exactly 18°** (exact 18° **does** max-rotate — not “free snap”) |
| Angle difference | Go-style: `Mod(2·Mod(a−θ, 2π), 2π) − Mod(a−θ, 2π)` |
| Stored angle | Double radians; peel whole ±2π turns; **not** snapped to integer degrees |

CG bot **input** often shows angle as **integer degrees** (`round(angle·180/π)` in community referees). That is a **display/I/O** rounding, not the internal physics commit.

### 5.5 World step (after all four moves applied)

Implemented once in `simulateFidelityWorld`:

| Phase | What happens |
|---|---|
| **A. Collision loop** | While remaining time `t ∈ (0,1]`: find earliest pod–pod TOI; advance all pods; bounce pair; optional mid-turn CP test for bounced pods; restart free-flight origin. Safety cap ~200 iterations. |
| **B. End-of-turn commit** | For each pod: friction + round position + decrement shield timer if &gt; 0 |
| **C. Checkpoint epilogue** | Segment CP test for all pods; rare exact-radius edge |
| **D. Clocks** | Both team timeouts **−1**; turn counter **+1** |

**Collision pair scan order (hidden, matches Go):**  
`i = 3..1`, `j = i−1..0`. Earliest TOI wins; ties keep the last pair written under `tx <= first`.

**TOI semantics (`newCollideTime`):**

| Return | Meaning |
|---|---|
| `0` | Already overlapping (`dist² ≤ 800²`) |
| `10` | No collision this remaining segment (sentinel &gt; 1) |
| `(0,1]` | Fractional time of impact within remaining turn time |

Movement during a sub-step: `position += velocity * dt` (continuous; no integer commit mid-turn).

### 5.6 Collisions / bounce (expert + code)

Public: elastic collisions; **minimum impulse 120**; shield multiplies mass.

Code-precise bounce (`worldBounce`):

```text
  n  = unit normal from pod A → pod B
  rv = relative velocity A − B
  m1, m2 = inverse-mass factors (1.0 normal; 0.1 if shieldtimer == 4)
  force = (n · rv) / (m1 + m2)
  if force < 120:  force += 120          # minimum impulse
  else:            force += force        # double the projected force
  impulse = −force · n
  apply impulse · m_i to each pod’s velocity
  if centers still ≤ 800 apart: separate by (overlap/2 + ε) along n
```

| Topic | Detail |
|---|---|
| Pod disk radius | **400** (centers collide at **800**) |
| Min impulse | **120** |
| Shield “mass × 10” | Fidelity uses **inverse-mass 0.1** when `shieldtimer == 4` only |
| Shield on other timer values (3,2,1) | **Normal mass** for bounce; still **no thrust** while timer &gt; 0 |
| Separation epsilon | `1e-5` (`kEpsilon`) |

Shield timer timeline:

```text
  SHIELD command → timer = 4   (activation: heavy mass + no thrust)
       endTurn   → 3           (normal mass, still no thrust)
       endTurn   → 2
       endTurn   → 1
       endTurn   → 0           (normal again)
```

Public text: “will not be able to accelerate for the next 3 turns” — combined with the activation frame, that is **4 turns of `t = 0`** while `shieldtimer > 0`.

---

## 6. Friction & commit (end of every turn)

After movement and bounces:

```text
  velocity  ←  trunc(velocity × 0.85)     # Fidelity: + selective ULP fix (frictionTrunc)
  position  ←  floor(position + 0.5)      # round half up
  if shieldtimer > 0: shieldtimer--
```

| Rule | Detail |
|---|---|
| Friction factor | **0.85** |
| Velocity commit | **trunc toward zero** after ×0.85 |
| Position commit | **round half up** (`floor(x + 0.5)`) |
| Angle commit | **None** — angle stays double radians |
| When integers appear | Only after this commit are pos/vel CG-keyframe integers |

---

## 7. Timeouts & survival (two different clocks)

### 7.1 Checkpoint / “did not reach next CP” (game rule)

Each **team** has a timeout counter:

| Event | Effect |
|---|---|
| Start of race | Both teams at **100** |
| Team’s pod passes a CP | Timeout refreshed so next frame reads **100** (see §4.3) |
| Every world step end | **Both** timeouts **−1** |

If a team’s timeout reaches **≤ 0**, that team is **eliminated**. Opponent wins.  
If **both** expire → dual elimination (platform still publishes ranks; see §8).

### 7.2 Agent / process timeout (platform rule)

If a bot fails its wall-clock budget (first turn **1000 ms**, later **75 ms**) or crashes:

- Match tooltips show `$i: timeout!`  
- Opponent wins **regardless** of checkpoint progress  
- Replays may have **truncated action streams** — invalid for full physics verification (see `battles/RETENTION.md`)

These are **not** the same as the 100-turn CP timer.

---

## 8. How the race ends (physics + platform)

### 8.1 Pure referee physics (`Game::checkWinner` / arena)

Checked after each Fidelity step:

| Outcome | Condition |
|---|---|
| **Team wins** | Any of its pods has `won == true`, or opponent eliminated by CP-timeout |
| **Draw (physics)** | Both finished same step; both eliminated; inconclusive dual terminal |
| **Max turns** | After **500** completed turns — arena may draw; Go-style progress ranking often used offline |

**Go progress ranking** (when no one finished before max turns / dual edge cases):

```text
score(pod) = next × 1_000_000  −  distance(pod, globalCp[next])
winner team = team of pod with highest score
```

Winning is “**first to finish**,” not aggregate score. Blocking and ramming are legal and often decisive.

### 8.2 CodinGame platform match outcome (hidden from marketing page)

Battle JSON `ranks` / `scores` / **tooltips** encode the **match result**, which is **not always** pure progress:

| Priority | Signal | Winner rule |
|---|---|---|
| 1 | Tooltip `$i: invalid action` | Winner = `1 − i` |
| 2 | Tooltip sole `$i: timeout!` (agent) | Winner = `1 − i` |
| 3 | Tooltip sole “did not reach the next checkpoint” | Winner = `1 − i` |
| 4 | Dual elimination / dual agent timeout | Use platform **`ranks`** (often 0–0 scores with forced ranking) |
| 5 | One pod finished (`won`) | That pod’s team |
| 6 | Sole CP-timeout | Other team |
| 7 | Else | Progress score (§8.1) or ranks |

**Implication:** turn-perfect Fidelity can still “disagree” with `ranks` if the harness only compares progress and ignores tooltips. Research: `docs/archive/PHYSICS_FIDELITY_RESEARCH.md`. Harness: `sim/verify_quick_accuracy.py` (`parse_tooltip_winner` / `compute_phys_winner`).

### 8.3 Victory / lose summary (public + complete)

**Victory**

- Be the first to complete all laps with one pod.

**Lose**

- Opponent finishes first.  
- Your team’s CP-timeout hits 0.  
- Your program provides **incorrect output** (invalid action).  
- Your program **times out** / crashes (agent timeout).  
- Dual elimination with platform ranking against you.  
- Max-turn progress ranking against you (offline / some referees).

---

## 9. Spawn (hidden geometry)

Standard CG-style spawn relative to the unit vector from **CP0 → CP1**.

Let `u = normalize(CP1 − CP0)`. Spawn offsets use the **perpendicular** frame `(u.y, u.x)` with multipliers:

| Pod | Multipliers `(mx, my)` | Position |
|---|---|---|
| 0 (team 0) | `(+500, −500)` | `round(CP0 + (u.y·mx, u.x·my))` |
| 1 (team 0) | `(−500, +500)` | same formula |
| 2 (team 1) | `(+1500, −1500)` | same formula |
| 3 (team 1) | `(−1500, +1500)` | same formula |

Initial state:

| Field | Value |
|---|---|
| Velocity | `(0, 0)` |
| Angle | small negative sentinel (~**−1°** in radians) |
| `hasRotated` | `false` |
| `next` | `1` |
| Timeouts | `100` each team |
| Boost / shield | unused / 0 |

---

## 10. Bot I/O (CodinGame-facing)

### Initialization

```text
laps
checkpointCount
checkpointCount lines: checkpointX checkpointY
```

### Each turn (input)

Four lines — **your two pods**, then **opponent two** (team-relative order; player 1’s view swaps teams):

```text
x y vx vy angle nextCheckPointId
```

| Field | Notes |
|---|---|
| `x y` | Integers after commit |
| `vx vy` | Integers after friction trunc |
| `angle` | **Degrees** for bots (Fidelity stores radians) |
| `nextCheckPointId` | **Local** track index `0 … checkpointCount−1` |

**Not fully revealed in public I/O (important for bots):**

| Hidden / partial | Note |
|---|---|
| Opponent remaining boost | Not a field — must track from history of opponent actions if observed in replays; live input has no `boosted` flag |
| Exact shield timer | Not printed; you only see kinematics after shield turns |
| Global multi-lap `next` | Only local modulo id is printed |
| Internal double angle | Only rounded degree view |

### Each turn (output)

Two lines (your two pods):

```text
tx ty thrust
```

where `thrust` is `0`…`200`, or `BOOST`, or `SHIELD`.

### Constraints (public)

| Constraint | Value |
|---|---|
| Thrust range | `0 ≤ thrust ≤ 200` (keywords separate) |
| Checkpoints | `2 ≤ checkpointCount ≤ 8` |
| First response | ≤ **1000 ms** |
| Later responses | ≤ **75 ms** |

This repository’s bots use configurable budgets (`src/cg/bot_config.h`).

---

## 11. Constant appendix (code SSOT)

From `src/core/constants.h` unless noted.

| Constant | Value | Code name |
|---|---|---|
| Pod radius | 400 | `kPodRadius` |
| Collision diameter | 800 | centers; `kPodCollisionRsq = 640000` |
| Checkpoint radius | 600 | `kCpRadius` / `kCpRsq = 360000` |
| Friction | 0.85 | `kFriction` |
| Max rotate / turn | 18° | `kMaxRotateDeg` / `kMaxRotateRad` |
| Max thrust | 200 | `kMaxThrust` |
| Boost thrust | 650 | `kBoostThrust` |
| Min collision impulse | 120 | `kMinImpulse` |
| Timeout (team) | 100 | `kTimeoutLimit` |
| Max game turns | 500 | `kMaxGameTurns` |
| Default laps | 3 | `kDefaultLaps` |
| Pods | 4 | `kPodCount` |
| Shield timer on activate | 4 | `kShieldTimerActivate` |
| Shield inverse-mass (activation frame) | 0.1 | `kShieldMassFactorFidelity` |
| Fast-search shield mass display | 10.0 | `kShieldMassFast` (GA Fast fragment only) |
| Separation epsilon | 1e-5 | `kEpsilon` |
| Init angle sentinel | ≈ −0.0174533 rad (−1°) | `kInitAngleSentinel` |

---

## 12. Replay / verification framing (repo-specific, essential)

CodinGame battle JSON is **not** one physics step per array element:

```text
  1 game turn  ==  2 frames in the replay JSON
    Frame 0              init keyframe (not a game turn)
    Frame 2T+1           player 0 stdout for game turn T  (often non-keyframe)
    Frame 2T+2           player 1 stdout + keyframe AFTER game turn T
```

Physics steps **once per game turn**. Fidelity is compared to the **post-turn keyframe** only.

Truncated action streams (`n_frames` too large vs recovered turns) are **invalid** for physics verification — typically agent crash/timeout mid-game (`battles/RETENTION.md`).

---

## 13. Implementation notes (this repository)

### What “Fidelity” means here

- **`simulateFidelityWorld`** is the **only** collision/CP/commit world loop for the referee path.  
- `csb::Game` (string/driver API) and `csb::fast_physics::Game` (exact rollout façade) both call it after applying moves.  
- Gate A (`test_session_battles`) and golden pass tier lock this behavior under `GATE_*` tolerances.

### Products that are *not* full race rules

| Product | Role |
|---|---|
| `csb::fast::SimulateTurn` | **GA search fragment** only (degrees pods, collisions + end-turn). **No** CP / timeout / won. Not a second full rule set. |
| GA bot heuristics | Strategy (`BotConfig`, roles) — **not** game rules |
| Python gate tolerances | Measurement policy (`sim/tolerance_policy.py`), not physics law |

---

## 14. Deep fidelity / numerics (implementers)

High-level rules above describe the **intended game**. To match CG keyframes across platforms, Fidelity also includes a **thrust / friction ULP lattice** (`applyFidelityThrust`, `frictionTrunc`, trig pole snaps). These predicates are:

- Provenanced by battle ids in code comments  
- **Part of the Fidelity product**, not optional cosmetics  
- Easy to regress — do not “simplify” without Gate A/B green

Rough classes of residual divergence historically:

| Class | Symptom | Typical cause |
|---|---|---|
| α | Angle drift ~1° after many turns | Non-committed double angle + sin/cos |
| β | Pos/vel off by few units | Integer commit cascade |
| γ | Catastrophic multi-pod error | Collision TOI / impulse branch flip |

Golden / divergence corpora track the long tail. Gate corpora are expected **100% turn-perfect** under `GATE_*`.

---

## 15. Folklore vs code (explicit)

| Topic | CG paste / website | **This engine / recovered truth** |
|---|---|---|
| Boost sharing | “Number of boost available is common between pods” | **Per-pod** `boosted` flag (`Pod::boosted`) — each pod gets its own BOOST once |
| Max thrust | 200 (was 100) | **200** normal, **650** boost |
| Max turns | Often omitted | **500** (`kMaxGameTurns`); old arena notes said 1000 — obsolete here |
| Shield mass | “Mass × 10” | Fidelity bounce: **inverse-mass 0.1** only when `shieldtimer == 4` |
| Shield no-accel | “Next 3 turns” | Timer starts at **4**; no thrust while `shieldtimer > 0` (activation + 3 drains) |
| First rotate | “Except first round” | Per-pod first **successful** rotate (skipped if dest==pos or invalid) |
| CP pass | “Center inside radius” | **Path segment** strictly inside; + exact-radius epilogue |
| Timeout | “100 turns” | Team counter; pass refresh; end-turn −1; dual clocks with **agent** timeout |
| Winner | First to finish | + invalid action / agent timeout / dual-elim ranks (platform) |
| Angle | Absolute degrees in I/O | Physics in **radians double**; I/O degrees are a view |

---

## 16. Turn flowchart

```text
                 ┌─────────────────────┐
                 │  Observe 4 pods     │
                 │  Choose 4 actions   │
                 └──────────┬──────────┘
                            ▼
                 ┌─────────────────────┐
                 │  Per pod:           │
                 │  invalid? → skip    │
                 │  SHIELD / BOOST /   │
                 │  dest==pos? → skip  │
                 │  rotate (1st/≤18°)  │
                 │  thrust along face  │
                 └──────────┬──────────┘
                            ▼
                 ┌─────────────────────┐
                 │  Collision timeline │
                 │  t ∈ [0,1]          │
                 │  bounce + CP mid    │
                 └──────────┬──────────┘
                            ▼
                 ┌─────────────────────┐
                 │  Friction ×0.85     │
                 │  Round positions    │
                 │  Shield countdown   │
                 │  CP epilogue        │
                 │  Timeouts −1        │
                 └──────────┬──────────┘
                            ▼
                 ┌─────────────────────┐
                 │  Win / CP-timeout / │
                 │  max turns /        │
                 │  platform tooltips  │
                 └─────────────────────┘
```

---

## 17. Public website text (for reference)

Condensed from CodinGame’s statement (not authoritative when it conflicts with §1–16):

- Max thrust **200** (was 100).  
- Two pods per player; first pod to complete the race wins for the team.  
- Checkpoints circular radius **600**; map **16000×9000**, origin top-left.  
- Thrust 0–200; rotate ≤ **18°**/turn; **BOOST** once; **SHIELD** heavy collision, no accel for next 3 turns.  
- Pod force-field radius **400**.  
- Pods may leave the playfield.  
- Eliminated if no pod hits next CP in **100** turns.  
- Expert: rotate → accelerate → move/collide → friction **0.85** → trunc speeds / round positions; elastic min impulse **120**; boost = **650**; shield mass ×10; angles absolute (0° East, 90° South).  
- I/O: init laps + checkpoints; per turn 4 pod lines; two output lines; response **1000 ms** first / **75 ms** later.

Use this section only as a checklist against the detailed sections above.

---

## 18. Further reading

| Doc / path | Role |
|---|---|
| [`SSOT.md`](SSOT.md) | Who owns which physics file |
| [`VERIFICATION_TRUTH_POLICY.md`](VERIFICATION_TRUTH_POLICY.md) | How we prove Fidelity against CG battles |
| [`archive/PHYSICS_FIDELITY_RESEARCH.md`](archive/PHYSICS_FIDELITY_RESEARCH.md) | Tooltips vs ranks, residue classes |
| `src/core/constants.h` | Numeric law |
| `src/physics/fidelity_world_step.h` | World step |
| `src/physics/fidelity_math.h` | Rotate, thrust, friction, CP segment |
| `third_party/referees/coders-strike-back-referee/csbref.go` | Community Go referee (reference, not SSOT) |
| `./tools/run_truth_suite.sh` | Behavioral proof (not this prose) |

---

*Mad Pod Arena rules — code-faithful edition. Strategy bots may invent tactics; they may not invent physics. When the website is silent, the keyframes and Fidelity code speak.*
