# Mad Pod Racing - Genetic Algorithm Bot
## Context & State Dump for AI Collaborators

### Problem Statement
This project implements a competitive AI bot for the [CodinGame Mad Pod Racing](https://www.codingame.com/multiplayer/bot-programming/mad-pod-racing) challenge. The goal is to control two pods racing around a circuit of checkpoints faster than the opponent's two pods. Each team designates a **Runner** (focuses on finishing the race) and a **Blocker** (focuses on disrupting the opponent's runner).

---

### Core Architecture

- **Language**: C++17, compiled with `-O3` and loop unrolling on CodinGame servers.
- **Build System**: Bazel (`bazel build //...`)
- **Core Engine** (`src/engine/`): A physics engine replicating the CodinGame server exactly:
  - Pods have radius 400 (collision at distance 800)
  - Checkpoints have radius 600
  - Per-turn: apply thrust, move linearly, resolve collisions, apply friction (`trunc(vel * 0.85)`), round positions
  - Collision: 2D elastic collision with **minimum impulse of 120** (the "secret ingredient")
  - Shield: Sets mass to 10 (normally 1), lasts 3 turns of inactivity
  - Boost: One-time 650 thrust instead of normal 200 max
  - Rotation: Max 18° per turn, instant-snap on Turn 1
- **Bot Engine** (`src/cg/cg_bot.cpp`): A Genetic Algorithm that evaluates sequences of moves:
  - Three genes per action: `gene1` (meta/shield), `gene2` (steering), `gene3` (thrust)
  - `gene1 > 0.95` → Shield; `gene1 < 0.3` → Direct-to-CP heuristic; else → manual control
  - Population of 40, horizon of 6 turns
  - Two-phase GA: 15ms for opponent model, then 70ms for our plan using opponent's predicted moves
- **Tournament Runner** (`src/tournament/`): Swiss-system tournament for hyper-parameter search

---

### Physics Engine Verification Status

✅ **Verified correct** against CodinGame server state dumps:
- Thrust application, velocity friction (`trunc`), position rounding
- Collision detection (quadratic equation for time-of-impact)
- Collision resolution (elastic + minimum impulse 120)
- Shield mass (10.0), shield cooldown (3 turns)
- Rotation clamping (±18°/turn)
- Checkpoint radius (600 units)

⚠️ **Known issue**: Overlapping pods at turn start may not be resolved (GetCollisionTime returns -1 when pods already overlap). Should return t=0 to force immediate resolution.

---

### Current Hyper-Parameters (Bot_449)

```cpp
config.horizon = 6;        // Turns of lookahead
config.population = 40;     // GA population size
config.dist_weight = 1.0;   // Distance-to-CP penalty
config.align_weight = 1.0;  // Velocity alignment reward
config.block_weight = 5.0;  // Blocker aggressiveness
config.shield_penalty = 50.0; // Shield usage penalty
```

---

### Key Features Implemented

1. **Dynamic Role Swapping**: Runner/Blocker roles swap in real-time based on race progress score (`laps * 50000 + cp_index * 1000 - distance`), with 1500-point hysteresis to prevent flickering.
2. **Turn 1 Forced Acceleration**: Always output thrust 200 on the starting line (angle == -1) regardless of GA output.
3. **Checkpoint Corner-Cutting**: Target is shifted 400 units toward the *next* checkpoint, allowing the GA to naturally cut inside corners.
4. **Orbital Drift Penalty**: Perpendicular velocity component is penalized to prevent pods from orbiting checkpoints.
5. **Boost Heuristic**: Runner boosts when distance > 5000 and angle error < 5°.
6. **Map & State Logging**: Full map data and per-turn state dumps to stderr for debugging.

---

### Known Weaknesses (Why We Lose to Noobkins)

1. **Blocker is useless**: With only 6-turn horizon, the GA can't plan long-range intercepts. The blocker wanders aimlessly, often targeting points off the map.
2. **No solution persistence**: Each turn starts GA from scratch instead of shifting the previous turn's best plan forward. This wastes enormous search effort.
3. **GA controls both pods jointly**: A mutation to the blocker's genes can ruin a good runner plan and vice versa.
4. **Evaluation is too coarse**: 50K reward per CP dominates; no speed bonus, no angle-to-target penalty.
5. **Opponent model is naive**: Uses `ApplyBasicProxy` (always thrust 200 toward CP), doesn't model braking/blocking.
6. **Boost timing is suboptimal**: Doesn't pre-compute the longest straight segment.

---

### Strategic Priorities for Legend League

| Priority | Change | Impact |
|----------|--------|--------|
| P1 | Replace GA blocker with heuristic state machine (CHASE/CAMP/RAM/SHIELD) | Critical |
| P2 | Implement solution persistence (warm-start from previous turn's shifted plan) | Major |
| P3 | Split runner/blocker into separate GA populations | Major |
| P4 | Improve evaluation function (speed bonus, angle penalty, smoother CP rewards) | Moderate |
| P5 | Better opponent modeling with plan persistence | Moderate |
| P6 | Pre-compute optimal boost segment at game start | Minor |
| P7 | Fix collision overlap detection bug | Minor |

---

### File Map

```
mad_pod_arena/
├── src/
│   ├── cg/cg_bot.cpp          # Monolithic file for CodinGame IDE (copy-paste this)
│   ├── engine/                 # Physics engine (engine.cpp/h, arena.cpp/h, bot.h)
│   ├── bot/                    # GA bot (ga_bot.cpp/h)
│   └── tournament/             # Swiss tournament runner
├── scripts/                    # Python utility scripts
├── tools/                      # Physics validator, test tools
├── logs/                       # Tournament & game logs
├── gemini.md                   # THIS FILE — context dump
├── README.md                   # Build & usage instructions
├── WORKSPACE.bazel             # Bazel workspace
└── MODULE.bazel                # Bazel module config
```

### Build Commands
```bash
bazel build //...                              # Build everything
bazel run //src/tournament:tournament           # Run full tournament
bazel run //src/tournament:fast_tournament      # Run quick tournament
```
