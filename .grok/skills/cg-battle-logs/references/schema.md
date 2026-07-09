# CodinGame CSB / Mad Pod battle JSON schema

Source API: `POST https://www.codingame.com/services/gameResult/findByGameId`  
Body: `[gameId, null]` (public) or `[gameId, viewerUserId]` (must be that user + session).

## Top-level

```json
{
  "gameId": 894831031,
  "refereeInput": "seed=...\npod_timeout=100\nmap=x0 y0 x1 y1 ...\npod_per_player=2\n",
  "frames": [ /* Frame */ ],
  "scores": [0.0, 2.0],
  "ranks": [1, 0],
  "tooltips": ["{\"turn\":392,\"text\":\"$0 did not reach the next checkpoint in time\",\"event\":0}"],
  "agents": [ { "index": 0, "codingamer": { "userId": 984614, "pseudo": "SamSi" }, "agentId": ..., "valid": true } ]
}
```

- **ranks**: `ranks[playerIndex]` = place; **0 = winner**.
- **scores**: platform score points (often 0/1/2 style for race).
- **tooltips**: elimination / invalid action messaging; critical for M1 outcome harness.

## Frame

```json
{
  "agentId": 0,
  "keyframe": false,
  "stdout": "tx ty thrust\ntx ty thrust\n",
  "stderr": "optional bot debug",
  "view": "referee string",
  "gameInformation": "Pod 1 of player $0 moves ...",
  "summary": "$0 rank: 1\n$1 rank: 2\n"
}
```

### Init frame 0 `view` (keyframe)

```
0
CodersStrikeBack
16000 9000 400 600 <laps> <angle_const?>
x0 y0 x1 y1 ...   # checkpoints
0 x y ...          # spawn / pod block start
```

### Mid-game keyframe `view` (usually on agentId 1 frames)

```
<frame_index> [endReached|InvalidInput]
x y vx vy thrust shield tx ty angle boosted next_cp z
"message"
... ×4 pods ...
progress0:timeout0 progress1:timeout1
[optional collision lines: id t poda xa ya podb xb yb force ix iy]
```

Pod field meanings match `sim.battle_parser.parse_pod_state`.

## Turn indexing for physics

For turn `t` (0-based):

- Actions player 0: `frames[2*t+1].stdout` (2 lines → pods 0,1 in CG order for that player)
- Actions player 1: `frames[2*t+2].stdout` (pods 2,3 in global 0..3 indexing used by Fidelity)
- Keyframe after turn: parse `frames[2*t+2].view` pods

Global pod order in keyframes: **p0 pod0, p0 pod1, p1 pod0, p1 pod1** (indices 0–3).

## Validation (download integrity)

Minimal checks (see `battles/scripts/scrape_leaderboard.is_file_accurate`):

- JSON object with `gameId` and non-empty `frames`
- Prefer size ≫ 1KB
- Optional: some frames have `stderr` for bot debug corpora

## Retention

Do not keep battles with `gameId <= 870230019` per `battles/RETENTION.md`.
