# Golden physics corpus (~200 battles)

Fixed regression set for referee physics (`src/physics/physics.h`).

Generated: 2026-06-23 19:28 UTC  
Builder: `battles/scripts/build_golden_corpus.py` (seed=42, target=200)

## Why this set

| Tier | Count | `expected_result` | Purpose |
|------|------:|-------------------|---------|
| **known_divergence** | 44 | `fail` | All battles from `leaderboard_physics_divergences/` — knife-edge cases we track but do **not** require 100% pass |
| **golden_sample** (test_session) | 86 | `pass` | Stratified slice of the original CI gate corpus |
| **golden_sample** (leaderboard) | 70 | `pass` | Breadth across post-cutoff leaderboard maps/playstyles |

Timeouts (`*_timeouts/`) are **excluded** — those are agent/runtime failures, not physics fidelity.

## Files

```
golden_physics_battles/
├── README.md
├── manifest.csv           # full index + expected_result + divergence metadata
├── expected_pass.txt      # filenames that must verify clean
├── expected_fail.txt      # known divergences (tracked, not gated)
└── battles/
    └── battle_*.json      # copies (sources unchanged)
```

## How to verify

```bash
# Only expected-pass subset (CI-style gate on this corpus)
python3 battles/scripts/verify_golden_corpus.py --tier pass

# Full report: pass tier must be 100%; fail tier reported but not fatal
python3 battles/scripts/verify_golden_corpus.py

# Rebuild this folder after changing quotas/divergences
python3 battles/scripts/build_golden_corpus.py --target 200 --seed 42
```

## CI recommendation

- **Hard gate:** `expected_pass` tier via `verify_golden_corpus.py --tier pass` (or keep using full `test_session_battles`)
- **Informational:** `expected_fail` tier count should stay stable; new unexpected fails on pass tier = regression

Do not require 100% on the whole folder — it intentionally includes known divergences.
