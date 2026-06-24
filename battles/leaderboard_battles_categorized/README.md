# Leaderboard Battles — Category Index

**Dedup note:** Battle JSON files live only in `battles/leaderboard_battles/`.
This folder stores **metadata only** (`manifest.csv`) so we do not keep 17k
duplicate copies. Use the manifest to filter/group battles by outcome class.

**Filter in manifest:** battles strictly after `battle_870230019.json` (id > 870230019).
**Total indexed:** 17011

Lookup example:
```bash
# list all marathon finishes
awk -F, '$3=="06_end_reached_marathon"{print $2}' manifest.csv

# verify one category via sim
python sim/verify_battles.py battles/leaderboard_battles \
  --filter "$(awk -F, '$3=="07_end_reached_sprint"{print $2}' manifest.csv | tr '\n' ' ')"
```

## Timeout labels

| Label | Count | Notes |
|-------|------:|-------|
| timeout_yes | 1604 | agent_program / pod_checkpoint in `timeout_type` |
| timeout_no | 15407 | all other |
| clean finish (subset of no-timeout) | 15368 | `19_timeout_no_clean_finish` primary_category |

Agent program timeouts are usually pre-segregated in `battles/leaderboard_timeouts/`.

## Primary categories (mutually exclusive, priority order)

| # | `primary_category` | Count | Description |
|--:|--------------------|------:|-------------|
| 1 | `02_invalid_input_abort` | 9 | Illegal thrust/power abort |
| 2 | `03_double_elimination` | 1 | Both players eliminated |
| 3 | `04_max_rounds_stalemate` | 32 | Turn/frame limit, no race finish |
| 4 | `05_end_reached_with_elimination` | 7 | Finish + elimination edge case |
| 5 | `06_end_reached_marathon` | 2139 | Finish with 300+ game turns |
| 6 | `07_end_reached_sprint` | 103 | Finish in under 80 turns |
| 7 | `08_end_reached_collision_fest` | 586 | Finish with >80 collisions |
| 8 | `09_end_reached_shield_heavy` | 8289 | Finish with 20+ SHIELD uses |
| 9 | `10_end_reached_boost_heavy` | 1332 | Finish with 4+ BOOST uses |
| 10 | `11_end_reached_standard` | 2919 | Baseline clean finishes |
| 11 | `13_elim_after_collision_battle` | 862 | Pod timeout after >50 collisions |
| 12 | `14_elim_shield_war` | 568 | Pod timeout with 15+ SHIELD |
| 13 | `15_elim_pod_timeout_standard` | 164 | Standard pod/checkpoint timeout |
| 14 | `18_timeout_yes_any` | 1604 | Any timeout (secondary view) |
| 15 | `19_timeout_no_clean_finish` | 15368 | No-timeout clean finish (secondary view) |

`manifest.csv` columns: `game_id,filename,primary_category,is_timeout,timeout_type,is_clean_finish,end_reached,max_rounds,invalid_input,p0_elim,p1_elim,winner,game_turns,n_frames,n_collisions,boost_count,shield_count,summary_tail`
