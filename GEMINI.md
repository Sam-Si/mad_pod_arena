# Mad Pod Arena — AI Coding Conventions

## Repo map (read this first)

```
src/cg/           GA bot (CodinGame submission)
src/engine/       Bot/arena physics (search + game runner)  ← bot simulation SSoT
src/physics/      Referee-faithful physics (battle-verified) ← CG server SSoT
src/tournament/   Benchmarks
sim/              Python battle verification harness
battles/          Real CG replays + scrape scripts
third_party/      External referees (read-only references)
docs/             Rules, verification write-up, research logs
```

Full human-oriented overview: [README.md](README.md).

## Core Principles

### 1. DRY (Don't Repeat Yourself) — two physics SSoTs, not one
- **Bot / arena physics** lives only in `src/engine/engine.h` + `engine.cpp` (**transitional** — SSOT program moves Fast into `src/physics/physics.h` profiles; see [`docs/SSOT.md`](docs/SSOT.md)). Never duplicate it elsewhere **outside that program**.
- **Referee / CG-server physics** lives only in `src/physics/physics.h`. **`MERGE_PHYSICS_OK`** = job `physics-accuracy` (Python `sim/verify_battles.py --gate` + golden `--tier pass` + `test_physics`). C++ `//src/physics:verify_battles` is **DIAGNOSTIC**. See [`docs/VERIFICATION_TRUTH_POLICY.md`](docs/VERIFICATION_TRUTH_POLICY.md).
- `cg_bot.cpp` uses the shared engine via `#include "src/engine/engine.h"`. The inline engine copy is only active when `-DCG_STANDALONE` is defined (for CodinGame submission).
- Changing bot search physics? Edit `src/engine/` only. Changing CG fidelity? Edit `src/physics/` only.

### 2. Two simulators *inside* `src/engine/` — know the difference
- **`PhysicsSimulator`**: Reference physics for the arena game runner (matches CG intent).
- **`GAPhysicsSimulator`**: Optimized approximation for the GA search loop. Deliberately omits some checks for speed (~2×). Used only for internal bot simulation.
- Both are defined in `engine.h`/`engine.cpp`. Never create a third copy in `src/engine/`.
- Separately, `src/physics/physics.h` is the **battle-verified** referee implementation (different package, different purpose).

### 3. Shield Timer Semantics
- Shield sets `shield_cd = 4` on activation turn.
- Mass is `10.0` only when `shield_cd == 4` (activation turn only).
- `shield_cd` is decremented in `EndTurn()` (after physics), never in `Apply*Action()`.
- Pod cannot thrust while `shield_cd > 0` (4 total turns of no thrust, including activation).

### 4. File Roles
| File | Role | Modify? |
|---|---|---|
| `src/engine/engine.h` / `engine.cpp` | Bot/arena physics — bot SSoT | ✅ Yes |
| `src/engine/bot.h` | IBot interface + BotConfig | ✅ Yes |
| `src/engine/arena.h` / `arena.cpp` | Game runner, maps, win detection | ✅ Yes |
| `src/physics/physics.h` | Referee-faithful physics — CG SSoT | ✅ Yes |
| `src/physics/verify_battles.cpp` | C++ batch verifier over `battles/` | ✅ Yes |
| `src/physics/replay_driver.cpp` | Driver for `sim/physics_driver.py` | ✅ Yes |
| `sim/*.py` | Python verification harness | ✅ Yes |
| `src/cg/cg_bot.cpp` | GA bot logic (CodinGame submission) | ✅ Bot logic only |
| `src/cg/cg_bot.cpp` (`#ifdef CG_STANDALONE`) | Inline engine copy for CG submission | ⚠️ Must mirror engine.h/cpp |
| `src/engine/test_physics.cpp` | Removed SSOT PR-1 (depended on deleted `csb_physics.h`) | ❌ Gone |
| `src/engine/diff_test.py` | Python runner (50 random cases) | ✅ Yes |
| `battles/` | Replay corpora (data) | ✅ Add/scrape only |
| `third_party/` | External referees | 🚫 Treat as read-only refs |
| `docs/research/` | Long exploration logs | optional |

### 5. Build & Test Commands
```bash
# Build everything
bazel build //...

# MERGE_PHYSICS_OK local (gate A + policy checker; also run golden --tier pass in CI)
bazel build //src/physics:replay_driver && cp -f bazel-bin/src/physics/replay_driver sim/replay_driver
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
python3 sim/check_verification_policy.py

# DIAGNOSTIC C++ (stricter; not PR merge gate alone)
bazel build //src/physics:verify_battles
bazel-bin/src/physics/verify_battles --dir battles/test_session_battles

# Engine differential tests vs Go referee (must pass 50/50)
bazel test --config=ci //src/physics:test_physics
python3 src/engine/diff_test.py

# Run benchmark tournament
bazel run //src/tournament:benchmark_tournament -- --repeats 5

# Build standalone CG submission (for copy-paste to CodinGame)
bazel build //src/cg:cg_bot_standalone
```

### 6. Canonical References
Referee physics is cross-checked against:
- **Real CG battles**: `battles/test_session_battles/` (golden), `battles/leaderboard_battles/` (scale)
- **Go referee**: `third_party/referees/coders-strike-back-referee/` ([robostac/coders-strike-back-referee](https://github.com/robostac/coders-strike-back-referee))
- **Java referee**: Official CodinGame `Referee.java` — the actual server code

Deep write-up: [docs/physics-verification.md](docs/physics-verification.md).

### 7. Key Physics Constants
| Constant | Value | Notes |
|---|---|---|
| Max thrust | 200 | Per turn |
| Boost thrust | 650 | One-time per pod (referee) / per team (legacy bot bug) |
| Max rotation | 18°/turn | π/10 radians |
| Pod radius | 400 | Collision at dist ≤ 800 |
| Checkpoint radius | 600 | Pass within 600 of center |
| Friction | 0.85 | `vel = trunc(vel × 0.85)` |
| Min impulse | 120 | Collision floor |
| Shield mass | 10× | Only on activation turn |
| Shield cooldown | 4 turns | Including activation |
| Pod timeout | 100 turns | Must reach next CP |
| Map size | 16000 × 9000 | Origin at top-left |

### 8. Paths & hygiene
- Prefer **repo-relative** paths (`battles/...`, `src/...`) — no hard-coded `/Users/...` absolutes.
- Large non-build trees (`battles/`, `third_party/`, `docs/`, `sim/`) are in `.bazelignore`.
- Agent-timeout battles live in `*_timeouts/` dirs; do not mix them into pass-rate stats.

## Agent battle curriculum

Physics/bot learning tiers: [`docs/agent-battle-curriculum.md`](docs/agent-battle-curriculum.md). Prefer golden + divergence sidecars over full leaderboard scrape.
