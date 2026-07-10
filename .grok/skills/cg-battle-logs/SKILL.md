---
name: cg-battle-logs
description: >
  Fetch and interpret CodinGame Coders Strike Back / Mad Pod Racing battle
  replay JSON (share-replay URLs, gameResult APIs, frames/keyframes/actions,
  stderr VERIFY). Use when the user pastes a codingame.com/share-replay/ or
  /replay/ URL, a game id, battle_*.json, or asks to scrape/read/compare CG
  server logs for physics fidelity. Slash: /cg-battle-logs
---

# CodinGame battle logs (Mad Pod / CSB)

## Goal

Turn a **share-replay URL or game id** into structured turn data (checkpoints,
all-4-pod actions, keyframe state, ranks/tooltips, optional bot stderr) and use
it for **physics fidelity** work in this repo.

## When invoked

1. Extract **game id** from the user message (see URL shapes below).
2. **Fetch** if not already on disk (prefer local `battles/**/battle_<id>.json`).
3. **Parse** with repo `sim/battle_parser.py` when available; otherwise follow
   [references/schema.md](references/schema.md).
4. Summarize what matters for the user’s ask (outcome, first bad turn, VERIFY
   stderr, collisions, etc.). Do **not** dump full multi-MB JSON into chat.

## URL shapes → game id

| URL | game id |
|---|---|
| `https://www.codingame.com/share-replay/894831031` | `894831031` |
| `https://www.codingame.com/replay/894831031` | `894831031` |
| Local `battles/.../battle_894831031.json` | `894831031` |

Regex: `(?:share-replay\|replay)/(\d+)` or `battle_(\d+)\.json`.

## Which network calls matter (from browser “copy as cURL”)

### USEFUL — battle body (turn-level data)

| Endpoint | Body | Notes |
|---|---|---|
| **`POST /services/gameResult/findByGameId`** | **`[gameId, null]`** | **Primary.** Public replay payload: `frames`, `refereeInput`, `scores`, `ranks`, `tooltips`, `agents`. Works **without** login when 2nd arg is `null`. |
| Same | `[gameId, viewerUserId]` | Only if **you are that CodinGamer** (session cookies). Wrong user → `"You are not the given CodinGamer"`. Single-arg `[gameId]` → `Service not found: …findbygameid(1)`. |
| **`POST /services/gameResult/findInformationById`** | `["gameIdString", userId]` | Needs **matching session** (`cgSession` / `rememberMe`). Browser uses this on replay page; agent should prefer `findByGameId` + `null` unless private fields needed. |

**Fetch command (agent default):**

```bash
python3 .grok/skills/cg-battle-logs/scripts/fetch_battle.py 894831031
# or URL:
python3 .grok/skills/cg-battle-logs/scripts/fetch_battle.py 'https://www.codingame.com/share-replay/894831031'
```

Writes `battles/copy_pasted_battles/battle_<id>.json` by default.

Raw curl equivalent:

```bash
curl -sS 'https://www.codingame.com/services/gameResult/findByGameId' \
  -H 'content-type: application/json;charset=UTF-8' \
  -H 'origin: https://www.codingame.com' \
  -H 'referer: https://www.codingame.com/replay/GAME_ID' \
  --data-raw '[GAME_ID,null]' -o battles/copy_pasted_battles/battle_GAME_ID.json
```

Optional cookies: only if `null` fails or user needs **stderr from their own agent** on private views. Pass `CG_COOKIE` env to the fetch script (full Cookie header value). **Never commit cookies or paste them into git.**

### USELESS for physics / turn data (ignore in network dumps)

| Call | Why ignore |
|---|---|
| `maps.googleapis.com/.../gen_204` | Maps telemetry |
| `api-iam.eu.intercom.io/...` | Intercom chat |
| `/services/intercom/generateToken` | Intercom |
| `/services/Notification/findUnseenNotifications` | Site chrome |
| `/services/Puzzle/findProgressAndRankingById` | Puzzle rank UI (`[148, userId]` = CSB puzzle) |
| `/services/Quest/countLootableQuests` | Quests UI |
| `/services/Contribution/findNewContributionCount` | Contributions badge |
| `/services/FeaturedEvent/findNewFeaturedEventCount` | Events badge |
| `cdn-games.codingame.com/coders-strike-back/spritesheet.json` | Viewer art |
| `cdn-games.../Font/*.fnt` | Viewer fonts |

Repo scrapers already use the same primary API:
`battles/scripts/scrape_leaderboard.py` → `gameResult/findByGameId`.

## JSON anatomy (what we need)

Top-level keys on a good download:

- `gameId` — int
- `refereeInput` — text: `seed=…`, `map=x0 y0 x1 y1 …`, `pod_timeout=100`
- `frames` — **array**; this is the turn log (see framing below)
- `scores`, `ranks` — player outcomes (`ranks[i]==0` is winner index i)
- `tooltips` — often JSON strings with elimination text / turn
- `agents` — who played (`codingamer.pseudo`, `userId`)

### Frame rhythm (CSB / Mad Pod)

| Frame index | Typical | Role |
|---|---|---|
| `0` | `agentId: -1`, `keyframe: true` | Init: map, constants, spawn pods in `view` |
| `2t+1` | `agentId: 0` | Player 0 **stdout** (2 lines = 2 pods); often no pod `view` |
| `2t+2` | `agentId: 1`, often `keyframe: true` | Player 1 **stdout** + **committed keyframe** in `view` (4 pods) |

So **game turn `t`** (0-based) = frames `2t+1` + `2t+2`. Keyframe after both players moved is on **player 1’s frame**.

### Fields on a frame

- `stdout` — bot output: `targetX targetY thrust|BOOST|SHIELD` (2 lines)
- `stderr` — bot debug (VERIFY / ACTIONS); **often absent** on public `findByGameId` unless present in stored result
- `view` — referee viewer string (parse for pods / timeouts / collisions)
- `keyframe` — bool; commit frames have full pod blocks in `view`
- `gameInformation` / `summary` — human text; ranks in summary
- `agentId` — `-1` referee, `0`/`1` players

### Pod line in keyframe `view` (12 fields)

```
x y vx vy thrust shield_active target_x target_y angle_rad boosted next_cp z_order
```

Then a quoted message line per pod. Timeouts line like `progress0:timeout0 progress1:timeout1`. Optional collision lines after that.

Full field notes: [references/schema.md](references/schema.md).

## Parse with this repo (preferred)

```python
from sim.battle_parser import load_battle
b = load_battle("battles/copy_pasted_battles/battle_894831031.json")
# b.checkpoints, b.initial_state, b.turns[t].p0_pod0, b.keyframes[t].pods, b.ranks, b.tooltips
```

Physics compare (prefer **exact** when hunting edge cases):

```bash
# Coordinate-exact: pos/vel/timeout/next_cp Δ==0; angle within 1e-12 rad
python3 sim/compare_battle.py --exact --continue-on-fail battles/copy_pasted_battles/battle_GAME_ID.json

# Merge-style tolerances only (can hide unit-level drift):
python3 sim/compare_battle.py --gate-tolerances battles/copy_pasted_battles/battle_GAME_ID.json

MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py battles/copy_pasted_battles
```

Need `sim/replay_driver` (Bazel `//src/physics:replay_driver`). For fidelity work, **`--exact` is the truth signal**; GATE/EXPLORE are looser.

## Three layers (do not confuse)

1. **Keyframes** (`view` pods) — CG physics oracle for Fidelity / GATE.
2. **stdout actions** — inputs to replay; needed for offline perfect sim.
3. **ranks / scores / tooltips** — platform **match** outcome (invalid action, dual timeout); can disagree with “max progress” without being a physics bug.

Gate tolerances: pos ≤5, vel ≤3, ang ≤1°, timeout ≤1, `next_cp` exact mod track.

## Platform VERIFY vs battle JSON

- **Battle JSON** has **all 4** players’ actions → can reproduce multi-turn Fidelity debt (golden α/β/γ).
- **Live CG IDE stderr** only proves **our** one-step predict vs stdin re-seed; opp actions unknown.
- If user pastes a share-replay of **their** fight with SUBMIT bot, check frames for `stderr` with `VERIFY` / `OUR PODS`; if missing, public API may not include agent stderr—use IDE log paste or authenticated scrape.

## Agent workflow checklist

- [ ] Parse game id from URL / filename
- [ ] If missing locally, run `scripts/fetch_battle.py`
- [ ] Confirm `frames` non-empty and `gameId` matches
- [ ] Load via `sim.battle_parser.load_battle` when in repo
- [ ] Answer using checkpoints + turn `t` actions + keyframe pods / tooltips
- [ ] For physics fails: report first GATE breach turn and class (α angle / β pos / γ catastrophe / truncated / tooltip-only)
- [ ] Never store or commit user `cgSession` / `rememberMe` values

## Install locations

- **User (all sessions):** `~/.grok/skills/cg-battle-logs/` — preferred for global availability
- **Project mirror:** `.grok/skills/cg-battle-logs/` in mad_pod_arena

## Related repo paths (mad_pod_arena)

| Path | Role |
|---|---|
| `sim/battle_parser.py` | SSOT parse of CG JSON |
| `sim/compare_battle.py` / `verify_battles.py` | Fidelity vs keyframes |
| `battles/scripts/scrape_leaderboard.py` | Bulk `findByGameId` |
| `battles/RETENTION.md` | Min battle id policy |
| `docs/VERIFICATION_TRUTH_POLICY.md` | Gate governance |
| `experiments/cg_rust/SUBMIT_single_file.rs` | CG paste for live VERIFY |
