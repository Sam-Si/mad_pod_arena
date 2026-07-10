# Battle retention policy (hard rule)

**Minimum battle id:** `870230019` (exclusive)

Only battles with id **strictly greater than** `870230019` may exist in this repo:

```
keep:   battle_870230020.json  and newer
delete: battle_870230019.json  and older
```

## Why

Older leaderboard scrapes predate the physics lock-in window and are not part of
the verified corpus. Keeping them inflates the repo and confuses CI / metrics.

## Applies to

| Directory | Policy |
|-----------|--------|
| `leaderboard_battles/` | id > 870230019 only |
| `leaderboard_timeouts/` | id > 870230019 only |
| `test_session_battles/` | already all post-cutoff (golden CI set) |
| `test_session_timeouts/` | segregated agent timeouts (not physics fails) |
| `leaderboard_battles_categorized/manifest.csv` | index only; same cutoff |

## Enforcement

```bash
# Audit / prune anything that slipped in
python3 battles/scripts/enforce_retention.py          # report only
python3 battles/scripts/enforce_retention.py --delete # hard delete offenders
```

Scrapers should refuse to write ids ≤ 870230019 (see `MIN_BATTLE_ID` in scripts).

## Truncated action streams (removed)

Replays where the parser cannot recover a full action/keyframe stream are **invalid
for physics verification**. Criterion:

```text
n_frames > 1 + 2 * n_turns + 4
```

(`n_turns` = complete turns with both players’ stdout + 4-pod keyframe.)

Typical causes: agent timeout / crash mid-game, missing stdout lines, scrape
corruption. These files must **not** remain in physics corpora; do not “fix”
outcomes by falling back to CG `ranks` while claiming a full replay.

Removed from `leaderboard_battles/` (2026-06-30) — see `logs/truncated_replays_removed.txt`.
Re-scan / prune:

```bash
python3 battles/scripts/enforce_retention.py --truncated          # report
python3 battles/scripts/enforce_retention.py --truncated --delete  # remove
```

## CI expectation

- **Gate (must be 100%):** `battles/test_session_battles/` via `//src/physics:verify_battles`
- **Golden subset (~200):** `battles/golden_physics_battles/` — pass tier must be 100%
  (`python3 battles/scripts/verify_golden_corpus.py --tier pass`); fail tier tracks known divergences only
- **Optional / nightly:** post-cutoff `leaderboard_battles/` (may have known edge cases; see docs)
