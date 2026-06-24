# Mad Pod Arena

A competitive Genetic Algorithm bot for CodinGame's
[Mad Pod Racing](https://www.codingame.com/multiplayer/bot-programming/mad-pod-racing)
(aka *Coders Strike Back*), plus a referee-faithful physics engine verified
against real battle replays.

> **AI contributors:** see [GEMINI.md](GEMINI.md) for coding conventions and physics semantics.

---

## Quick map of the repo

```
mad_pod_arena/
│
├── README.md                 ← you are here
├── GEMINI.md                 ← AI / contributor conventions
│
├── src/                      ★ all first-party C++ code
│   ├── cg/                   CodinGame submission bot (GA)
│   ├── engine/               Bot/arena physics (search + game runner)
│   ├── physics/              Referee-faithful physics (battle-verified)
│   └── tournament/           Self-play benchmarks (CGBot vs CGBot)
│
├── sim/                      ★ Python tools to verify physics vs real battles
│
├── battles/                  ★ CodinGame replay corpora + scrapers
│   ├── test_session_battles/     Golden corpus (100% pass target)
│   ├── test_session_timeouts/    Agent-timeout games (segregated)
│   ├── leaderboard_battles/      Large leaderboard scrape (+ rank_* subdirs)
│   ├── leaderboard_timeouts/     Agent-timeout leaderboard games
│   ├── leaderboard_battles_categorized/  Outcome index (manifest.csv only; no dup JSON)
│   ├── copy_pasted_battles/      Hand-copied edge cases
│   └── scripts/                  Scrape / migrate / dedup helpers
│
├── third_party/referees/     External referee & arena sources (read-only refs)
├── docs/                     Rules, verification notes, research logs
│   └── archive/              Merge notes (e.g. retired codingame-csb-physics)
├── tools/                    Bazel helpers (cpp_opts.bzl, …)
├── setup.sh / setup-ubuntu.sh
└── BUILD.bazel, MODULE.bazel, WORKSPACE.bazel
```

| Want to… | Go here |
|---|---|
| Edit / submit the GA bot | [`src/cg/cg_bot.cpp`](src/cg/cg_bot.cpp) |
| Change bot/arena physics used by the GA | [`src/engine/`](src/engine/) |
| Change referee-accurate physics | [`src/physics/physics.h`](src/physics/physics.h) |
| Verify physics vs real CG battles | [`sim/`](sim/) + [`battles/`](battles/) |
| Run CGBot self-play benchmark | [`src/tournament/`](src/tournament/) |
| Read game rules | [`docs/rules.md`](docs/rules.md) |
| Deep physics verification write-up | [`docs/physics-verification.md`](docs/physics-verification.md) |
| External referee implementations | [`third_party/referees/`](third_party/referees/) |
| AI coding conventions | [`GEMINI.md`](GEMINI.md) |

---

## Single sources of truth (do not fork physics elsewhere)

| Concern | **Only** edit here | Verified by |
|---|---|---|
| **Referee / CG-server physics** | [`src/physics/physics.h`](src/physics/physics.h) | CI physics gate + `//src/physics:verify_battles` |
| **Bot / arena / GA search physics** | [`src/engine/engine.h`](src/engine/engine.h) + [`engine.cpp`](src/engine/engine.cpp) | `//src/engine` tests, tournaments |
| **Battle retention** | [`battles/RETENTION.md`](battles/RETENTION.md) (`id > 870230019`) | `battles/scripts/enforce_retention.py` in CI |

| | `src/physics/` | `src/engine/` |
|---|---|---|
| **Purpose** | Referee-faithful simulation (**CG SSoT**) | Bot search + arena runner (**bot SSoT**) |
| **Validated by** | Golden replays in `battles/test_session_battles/` | Unit tests / diff vs Go referee |
| **Used by** | Verifiers (`verify_battles`, `sim/` → `replay_driver`) | GA bot, arena, tournament |
| **Precision** | `double`, full referee edge cases | Optimized; GA path may skip checks |

`src/engine/csb_physics.h` is **not** referee physics — experimental engine helper only.

**Rule of thumb:** reproducing the CodinGame server → `src/physics/` only.
Improving bot search speed/quality → `src/engine/` only.
Never paste physics loops into bots, `sim/`, or third_party wrappers.

### CI / CD (GitHub Actions)

| Workflow | Job | Requirement |
|---|---|---|
| [`ci.yml`](.github/workflows/ci.yml) | `battle-retention` | No battles with `id <= 870230019` |
| [`ci.yml`](.github/workflows/ci.yml) | `build-and-test` | `bazel build/test //...` |
| [`ci.yml`](.github/workflows/ci.yml) | **`physics-accuracy`** | **`verify_battles` on `test_session_battles` must be 100%** |
| [`scheduled-tests.yml`](.github/workflows/scheduled-tests.yml) | nightly | Full build + leaderboard corpus report |

Local equivalent of the physics gate:

```bash
bazel build //src/physics:verify_battles
bazel-bin/src/physics/verify_battles --dir battles/test_session_battles --stop-on-fail
```

---

## Quick start

### Prerequisites

- Bazelisk / Bazel (see `.bazelversion`)
- C++17 toolchain
- Python 3 (for verification / diff tests)

```bash
# macOS / Linux bootstrap
./setup.sh          # macOS-oriented
./setup-ubuntu.sh   # Ubuntu-oriented
```

### Build everything

```bash
bazel build //...
```

### Run the bot benchmark

```bash
# CGBot self-play, all maps, both sides
bazel run //src/tournament:benchmark_tournament

# Custom: maps 0-5, 3 repeats per side
bazel run //src/tournament:benchmark_tournament -- --start-map 0 --end-map 6 --repeats 3

# Help
bazel run //src/tournament:benchmark_tournament -- --help
```

### Deploy to CodinGame

```bash
# 1. Build to verify no compile errors
bazel build //src/cg:cg_bot

# 2. Optional: standalone build that inlines the engine for CG paste
bazel build //src/cg:cg_bot_standalone

# 3. Copy src/cg/cg_bot.cpp into the CodinGame IDE
```

---

## Physics verification (battle replays)

Verify the referee-faithful engine (`src/physics/physics.h`) turn-by-turn against
real CodinGame battle JSONs.

### C++ batch verifier (Bazel)

```bash
bazel build //src/physics:verify_battles

# Golden corpus (should pass 100%)
bazel-bin/src/physics/verify_battles --dir battles/test_session_battles

# Large leaderboard corpus
bazel-bin/src/physics/verify_battles --dir battles/leaderboard_battles

# Single battle, verbose
bazel-bin/src/physics/verify_battles --file battles/test_session_battles/battle_891669739.json --verbose
```

### Python harness (`sim/`)

Uses `replay_driver` (auto-built via `g++`, or build with Bazel first).

```bash
# Verify golden corpus
python3 sim/verify_battles.py battles/test_session_battles

# Debug one battle in detail
python3 sim/compare_battle.py battles/test_session_battles/battle_891669739.json
```

Or via Bazel for the driver only:

```bash
bazel build //src/physics:replay_driver
# Then point sim/physics_driver.py at bazel-bin/src/physics/replay_driver if desired
```

### Battle corpora

| Directory | What it is |
|---|---|
| `battles/test_session_battles/` | ~312 games from an IDE test session — **primary golden set** |
| `battles/test_session_timeouts/` | Same session, but agent timed out (not physics bugs) |
| `battles/leaderboard_battles/` | Large leaderboard scrape (flat `battle_*.json` + optional `rank_*` per-player folders) |
| `battles/leaderboard_timeouts/` | Leaderboard games with agent timeouts |
| `battles/copy_pasted_battles/` | Manually saved edge cases (markdown) |
| `battles/scripts/` | Scrape / migrate / dedup utilities |

Agent-timeout battles are **segregated on purpose** — they are not physics failures.

---

## Build targets reference

| Target | Use |
|---|---|
| `//src/cg:cg_bot` | Local build (shared engine via `#include`) |
| `//src/cg:cg_bot_standalone` | CodinGame submission (inlines engine, `-DCG_STANDALONE`) |
| `//src/tournament:benchmark_tournament` | CGBot self-play benchmark |
| `//src/engine:test_physics` | Differential test harness vs Go referee |
| `//src/physics:physics` | Header-only referee physics library |
| `//src/physics:verify_battles` | C++ batch verifier over battle JSONs |
| `//src/physics:replay_driver` | Text driver for the Python `sim/` harness |
| `//src/physics:test_physics` | Unit/smoke tests for referee physics |

### Engine diff tests (Go referee)

```bash
# Build the C++ side first
bazel build //src/engine:test_physics

# Run 50 random cases
python3 src/engine/diff_test.py
```

---

## `src/` layout (detail)

```
src/
  cg/
    cg_bot.cpp          GA bot — uses shared engine locally; inlines it for CG submission
    patch_*.py          Small helpers for bot patching experiments
  engine/
    engine.cpp/h        PhysicsSimulator (reference) + GAPhysicsSimulator (GA-optimized)
    arena.cpp/h         Game loop, 18 real CG maps, timeout/win detection
    bot.h               IBot interface + BotConfig hyperparameters
    csb_physics.h       Shared CSB physics helpers used by engine
    test_physics.cpp    Diff harness vs Go referee
    diff_test.py        Python runner for the above
  physics/
    physics.h           Referee-faithful engine (battle-verified)
    verify_battles.cpp  Batch verifier over battles/*.json
    replay_driver.cpp   stdin/stdout driver for sim/physics_driver.py
    json_minimal.h      Tiny JSON reader for battle files
    maps.h              Checkpoint maps for tests
  tournament/
    benchmark_tournament.cpp
    cg_bot_wrapper.h
```

---

## Game rules (summary)

Full rules: [`docs/rules.md`](docs/rules.md). Short version:

- **Goal:** first pod to complete all laps wins.
- **Map:** 16000 × 9000, origin top-left; checkpoints radius 600.
- **Pods:** 2 per player; output `targetX targetY thrust|BOOST|SHIELD` each turn.
- **Thrust:** 0–200; **BOOST** = 650 once; **SHIELD** = 10× mass, no thrust for 4 turns (incl. activation).
- **Turn order:** rotate (max 18°/turn except turn 0) → accelerate → move + collisions → friction (`trunc(v*0.85)`) → round positions.
- **Collisions:** elastic, min impulse 120, pod radius 400 (collide at dist ≤ 800).
- **Timeout:** 100 turns without hitting next checkpoint → eliminated.

### Expert turn loop

1. **Rotation** — toward target, max ±18° (except first turn: snap).
2. **Acceleration** — thrust along facing; SHIELD/BOOST special cases.
3. **Movement + collisions** — continuous-time sweep; chronological pod-pod bounces.
4. **Friction + rounding** — `v = trunc(v * 0.85)`, `p = floor(p + 0.5)`.
5. **Timers** — decrement shield cooldowns and player timeouts.

---

## Third-party & docs

| Path | Contents |
|---|---|
| `third_party/referees/coders-strike-back-referee/` | Go referee (robostac) |
| `third_party/referees/csb-referee/` | C referee |
| `third_party/referees/CG-CSB-Arena/` / `CSB-Runner-Arena/` | Community arenas |
| `third_party/referees/coders-strike-back/` | Magus-style reference |
| `third_party/referees/tcourreges-…` / `csb_en.html` | Rules / site mirrors |
| `docs/rules.md` | Game rules |
| `docs/physics-verification.md` | Full verification methodology & edge cases |
| `docs/research/` | Long exploration logs (optional reading) |

---

## Contributing / conventions

1. **Single source of truth** for bot physics: `src/engine/` only.
2. **Single source of truth** for referee physics: `src/physics/physics.h` only.
4. **`cg_bot.cpp` `#ifdef CG_STANDALONE` block** must mirror `engine.h`/`engine.cpp` when you change physics used by the bot.
5. Prefer relative paths and Bazel targets; avoid hard-coded absolute machine paths.

See [GEMINI.md](GEMINI.md) for constants, shield semantics, and test commands.
