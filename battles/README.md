# Battle corpora

Real CodinGame Mad Pod Racing replays for verifying **`src/physics/physics.h`**.

## Retention

See [`RETENTION.md`](RETENTION.md): battle id **> 870230019**; no truncated action streams (`n_frames > 1+2*n_turns+4`).

```bash
python3 battles/scripts/enforce_retention.py
python3 battles/scripts/enforce_retention.py --truncated
```

## Corpora

| Directory | Role | CI |
|---|---|---|
| **`test_session_battles/`** (~312) | Primary gate **(A)** — must be **100%** turn-perfect under GATE | **Required** |
| **`golden_physics_battles/`** (~200) | Stratified regression; **`expected_pass.txt` (~188)** = gate **(B)** | **Pass tier required** |
| `leaderboard_physics_divergences/` | Known long-tail fails (curriculum) | Optional |
| `leaderboard_battles/` | Full breadth scrape | Nightly / research |
| `copy_pasted_battles/` | Hand-fetched share-replay JSON (+ legacy notes) | Research / EXACT checks |
| `*_timeouts/` | Agent timeouts — **not** physics fails | Retention only |
| **`latest_battles/`** | Post-max-id scrape (id > prior local max) | Research / regression | 
| `scripts/` | Scrape, golden build/verify, retention | CI retention job |

## Golden tests (local)

```bash
# Gate B only (must pass)
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass

# Full golden report (fail tier tracked, not blocking)
python3 battles/scripts/verify_golden_corpus.py
```

Details: [`golden_physics_battles/README.md`](golden_physics_battles/README.md).
