# Mad Pod Arena

A competitive Genetic Algorithm bot for CodinGame's [Mad Pod Racing](https://www.codingame.com/multiplayer/bot-programming/mad-pod-racing) challenge. Includes a physics engine replicating the CG server, a GA optimizer with runner/blocker roles, and a parallelized tournament system for hyperparameter search.

## Project Structure

```
src/
  engine/          Physics engine, collision detection, arena game runner
    engine.cpp/h   Core physics: rotation, thrust, friction, elastic collisions
    arena.cpp/h    Game loop with 18 real CG maps, timeout/win detection
    bot.h          IBot interface and BotConfig hyperparameters
  bot/             GA bot implementation
    ga_bot.cpp/h   Genetic algorithm with joint runner+blocker scoring
  cg/              CodinGame submission
    cg_bot.cpp     Monolithic file — copy-paste this into the CG IDE
  tournament/      Hyperparameter search
    tournament.cpp      Full tournament: Swiss + top-8 round-robin playoff
    fast_tournament.cpp Quick tournament for iteration
```

## Quick Start

```bash
# Build everything
bazel build //...

# Run a quick tournament (32 bots, 5 Swiss rounds)
bazel run //src/tournament:fast_tournament

# Run with custom size: 16 bots, 4 rounds
bazel run //src/tournament:fast_tournament -- 16 4
```

## Commands Reference

### Build

```bash
# Build all targets
bazel build //...

# Build only the CG submission file (check for compile errors before submitting)
bazel build //src/cg:cg_bot

# Build only the tournament runner
bazel build //src/tournament:tournament

# Build the fast tournament
bazel build //src/tournament:fast_tournament
```

### Run Tournaments

**Fast Tournament** — quick iteration, ~5-15 min depending on bot count:
```bash
# Default: 32 bots, 5 Swiss rounds, 6 games per match (3 maps x 2 sides)
bazel run //src/tournament:fast_tournament

# Custom: 64 bots, 6 rounds
bazel run //src/tournament:fast_tournament -- 64 6
```

**Full Tournament** — thorough search with playoff validation:
```bash
# Default: 256 bots, 8 Swiss rounds + top-8 round-robin playoff (all 18 maps x 2 sides)
bazel run //src/tournament:tournament

# Custom: 128 bots, 6 Swiss rounds
bazel run //src/tournament:tournament -- 128 6
```

**Mega Tournament** — extreme search with multi-stage culling:
```bash
# Default: 10,000 bots -> 128 -> 8 -> Top 2
# Generates a timestamped log file: tournament_log_YYYYMMDD_HHMMSS.txt
bazel run //src/tournament:mega_tournament

# Custom: start with 5,000 bots
bazel run //src/tournament:mega_tournament -- 5000
```

The full tournament has two stages:
1. **Swiss rounds** — pairs bots by win count, plays 6 games per match (3 random maps x 2 sides). Quickly narrows the field.
2. **Playoff** — top 8 play a round-robin across **all 18 maps x 2 sides = 36 games per pair**. Eliminates map luck and reliably surfaces the best configuration.

Both tournaments output copyable `BotConfig` code for the top bots.

### Deploy to CodinGame

```bash
# 1. Build to verify no compile errors
bazel build //src/cg:cg_bot

# 2. Copy src/cg/cg_bot.cpp contents into the CodinGame IDE
# 3. Adjust BotConfig in main() with the best hyperparameters from the tournament
```

## Hyperparameters (BotConfig)

| Parameter | Range | Description |
|---|---|---|
| `horizon` | 4-8 | GA lookahead depth (turns) |
| `population` | 20-80 | GA population size |
| `dist_weight` | 0.5-3.5 | Distance-to-checkpoint penalty |
| `align_weight` | 0.5-4.5 | Velocity alignment reward |
| `speed_bonus` | 0.0-1.0 | Raw speed reward |
| `lateral_penalty` | 0.0-2.0 | Sideways drift penalty |
| `angle_penalty` | 5-60 | Facing-wrong-direction penalty |
| `corner_cut_dist` | 200-800 | Corner-cutting offset (units) |
| `block_weight` | 0.0-10.0 | Blocker aggressiveness multiplier |
| `shield_penalty` | 0-100 | Penalty for using shield |
| `shield_ram_dist` | 600-1100 | Distance to trigger shield-ram |
| `opp_penalty` | 0.0-3.0 | Weight on penalizing opponent progress |
| `opp_model_ms` | 5-50 | Milliseconds spent modeling opponent |

## Physics Engine

The engine replicates the CodinGame/Magus referee exactly:

- **Rotation**: max +/-18 deg/turn, angle stored as float (not rounded between turns)
- **Thrust**: 0-200, applied along current heading. Shield sets thrust to 0 for 3 turns.
- **Movement**: continuous collision detection via quadratic intersection time
- **Collisions**: elastic bounce with minimum 120-unit impulse, shield mass = 10x normal
- **Friction**: `vel = trunc(vel * 0.85)` each turn
- **Position**: `pos = round(pos)` each turn
- **Checkpoint**: activated when pod center is within 600 units of checkpoint center
- **Maps**: 18 real maps captured from the CG server (3-6 checkpoints each)
