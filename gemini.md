# Mad Pod Racing - Genetic Algorithm Bot
## Context & State Dump for AI Collaborators

> [!IMPORTANT]
> **Legacy Bot Read-Only Rule**: The legacy bot located in `src/legacy_bot` is strictly a read-only reference bot. Under no circumstances should its code or files be modified.

### Problem Statement
This project implements a competitive AI bot for the [CodinGame Mad Pod Racing](https://www.codingame.com/multiplayer/bot-programming/mad-pod-racing) challenge. The goal is to control two pods racing around a circuit of checkpoints faster than the opponent's two pods. Each team designates a **Runner** (focuses on finishing the race) and a **Blocker** (focuses on disrupting the opponent's runner).

---

### Core Architecture

- **Language**: C++17, compiled with `-O3` and loop unrolling on CodinGame servers.
- **Build System**: Bazel (`bazel build //...`)
- **Core Engine** (`src/engine/`): A physics engine replicating the CodinGame server exactly:
  - Pods have radius 400 (collision at distance 800)
  - Checkpoints have radius 600
  - Per-turn: rotate → apply thrust → move linearly → resolve collisions → apply friction (`trunc(vel * 0.85)`) → round positions
  - Collision: Exact Magus bounce — impulse applied TWICE (raw + min-120 enforcement)
  - Shield: Sets mass to 10 (normally 1), 3-turn cooldown. Can re-shield during cooldown.
  - Boost: One-time 650 thrust instead of normal 200 max
  - Rotation: Max 18° per turn, instant-snap on Turn 1
- **Bot Engine** (`src/cg/cg_bot.cpp`): Hybrid GA + Heuristic Blocker:
  - Three genes per action: `gene1` (meta/shield), `gene2` (steering), `gene3` (thrust)
  - `gene1 > 0.95` → Shield; `gene1 < 0.3` → Direct-to-CP heuristic; else → manual control
  - Population of 80, horizon of 4 turns (tournament-optimized)
  - Two-phase GA: 15ms for opponent model, then 55-70ms for our plan
  - Solution persistence: warm-start from previous turn's shifted plan
  - Heuristic blocker: deterministic CHASE/RAM/SHIELD state machine
  - Joint scoring: opponent penalty, positional blocking bonus, friendly fire avoidance
- **Tournament Runner** (`src/tournament/`): Swiss-system tournament with 18 real CG maps

---

### Physics Engine Verification Status

✅ **Verified correct** against Magus reference implementation:
- Thrust application, velocity friction (`trunc(vel * 0.85)`), position rounding (`round`)
- Collision detection (quadratic equation, overlap returns t=0)
- Collision resolution: **Exact Magus bounce** (impulse applied TWICE: raw physics + min-120 enforcement)
- Shield mass (10.0), shield cooldown (3 turns), re-shielding during cooldown
- Rotation clamping (±18°/turn)
- Checkpoint radius (600 units)
- Shield activation order: check for `-1` BEFORE decrementing cooldown

---

### Current Hyper-Parameters (Tournament Winner Bot_0)

```cpp
config.horizon = 4;           // Turns of lookahead
config.population = 80;        // GA population size
config.dist_weight = 0.9;      // Distance-to-CP penalty
config.align_weight = 3.6;     // Velocity alignment reward
config.speed_bonus = 0.9;      // Raw speed reward
config.lateral_penalty = 1.7;  // Sideways drift penalty
config.angle_penalty = 55;     // Angle-to-target penalty
config.corner_cut_dist = 600;  // Corner-cutting offset
config.block_weight = 0.8;     // Blocker aggressiveness in joint scoring
config.shield_penalty = 49;    // Shield usage penalty
config.shield_ram_dist = 1100; // Distance to trigger shield-ram
config.opp_penalty = 0.9;      // Opponent progress penalty
```

---

### Key Features Implemented

1. **Dynamic Role Swapping**: Runner/Blocker roles swap based on race progress with 1500-point hysteresis.
2. **Heuristic Blocker**: Deterministic CHASE/RAM/SHIELD state machine replaces GA for blocker pod.
3. **Solution Persistence**: GA warm-starts from previous turn's shifted best solution.
4. **Joint Scoring**: Runner score + opponent penalty + positional blocking + collision bonus - friendly fire.
5. **Corner-Cutting**: Target shifted 600 units toward next CP.
6. **Turn 1 Forced Acceleration**: Always thrust 200 on starting line.
7. **Per-Move Timing**: Full timing breakdown (opp model, our plan, total) to stderr.
8. **Per-Pod Debug**: Role, target coordinates, thrust, and gene values logged per turn.

---

### File Map

```
mad_pod_arena/
├── src/
│   ├── cg/cg_bot.cpp          # Monolithic file for CodinGame IDE (copy-paste this)
│   ├── engine/                 # Physics engine (engine.cpp/h, arena.cpp/h, bot.h)
│   ├── bot/                    # GA bot (ga_bot.cpp/h)
│   ├── legacy_bot/            # [READ-ONLY] Legacy reference bot (NEVER MODIFY!)
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
bazel build //...                                        # Build everything
bazel run //src/tournament:tournament -- 128 6            # 128 bots, 6 rounds
bazel run //src/tournament:tournament -- 32 4             # Quick 32-bot test
```

### Tournament Details
- 18 real CodinGame server maps (no synthetic)
- Best-of-6 matches (3 random maps × 2 sides)
- Continuous parameter sampling across 12 config fields
- Per-round leader config dump (copyable C++)
- Full top-10 leaderboard with parameters at finish
