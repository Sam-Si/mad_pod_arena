# Merged from `codingame-csb-physics`

On 2026-06-22 this monorepo absorbed everything useful from
[`codingame-csb-physics`](https://github.com/Sam-Si/codingame-csb-physics).

## Decision: `mad_pod_arena` survives

| Criterion | mad_pod_arena | codingame-csb-physics |
|-----------|---------------|------------------------|
| Scope | Bot + engine + physics + battles | Physics verification only |
| Leaderboard battles | ~21,471 unique | ~17,011 (strict subset) |
| Timeouts / test sessions | Full set (889 / 312 / 130) | Identical subset |
| Build system | Bazel + feature branches | Loose g++ only |
| Physics engine | `src/physics/physics.h` (evolved, namespaced) | Older non-namespaced variant |
| Bot / tournament | Yes (`src/cg`, `src/engine`, `src/tournament`) | No |

## What was imported (deduped)

| Asset | Destination | Notes |
|-------|-------------|-------|
| Battle categorization | `battles/leaderboard_battles_categorized/manifest.csv` + README | **Index only** — no duplicate JSON |
| `replay_driver` hang fixes | `src/physics/replay_driver.cpp` | STEP_DONE on error; SET_POD sets initialized |
| `.clang-format` | repo root | LLVM-based style |
| Research / rules docs | already under `docs/` | physics root copies were duplicates |
| `sim/*.py` | kept mad versions | mad paths target `src/physics/`; stricter tol optional later |

## What was deliberately **not** copied

- **17k categorized battle JSON copies** — same bytes as `leaderboard_battles/`
- **`physics/physics.h` from physics repo** — mad's version is the maintained source of truth
- **`physics/replay_driver` binary** — build artifact
- **`.venv/`, `.idea/`, `.cache/`** — local env noise
- **`setup_env.sh`** — superseded by `setup.sh` / `setup-ubuntu.sh`

## Retired repository

`codingame-csb-physics` should be archived/deleted on GitHub; local copy may be moved to trash.
All future work happens here.
