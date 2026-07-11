# Latest battles (post local max id)

**Coders Strike Back / Mad Pod Racing** replays with `gameId` **strictly greater than** the max id already in the repo at scrape time.

| Field | Value |
|---|---|
| Puzzle | `coders-strike-back` |
| Pre-scrape max id | **894832693** |
| This folder count | **2320** (`battle_*.json`) |
| Id range | **894833673 … 895706302** |
| Retention | id > 870230019 |
| Scrape | `python3 battles/scripts/scrape_leaderboard.py --mode leaderboard --limit-leaderboard 80 --min-id 894832693 --output-dir battles/latest_battles` |
| Manifest | `manifest.csv` |
| GATE verify (2026-07-11, exit suite) | Passed **2315** / Failed **0** / Skipped **5** / Total **2320** / turn accuracy **100.00%** |

See investigation: [`docs/artifacts/PHYSICS_100PCT_LATEST_INVESTIGATION.md`](../../docs/artifacts/PHYSICS_100PCT_LATEST_INVESTIGATION.md).
