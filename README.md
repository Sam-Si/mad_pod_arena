# Mad Pod Arena - Genetic Algorithm Bot

Welcome to the Mad Pod Arena Genetic Algorithm Bot repository. This repository contains a complete physics engine, a hyper-parameter search tournament runner, and a highly competitive Genetic Algorithm (GA) bot designed for the CodinGame Mad Pod Racing challenge.

## Project Structure
- `src/cg/cg_bot.cpp`: The monolithic C++ file. This is the **only file you need to copy-paste** into the CodinGame IDE. It contains the merged physics engine, GA, and game loop.
- `src/engine/`: The core physics and collision engine, perfectly replicating the CodinGame servers.
- `src/bot/`: The Genetic Algorithm implementation (`ga_bot.cpp`) used by the tournament runner.
- `src/tournament/`: A parallelized Swiss-system tournament runner used to evaluate thousands of bots with randomized hyper-parameters to find the optimal configuration.
- `scripts/`: Python utility scripts used for log parsing and automated code refactoring.
- `tools/`: Diagnostic C++ tools to validate physics parity against server logs.

## Building with Bazel

This repository is managed using the [Bazel](https://bazel.build) build system.

**Build everything:**
```bash
bazel build //...
```

**Run the Tournament:**
To run a massive Swiss-system tournament across thousands of randomized bot configurations:
```bash
bazel run //src/tournament:tournament
```

**Run the Fast Tournament:**
To run a faster micro-tournament (great for quick testing):
```bash
bazel run //src/tournament:fast_tournament
```

## How to Deploy to CodinGame
1. Build the standalone file to ensure there are no compilation errors:
   `bazel build //src/cg:cg_bot`
2. Open `src/cg/cg_bot.cpp`.
3. Select all, copy, and paste it into the CodinGame IDE.
4. Modify `BotConfig config` inside `main()` if you wish to adjust the bot's behavior (e.g., `horizon`, `population`, `block_weight`).
