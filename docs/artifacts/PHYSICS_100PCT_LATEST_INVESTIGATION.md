# Investigation: Physics 100% turn accuracy + latest battle scrape

| Field | Value |
|---|---|
| **Branch** | `physics/investigate-100pct-and-latest-scrape` |
| **Base commit** | `ef43e00b` (`physics/max-fidelity`) |
| **Date** | 2026-07-11 |
| **Max local battle id (pre-scrape)** | **894832693** (`battles/copy_pasted_battles/battle_894832693.json`) |
| **Latest corpus** | `battles/latest_battles/` — **2320** replays with id ∈ **(894832693, 895706302]** |
| **Scratch evidence** | implementer: `branch.txt`, `max_battle_id.txt`, `scrape_attempt.log`, `verify_existing_or_baseline.log`, `verify_latest.log`, `latest_manifest.txt`, `latest_fail_list.txt`, `retention.log`, `test_scrape_min_id.out` |

## 1. Question

What remaining levers inside the **Fidelity physics engine** (if any) could still move the repo toward turn-level **100%** on *all* Mad Pod / CSB battles — and what is **not** a physics problem?

## 2. Current measured bar (existing corpora)

Evidence: `logs/full_corpus_verify_20260711_161422.log` + re-run Gate A (`verify_existing_or_baseline.log`).

| Corpus | Tested | Failed | Skipped | Turn accuracy |
|---|---:|---:|---:|---:|
| `test_session_battles` (Gate A) | 312 | 0 | 0 | **100%** (46364/46364) |
| `golden_physics_battles` | 200 | 0 | 0 | **100%** (36420/36420) |
| `leaderboard_physics_divergences` | 44 | 0 | 44* | **100%** (11620/11620) |
| `copy_pasted_battles` | 4 | 0 | 0 | **100%** (817/817) |
| `leaderboard_battles` root | 17437 | 0 | 68 | **100%** (3613397/3613397) |

\* Divergence **skips** are `battle_*.divergence.json` **metadata sidecars**, not physics fails (`glob battle_*.json` matches them).

**Re-run Gate A (this investigation):** 312/312, Failed:0, 100% turns.

**Conclusion on pre-scrape warehouse:** Under `GATE_*` (pos≤5, vel≤3, ang≤1°, timeout≤1, exact next_cp), Fidelity was **already 100%** on all non-timeout physics corpora including full post-cutoff leaderboard root.

## 3. Non-physics causes that look like “not 100%”

| Class | Mechanism | Fix surface |
|---|---|---|
| Agent process timeout | `$i: timeout!`, truncated streams | `*_timeouts/` policy — not Fidelity |
| Truncated / corrupt scrape | frames vs recoverable turns | `enforce_retention.py --truncated` |
| Platform match outcome | ranks/tooltips vs progress | harness oracle, not `physics.h` |
| Skipped non-battles | `*.divergence.json` sidecars | verifier glob |
| Scrape lag | local max id behind CG | **latest scrape (this work)** |

## 4. Physics-engine levers (if engine is the culprit)

Change only under Gate A+B green; one lattice change at a time.

| Lever | File | Symptom class |
|---|---|---|
| Rotate (first snap / ≤18° / target snap) | `fidelity_math.h` `applyFidelityRotate` | Angle α / cascade |
| Thrust ULP / nextafter lattice | `applyFidelityThrust` | pos/vel β off-by-1..few |
| Friction trunc + selective bump | `frictionTrunc` | end-turn vel ±1 |
| World bounce / TOI scan order | `fidelity_world_step.h` | catastrophic γ |
| CP segment strict `< rsq` + exact-radius epilogue | `cpCollide` + world | next_cp / timeout |
| Shield timer==4 mass 0.1 | `worldBounce` | shield rams |
| dest==pos / invalid_input skip | `applyFidelityMove` | rare action edges |
| Timeout refresh 101→−1 | passCheckpoint | timeout display |

**Not physics law:** GATE vs EXPLORE vs EXACT tolerances; C++ 0.01 diagnostic verifier; `csb::fast` GA fragment; outcome tooltips.

### Historical residual taxonomy (research)

- **α** angle ~1° long horizon  
- **β** pos/vel few units over GATE (often Δ=6 just past pos≤5)  
- **γ** multi-pod collision topology  

## 5. Latest scrape (id > 894832693)

| Item | Value |
|---|---|
| Puzzle | `coders-strike-back` (Mad Pod Racing) |
| Command | `scrape_leaderboard.py --mode leaderboard --limit-leaderboard 80 --min-id 894832693 --output-dir battles/latest_battles` |
| Scanned | Top **80** agents; **7214** game ids seen; **4390** filtered (≤ max or retention); **2320** pending |
| Download | **2320 succeeded, 0 failed** (~49 min) |
| Id range | min **894833673**, max **895706302** (all **> 894832693**) |
| Validity | All 2320 have `gameId` + non-empty `frames` (`manifest.csv`, `bad_count=0`) |
| Retention | `enforce_retention.py` + `--truncated`: **OK**, 0 offenders / 0 truncated |

Scraper extension: `--min-id` + `_eligible_game_id()` in `battles/scripts/scrape_leaderboard.py`. Unit tests: `battles/scripts/tests/test_scrape_min_id.py` (3 tests OK).

## 6. Verify results on `battles/latest_battles/`

From `verify_latest.log` (GATE tolerances, DIAGNOSTIC role):

| Metric | Value |
|---|---:|
| Total | 2320 |
| Passed | **2307** |
| Failed | **8** |
| Skipped | 5 |
| Perfect turns | 552098 / 553215 |
| Turn accuracy | **99.80%** |
| Wall time | ~75 s |

### Fail list (first fail turn)

| Battle | Turn | Types | Notes |
|---|---:|---|---|
| `895131867` | 48 | pos | pod2/3 Δ≈6 units (just over GATE 5) — class **β** |
| `895340085` | 93 | pos | pod1 Δx=−6 — **β** |
| `895345570` | 99 | pos, vel | pod1 Δx=−6; mild vel — **β** |
| `895429566` | 90 | angle | pod1 ~2.2° over GATE 1° — **α** |
| `895515899` | 60 | pos | pod3 Δy=−7 — **β** |
| `895564994` | 47 | pos | pod2 Δx=−6 — **β** |
| `895612448` | 239 | pos | late game Δy=−7 — **β** |
| `895637720` | 297 | pos | late Δx=6 — **β** |

**No γ catastrophe** in this set. Dominant residual: **integer commit / ULP β** (position just past 5-unit GATE), plus one **angle α**.

### If physics is the culprit for these 8

Priority experiments (do **not** loosen GATE):

1. Binary-search first double divergence before GATE trip on each id.  
2. Check collision on fail turn (bounce vs free flight).  
3. Thrust/friction ULP near integer axes for the ±6 pos cases.  
4. Angle-only case `895429566`: rotate lattice / principal angle / long-horizon accumulate.  
5. Promote any pure numeric isolation to `//src/physics:test_physics`.  
6. Re-run Gate A + golden + these 8 after each change.

## 7. Bottom line

1. **Pre-existing warehouse was already 100% under GATE** — not inventing a green bar.  
2. **Fresh post-max-id battles surface a real ~0.2% fail rate (8/2320)** — long-tail Fidelity risk is **not zero** on new data.  
3. Failures cluster as **β (pos ±6..7)** + **one α (angle)** — classic residual classes; engine levers in §4 apply.  
4. Non-physics skips/timeouts remain separate.  
5. This branch delivers: investigation, `--min-id` scrape, `latest_battles` corpus + verify numbers for follow-up physics work.

## 8. Related paths

- Fidelity SSOT: `src/physics/fidelity_math.h`, `fidelity_world_step.h`  
- Gate: `docs/VERIFICATION_TRUTH_POLICY.md`, `sim/tolerance_policy.py`  
- Scraper: `battles/scripts/scrape_leaderboard.py`  
- Latest: `battles/latest_battles/`
