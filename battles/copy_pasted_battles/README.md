# Copy-pasted / share-replay battles

Hand-fetched CodinGame replays (and optional human-readable notes) for local
physics and bot verification.

| Pattern | Role |
|---|---|
| `battle_<id>.json` | Full `gameResult/findByGameId` payload (`frames`, actions, keyframes) |
| `NNN.md` | Optional IDE log / notes (not used by automated gates) |

Fetch example:

```bash
python3 .grok/skills/cg-battle-logs/scripts/fetch_battle.py \
  'https://www.codingame.com/share-replay/GAME_ID'
```

EXACT Fidelity check:

```bash
python3 sim/compare_battle.py --exact --continue-on-fail battles/copy_pasted_battles/battle_GAME_ID.json
```

Fidelity vs `fast_physics` is covered by `sim/validate_fast_physics_corpus.py` (includes this directory).
