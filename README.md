# Mad Pod Arena

A competitive Genetic Algorithm bot for CodinGame's [Mad Pod Racing](https://www.codingame.com/multiplayer/bot-programming/mad-pod-racing) challenge. Includes a physics engine replicating the CG server, a GA optimizer with runner/blocker roles, and a parallelized benchmark system.

## Project Structure

```
src/
  cg/              CodinGame submission (single source of truth for bot logic)
    cg_bot.cpp     Monolithic file — copy-paste this into the CG IDE
  engine/          Physics engine, collision detection, arena game runner
    engine.cpp/h   Core physics: rotation, thrust, friction, elastic collisions
    arena.cpp/h    Game loop with 18 real CG maps, timeout/win detection
    bot.h          IBot interface and BotConfig hyperparameters
  legacy_bot/      [READ-ONLY] Legacy reference bot (never modify)
    legacy_bot.cpp
  tournament/      Benchmark tooling
    benchmark_tournament.cpp  CGBot vs LegacyBot benchmark (parallelized)
    cg_bot_wrapper.h          Adapts cg_bot.cpp to the engine IBot interface
    legacy_wrapper.h          Adapts legacy_bot.cpp to the engine IBot interface
```

## Quick Start

```bash
# Build everything
bazel build //...

# Run the benchmark (CGBot vs LegacyBot, all 18 maps, both sides)
bazel run //src/tournament:benchmark_tournament

# Custom: maps 0-5, 3 repeats per side
bazel run //src/tournament:benchmark_tournament -- 0 6 3
```

## Commands Reference

### Build

```bash
# Build all targets
bazel build //...

# Build only the CG submission file (check for compile errors before submitting)
bazel build //src/cg:cg_bot

# Build the benchmark
bazel build //src/tournament:benchmark_tournament
```

### Run Benchmark

The benchmark pits `cg_bot.cpp` (via `CGBotWrapper`) against the legacy bot across
all 18 real CodinGame maps, playing both sides to eliminate positional bias.

Games run in parallel with a dynamic batch size of **2 × hardware threads**.

```bash
# Default: all maps, 1 repeat per side
bazel run //src/tournament:benchmark_tournament

# 5 repeats per side across all maps
bazel run //src/tournament:benchmark_tournament -- --repeats 5

# Maps 0-5 only, 3 repeats, verbose GA output
bazel run //src/tournament:benchmark_tournament -- --start-map 0 --end-map 6 --repeats 3 --verbose

# Show all options
bazel run //src/tournament:benchmark_tournament -- --help
```

### Deploy to CodinGame

```bash
# 1. Build to verify no compile errors
bazel build //src/cg:cg_bot

# 2. Copy src/cg/cg_bot.cpp contents into the CodinGame IDE
```

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

## Game Rules

### Summary of New Rules
- **Maximum Thrust**: The maximum thrust value is now **200** instead of 100.

### The Goal
Win the race by completing the checkpoints first.

### Rules
- **Teams**: The players each control a team of two pods during a race. As soon as a pod completes the race, that pod's team is declared the winner.
- **Circuit**: The circuit of the race is made up of checkpoints. To complete one lap, your vehicle (pod) must pass through each one in order and back through the start checkpoint. The first player to reach the start on the final lap wins.
- **Map Dimensions**: The game is played on a map 16000 units wide and 9000 units high. The coordinate X=0, Y=0 is the top-left pixel.
- **Checkpoints**:
  - Checkpoints are circular, with a radius of **600** units.
  - Checkpoints are numbered from `0` to `N` where `0` is the start and `N-1` is the last checkpoint.
  - The disposition of the checkpoints is selected randomly for each race.
  - To pass a checkpoint, the center of a pod must be inside the radius of the checkpoint.
- **Pods Movement & Controls**:
  - To move a pod, you must print a target destination point followed by a thrust value.
  - The thrust value of a pod is its acceleration and must be between `0` and `200`.
  - The pod will pivot to face the destination point by a maximum of **18 degrees per turn** and will then accelerate in that direction.
  - **Boost**: You can use 1 acceleration boost in the race; you only need to replace the thrust value by the `BOOST` keyword.
  - **Shield**: You may activate a pod's shields with the `SHIELD` command instead of accelerating. This multiplies the pod's mass by **10** (normally 1), making it much heavier in collisions. However, the pod will not be able to accelerate for the next **3 turns**.
  - **Force-field/Collisions**: The pods have a circular force-field around their center with a radius of **400** units, which activates in case of collisions with other pods (collision at distance < 800 units).
  - **Boundary**: The pods may move normally outside the game area.
  - **Elimination**: If none of your pods make it to their next checkpoint in under **100 turns**, you are eliminated and lose the game. Only one pod needs to complete the race.

### Victory Conditions
- Be the first to complete all the laps of the circuit with one pod.

### Lose Conditions
- Your program provides incorrect output.
- Your program times out.
- None of your pods reach their next checkpoint in time (100 turns).
- Somebody else wins.

### Expert Rules
On each turn the pods movements are computed this way:
1. **Rotation**: The pod rotates to face the target point, with a maximum of **18 degrees** (except for the 1st round, where rotation snaps instantly).
2. **Acceleration**: The pod's facing vector is multiplied by the given thrust value. The result is added to the current speed vector.
3. **Movement**: The speed vector is added to the position of the pod. If a collision would occur at this point, the pods rebound off each other (continuous collision detection).
4. **Friction**: The current speed vector of each pod is multiplied by **0.85**.
5. **Rounding & Truncation**: The speed's values are truncated (`trunc`) and the position's values are rounded to the nearest integer (`round`).
6. **Collisions**: Collisions are elastic. The minimum impulse of a collision is **120**.
7. **Boost Detail**: A boost is in fact an acceleration of **650**. The number of boost available is common between pods (1 per team). If no boost is available, the maximum thrust is used.
8. **Shield Detail**: A shield multiplies the Pod mass by **10**.
9. **Angles**: The provided angle is absolute. 0° means facing EAST while 90° means facing SOUTH.

### Note
The program must first read the initialization data from standard input. Then, within an infinite loop, read the contextual data from standard input and provide the desired instructions to standard output.

### Game Input / Output

#### Initialization Input
- **Line 1**: `laps` : the number of laps to complete the race.
- **Line 2**: `checkpointCount` : the number of checkpoints in the circuit.
- **Next `checkpointCount` lines**: 2 integers `checkpointX`, `checkpointY` for the coordinates of each checkpoint.

#### Input for One Game Turn
- **First 2 lines**: Your two pods.
- **Next 2 lines**: The opponent's pods.
- Each pod is represented by: 6 integers: `x` & `y` for the position, `vx` & `vy` for the speed vector, `angle` for the rotation angle in degrees, and `nextCheckPointId` for the number of the next checkpoint the pod must go through.

#### Output for One Game Turn
- **Two lines**: 2 integers for the target coordinates of your pod followed by `thrust` (an integer acceleration between `0` and `200`), `SHIELD` (to activate shields), or `BOOST` (for an acceleration burst). One line per pod.

#### Constraints
- `0 <= thrust <= 200`
- `2 <= checkpointCount <= 8`
- Response time first turn `<= 1000ms`
- Response time per turn `<= 75ms`
