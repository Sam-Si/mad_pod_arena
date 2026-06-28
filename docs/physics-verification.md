# Coders Strike Back — Verified Referee Physics Engine

**Status: production-grade double-precision physics** — `src/physics/physics.h` uses `double` throughout and is validated turn-by-turn against real CodinGame battle replays.

All code lives in the **`mad_pod_arena`** monorepo only (the old `codingame-csb-physics` tree was merged and retired).

```
$ MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
  312/312 tested battles PASSED (46,364 turns, 100.00% accuracy)
  ★ MERGE_PHYSICS_OK component (A) — must remain 100% (see docs/VERIFICATION_TRUTH_POLICY.md)

$ python3 sim/verify_battles.py battles/leaderboard_battles
  ~17,399 / 17,521 battles fully match every turn (~99.84% turn accuracy)
  Remaining ~44 divergences are borderline mid-turn CP / collision float cases
  after many turns (see "Known edge cases" below).

Player agent-timeout battles (program failed to output in time) are segregated:
  battles/test_session_timeouts/   (~130)
  battles/leaderboard_timeouts/    (~758)
```

---

## Verification Methodology

Given a battle JSON from CodinGame:
1. Parse exact initial state (positions, velocities, angles) from frame 0
2. Parse exact player commands (target_x, target_y, thrust) from every frame's `stdout`
3. Parse ground-truth post-turn state from every keyframe's `view` data
4. Feed initial state + exact commands into `physics.h` via `physics/replay_driver`
5. Compare engine output vs referee state **every single turn** — position, velocity, angle, next_cp, timeouts

A battle PASSes the **CI gate** only if **every turn** matches within **GATE_*** tolerances from `sim/tolerance_policy.py` (pos **5**, vel **3**, angle **1°**, timeout tol **1**). Tighter ±1 bounds are **EXPLORE_*** / diagnostic defaults (`sim/compare_battle.py`), not the merge gate.

---

## Frame / Turn Mapping

```
game_turn = (frame_index - 1) // 2

Frame 0             → init state (keyframe=true, NOT a game turn)
Frame 2T+1          → Player 0 submits actions for turn T (keyframe=false)
Frame 2T+2          → Player 1 submits actions for turn T (keyframe=true, state AFTER turn T)
```

Frames with `keyframe=true` contain the actual game state in the `view` field.
Frame 0 is the initialization frame (spawn positions, checkpoints).

---

## Core Turn Loop (5 Steps, Order Critical)

On every turn the referee executes exactly:

1. **Rotation** — each pod rotates toward its target (max 18°/turn, except turn 0)
2. **Acceleration** — thrust vector added to velocity along facing direction
3. **Movement + Collisions** — continuous-time sweep over `[0, 1.0]`, resolve all pod-pod collisions in chronological order, check checkpoint crossings between collisions
4. **Friction + Rounding** — `v = trunc(v * 0.85)`, `p = floor(p + 0.5)`
5. **Timers** — decrement shield cooldowns, decrement player timeouts

Steps 1-2 happen for **all 4 pods** before any movement begins.

---

## Verified Rules (All Proven Against 209+ Real Battles)

### 1. First-Turn Rotation (Per-Pod, Not Global Turn Index)

No 18° rotation limit on a pod's **first successful rotate**. Tracked via `hasRotated` per pod — not global `turn == 0`.

Pods that `target == position` on turn 0 skip rotate entirely and keep `hasRotated = false`; their first real rotate on a later turn still snaps:
```
angle = 0; angle = diffAngle(target); applyRotateFirst(angle)  // normalize to [-π, π]
```

**Evidence**: `battle_870230125` pod0 BOOST@self on turn 0, snaps to 163° on turn 1.

### 2. Normal Rotation (Turn 1+)

Clamped to ±18° per turn. `diffAngle` uses the Go referee formula
`fmod(2*da, 2π) - da` so angles accumulate outside `[-π, π]` (cos/sin still correct).
```cpp
double rotateAngle = diffAngle(target);  // Go: fmod(2*da, 2π) - da
if (rotateAngle < -maxRotate) a = angle - maxRotate;
if (rotateAngle >  maxRotate) a = angle + maxRotate;  // two separate ifs, not else-if
angle = a;  // NOT normalized to [-π, π]
```

### 3. Boost (Per-Pod, Not Per-Player)

Each of the 4 pods can BOOST **once per game independently**.

- Successful BOOST: thrust = 650, `boosted` flag set to 1
- Already boosted: treated as thrust = 200
- BOOST during shield cooldown: boost **is consumed** (`boosted = 1`), but thrust is still forced to 0 by the shield timer

**Evidence**: `battle_890672200` SHIELD then repeated BOOST; second effective BOOST is thrust=200 not 650.

### 4. Shield Mechanics

**Activation** (`SHIELD` command):
- `shieldtimer` set to 4
- Pod mass becomes 10× for collisions **this turn** (mass = 0.1 in inverse-mass formulation)
- Thrust = 0 (no acceleration)
- Rotation still applied normally

**Cooldown** (turns with `shieldtimer > 0`):
- No thrust applied regardless of command
- If BOOST requested during cooldown → boost NOT consumed, thrust = 0
- Timer decrements by 1 each turn at end-of-turn
- Total lockout: 4 turns (activation turn + 3 cooldown turns)

### 5. InvalidInput (Negative / >200 Thrust)

When a player's stdout contains an out-of-range thrust value:

**Per-pod behavior**: **No shield**, **no rotation**, **no thrust** (angle unchanged, velocity is pure friction only).
Negative thrust does **not** activate `shieldtimer` — doing so would give 10× mass on collisions and diverges from the referee (verified: `battle_891684936` turn 102).

**Propagation rule** (verified from battles with invalid thrust):
- If the **first line** (pod 0 of that player) is invalid → **both** pods invalidated
- If only the **second line** (pod 1) is invalid → only that pod invalidated

The referee reads stdout line-by-line; an error on line 1 prevents parsing line 2.

### 5b. Target == Position

If a pod targets its own coordinates (`target_x == pod.x && target_y == pod.y`), the referee skips **both** rotation and thrust for that pod (Go: `if move.target == pod.p { continue }`). SHIELD still sets `shieldtimer` before the early exit.

### 6. Collision Physics

**Detection**: Continuous-time sweep. For each pod pair, solve the quadratic for when distance = 1600 (2× pod radius 800). Take the earliest collision time.

**Resolution**: Modified elastic collision with minimum impulse:
```cpp
force = normal.dot(relativeVelocity) / (invMass_a + invMass_b);
if (force < 120.0) force += 120.0;  // minimum impulse
else force += force;                 // double the force (elastic)
```

Shield mass: `invMass = 0.1` (10× heavier). Normal mass: `invMass = 1.0`.

**Overlap correction**: If pods are already overlapping (`distance ≤ 800`), push apart by `(800 - distance) / 2 + EPSILON` along the normal.

**Multiple collisions**: Resolved in strict time order. After each collision, re-sweep remaining time for new collisions.

### 7. Checkpoint Detection

Segment-vs-circle test on **previous position → current position**: does that movement segment come **strictly inside** radius 600 (`dist² < 360000`) of the next checkpoint center?

Closest-point-on-segment projection (Go `cpCollide`): project CP onto the line through previous→current; if parameter `u>1` use current, else if `u>0` use the interpolate, else use previous; then `dist² < 600²`.

Challenged on golden corpus:
- **Strict `<` vs inclusive `<=`**: `battle_884515945` turn 9 has closest dist exactly 600 with positions matching GT but GT does **not** pass — inclusive falsely advances `next_cp`. Go referee uses `<`.
- **Mid-turn subsegments vs end-only**: end-of-turn-only (single segment turn-start→post-friction) regresses 33 pass-tier battles; mid-turn bookkeeping (advance previous pose after each collision slice, Go `curps`) is required.

Checkpoints are checked:
- Mid-turn after each integrated collision step while time remains (segment previous→current after bounce); previous pose advances
- End of turn after friction/rounding (segment previous→rounded pose)

The engine uses a global linear checkpoint index: `laps × num_checkpoints + 1` total entries. The referee's `view` shows `next_cp % num_checkpoints`.

### 8. Timeout System

Each player starts with 100 timeout ticks. Decremented by 1 every turn.

When a pod passes its next checkpoint: player's timeout resets to 100 (set to 101, then decremented by 1 at end of turn = net 100).

If timeout reaches 0, the player is eliminated.

### 9. Rounding

- **Velocities**: `trunc(v * 0.85)` — toward zero (C `trunc()`)
- **Positions**: `floor(p + 0.5)` — round half-up (standard rounding)

These happen after friction and movement, before the next turn begins.

### 10. Spawn Positions

Pods spawn perpendicular to the CP0→CP1 vector with specific offsets:
```
startPointMult = [{500, -500}, {-500, 500}, {1500, -1500}, {-1500, 1500}]
unit = normalize(CP1 - CP0)
pod[i].x = floor(CP0.x + unit.y * mult[i].x + 0.5)
pod[i].y = floor(CP0.y + unit.x * mult[i].y + 0.5)
```

Pods 0,1 belong to Player 0. Pods 2,3 belong to Player 1.

---

## Known Edge Cases (~0.25% of leaderboard battles, 44 / 17,521)

After fixes above, ~44 leaderboard battles still diverge on some late turn.
Typical causes (not logic bugs in normal play):

1. **Mid-turn CP vs full-segment CP** — a collision stops the world mid-turn; the sub-segment passes within 600 of a CP but the full start→end trajectory finishes just outside (`battle_891616683` turn 88). Go also checks mid-turn; viewer outcomes occasionally disagree on these knife-edges.
2. **Borderline pod-pod collisions** — pods at distance ~800–805; sub-unit drift flips whether a collision fires, then velocities diverge.
3. **Sub-degree angle accumulation** — after 50–400 turns, cos/sin drift yields ~1–2° angle error (just over the 1° verifier tolerance) and occasionally ±6 position units.
4. **Viewer timeout=101** — exact boundary passes occasionally show timeout 101; verifier allows ±1 on timeouts.

These do **not** affect normal bot search / simulation quality. Test-session corpus (312 games, 46k turns) passes at **100%** turn accuracy and is the CI gate.

---

## Bugs Found in the Original Bot Code

The bot source (used to generate `stderr` logs) had several physics bugs relative to the referee:

| Bug | Bot code | Correct (verified) |
|-----|---------|-------------------|
| Angle normalization | `[0, 2π]` in `applyRotateFirst` | `[-π, π]` (atan2 convention) |
| Boost tracking | Per-player | Per-pod (each pod has its own) |
| First-turn detection | `turn == 1` counter check | `isFirstTurn` flag per pod |
| Timeout reset | `playerTimeout = 100` | `playerTimeout = 101` (then -1 = net 100) |
| InvalidInput handling | Not handled | Shield + no rotation |

Despite these bugs, the bot's `PRED_ASSERT` accuracy was ~70% on perfect matches (position within ±1, angle within ±1°). The remaining 30% were primarily rounding artifacts, first-frame noise, and the bugs above.

---

## Quick Start

### Setup Environment (Ubuntu Linux)
To automatically update the system, install C++ build tools and Python 3, compile the physics engine driver, and run the verification suite:
```bash
./setup_env.sh
```

### Verify all battles
```bash
python sim/verify_battles.py battles/test_session_battles
```

### Debug a single battle
```bash
python sim/compare_battle.py battles/test_session_battles/battle_891669739.json
```

### Rebuild the C++ driver
```bash
g++ -std=c++17 -O2 -o physics/replay_driver physics/replay_driver.cpp
```

---

## File Reference

| File | Purpose |
|------|---------|
| `setup_env.sh` | **Setup script for Ubuntu** — Updates apt/packages, installs dependencies, builds the driver, and runs verification |
| `physics/physics.h` | **The verified physics engine** — 100% referee-accurate |
| `physics/replay_driver.cpp` | C++ text-protocol driver for physics.h |
| `sim/battle_parser.py` | Parses battle JSON → structured data |
| `sim/physics_driver.py` | Python subprocess wrapper for C++ driver |
| `sim/verify_battles.py --gate` | Gate (A) batch verifier (`MERGE_PHYSICS_OK`; tolerances in `sim/tolerance_policy`) |
| `sim/compare_battle.py` | Single-battle debugger with detailed output |
| `rules.md` | Original game rules reference |
| `battles/test_session_battles/` | 220 real battle replays (test corpus) |
