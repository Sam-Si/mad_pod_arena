# Battle corpora

Real CodinGame *Mad Pod Racing* / *Coders Strike Back* replays used to verify
**one** referee physics implementation: [`src/physics/physics.h`](../src/physics/physics.h).

> **Hard retention rule:** only battles with id **> 870230019**. See [RETENTION.md](RETENTION.md).

## Directories

| Folder | Role | CI |
|---|---|---|
| `test_session_battles/` | **Golden set** (~312). Physics must be 100% here. | **Required gate** in `.github/workflows/ci.yml` |
| `test_session_timeouts/` | Agent timed out — segregated; not physics fails. | Not in physics gate |
| `leaderboard_battles/` | Large post-cutoff scrape (flat `battle_*.json` + optional `rank_*`). | Nightly (report) |
| `leaderboard_timeouts/` | Leaderboard agent timeouts. | Not in physics gate |
| `leaderboard_battles_categorized/` | **Index only** (`manifest.csv`) — no duplicate JSON. | — |
| `leaderboard_physics_divergences/` | **Couldn't match referee** — 44 leaderboard battles where physics diverges; each has `first_fail_turn` in `manifest.csv` + `*.divergence.json` sidecars (copies; originals stay in `leaderboard_battles/`). | — |
| `golden_physics_battles/` | **~200 golden regression set** — all divergences (`expected_fail`) + stratified pass samples from `test_session_battles` + `leaderboard_battles`. See `manifest.csv`. | Optional gate: pass tier only |
| `copy_pasted_battles/` | Hand-saved edge cases (markdown). | — |
| `scripts/` | Scrape / migrate / **enforce_retention.py** / **build_golden_corpus.py** / **verify_golden_corpus.py**. | Retention job |

## Single source of truth (physics)

| Concern | Location | Do not duplicate in |
|---|---|---|
| Referee / CG-server fidelity | `src/physics/physics.h` | `src/engine/`, `sim/`, bots |
| Bot search / arena runner | `src/engine/engine.h` + `engine.cpp` | `src/physics/` |
| Batch verification | `//src/physics:verify_battles` | Ad-hoc copies of physics |
| Python harness (optional) | `sim/` drives `replay_driver` → same `physics.h` | Own physics math |

## How to verify locally

```bash
# Retention
python3 battles/scripts/enforce_retention.py

# C++ verifier (same binary CI uses)
bazel build //src/physics:verify_battles
bazel-bin/src/physics/verify_battles --dir battles/test_session_battles

# Python harness (same physics.h via replay_driver)
python3 sim/verify_battles.py battles/test_session_battles
```

## Scripts

```bash
python3 battles/scripts/scrape_leaderboard.py --help
python3 battles/scripts/migrate_and_dedup.py
python3 battles/scripts/enforce_retention.py --delete   # prune if policy violated
```

## Agent curriculum

Labeled learning tiers for AI/agents: [`docs/agent-battle-curriculum.md`](../docs/agent-battle-curriculum.md) and indices in [`docs/agent_pack/`](../docs/agent_pack/).
