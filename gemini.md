# Mad Pod Racing - Genetic Algorithm Bot
## Context & State Dump

This file serves as a memory dump for the current state of the Mad Pod Racing bot project.

### Core Architecture
- **Language**: C++17 (compiled with `-O3` and loop unrolling).
- **Core Engine**: A 100% accurate physics engine (`engine.cpp`) that perfectly mirrors the CodinGame server's collision, friction, and rotation constraints. Validated using server-side `STATE DUMP` diffs.
- **Bot Engine (`cg_bot.cpp`)**: A Genetic Algorithm (GA) that evaluates `MAX_POPULATION` (e.g., 40) sequences of `MAX_HORIZON` (e.g., 6) moves.
- **Evaluation Heuristics**: 
  - Rewards Checkpoint passes (`cpspassed * 50000.0`).
  - Minimizes distance to the next Checkpoint (`dist * dist_weight`).
  - Penalizes Shield usage.
  - Modifies the Checkpoint `target` internally by shifting it 400 units towards the *next* Checkpoint, allowing the GA to dynamically cut corners (Checkpoint Sweeping).

### Recent Breakthroughs
1. **Dynamic Role Swapping**: The bots no longer have hardcoded "Runner" and "Blocker" IDs (0 and 1). Instead, a heuristic compares the race progress (`laps * 50000 + cp_index * 1000 - distance`). If the Blocker ends up further ahead of the Runner, the roles dynamically swap to ensure the optimal pod finishes the race.
2. **Turn 1 Forced Acceleration**: The GA was sometimes choosing low thrusts (e.g., 52) on Turn 1 because the optimal path sequence involved early braking. We bypassed this by forcing `Thrust = 200` if `angle == -1` (starting line).
3. **Aggressive Blocking**: Discovered that a `block_weight` of `0.0` caused the Blocker to wander aimlessly. Raising it to `5.0` forces the Blocker to aggressively intercept opponents.
4. **Short-Sighted Braking**: The GA naturally discovers that braking (Thrust 0) before a checkpoint is mathematically optimal for tight hairpin turns. This is an intended and highly effective behavior (Noobkins also uses it).

### Hyper-Parameters to Explore (BotConfig)
To beat top-tier bots like Noobkins in the Legend League, the following `BotConfig` parameters in `cg_bot.cpp` should be manually tweaked and tested:
- **`horizon` (4 to 8)**: Higher horizon prevents short-sighted braking but reduces the number of evaluated populations within the 50ms time limit.
- **`population` (20 to 50)**: More populations smooths out jittery movements and finds better tactical sequences.
- **`block_weight` (2.0 to 10.0)**: Determines how aggressively the Blocker hunts the opponent's Runner. Too high, and it might miss checkpoints or collide with its own Runner.
- **`dist_weight` (1.0 to 2.0)**: Determines how much the Runner prioritizes the exact target.
- **`align_weight` (0.0 to 3.0)**: Determines the reward for having velocity pointing exactly at the target.

### Current Files
- `cg_bot.cpp`: The monolithic file containing the entire engine + GA + game loop. This is copy-pasted into the CodinGame IDE.
- `tournament.cpp`: A Swiss-system tournament runner used to evaluate thousands of randomly mutated bots against each other to find the best hyper-parameters.
- `ga_bot.cpp`: The standalone version of the bot used by the tournament runner.
- `engine.cpp`: The standalone physics and collision engine used by the tournament runner.
