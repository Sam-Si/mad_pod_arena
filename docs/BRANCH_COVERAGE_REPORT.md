# Branch coverage report (measured multi-package proof)

**Generated:** 2026-07-07 20:05 UTC  
**Books:** Fowler *Refactoring* 1999, Fowler *Refactoring* 2018, Feathers *WEWLC* — see [`TESTING_BAR_FROM_BOOKS.md`](TESTING_BAR_FROM_BOOKS.md).

## Reproduce (single command)

```bash
./tools/run_branch_coverage.sh
# writes docs/coverage_phys_latest.txt
#       docs/coverage_engine_latest.txt
#       docs/coverage_ga_pure_latest.txt
#       docs/coverage_full_combined.md
# and runs real Bazel unit targets (must PASS)
```

| Suite binary | Product sources instrumented |
|---|---|
| `physics_branch_suite` | `ga_pure.h`, `progress.h`, `catalog.h`, `fidelity_math.h`, `fidelity_world_step.h`, `fast.h`, `fast_physics.h`, `physics.h` |
| `engine_arena_suite` | **`engine/engine.cpp`**, **`engine/arena.cpp`**, `engine.h`, `bot.h` (+ physics headers pulled in by arena) |
| `ga_pure_test_cov` | Same source as `//src/cg:ga_pure_test` |

Behavioral authority (not llvm-cov):  
`bazel test //src/physics:test_physics //src/cg:ga_pure_test //src/cg:amalgam_fast_smoke_test //src/engine:arena_fidelity_trace_test`

---

## Table A — Physics / core / cg-pure package (primary law)

```
Filename                              Regions    Missed Regions     Cover   Functions  Missed Functions  Executed       Lines      Missed Lines     Cover    Branches   Missed Branches     Cover
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
cg/ga_pure.h                               37                 0   100.00%          15                 0   100.00%          69                 0   100.00%          20                 0   100.00%
core/maps/catalog.h                         2                 0   100.00%           2                 0   100.00%          26                 0   100.00%           0                 0         -
core/progress.h                            20                 0   100.00%           3                 0   100.00%          17                 0   100.00%          14                 0   100.00%
coverage/physics_branch_suite.cpp         450               123    72.67%          10                 0   100.00%         375                 3    99.20%         174                64    63.22%
physics/fast.h                             93                38    59.14%           8                 1    87.50%         110                43    60.91%          76                51    32.89%
physics/fast_physics.h                    161                24    85.09%          21                 0   100.00%         214                10    95.33%         110                33    70.00%
physics/fidelity_math.h                    32                 0   100.00%           7                 0   100.00%          49                 0   100.00%          18                 0   100.00%
physics/fidelity_world_step.h              75                 1    98.67%           6                 0   100.00%         125                 0   100.00%          56                 6    89.29%
physics/physics.h                         240                44    81.67%          35                 0   100.00%         361                50    86.15%         160                54    66.25%
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
TOTAL                                    1110               230    79.28%         107                 1    99.07%        1346               106    92.12%         628               208    66.88%

```

### Pure islands at **100% branch** (physics suite)

| File | Branches | Lines | Functions |
|---|---|---|---|
| `src/cg/ga_pure.h` | **100%** (20/20) | 100% | 100% |
| `src/core/progress.h` | **100%** (14/14) | 100% | 100% |
| `src/physics/fidelity_math.h` | **100%** (18/18) | 100% | 100% |
| `src/core/maps/catalog.h` | n/a | 100% | 100% |

### Product files with remaining branch misses — **explicit reasons** (Criterion 4)

| File | Missed branches (approx) | Closed by real tests? | One-line reason for residual |
|---|---|---|---|
| `physics/fidelity_world_step.h` | 6 | Partially — all functions 100%, lines 100% | Residual multi-bounce / co-location / exact-on-circle epilogue combinations; **production trajectories locked by EXACT battle corpus** (10-sample + 100 quick). |
| `physics/fast_physics.h` | 33 | Partially — **100% functions** | Alternate rotate/boost/invalid move combinations in applyMove not all enumerated; exercised via step() paths + EXACT. |
| `physics/physics.h` | 54 | Partially — **100% functions** | `parseMove` edge forms, comparePod fail branches, applyRotate/applyThrust rare flags, checkWinner multi-outcome combos; driver API covered, not every boolean combination. |
| `physics/fast.h` | 51 | Partially — SimulateTurn + GetCollisionTime main paths hit | Collision loop is combinatorial (pair order × early-outs × impulse floor); **Fast goldens** in `//src/physics:test_physics` + EXACT are characterization for search fragment. |
| `coverage/physics_branch_suite.cpp` | (suite itself) | N/A | Test harness, not product. |

---

## Table B — Engine + Arena package (was missing; now primary)

```
Filename                                   Regions    Missed Regions     Cover   Functions  Missed Functions  Executed       Lines      Missed Lines     Cover    Branches   Missed Branches     Cover
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
cg/ga_pure.h                                    15                14     6.67%          15                14     6.67%          69                66     4.35%           0                 0         -
core/maps/catalog.h                              2                 0   100.00%           2                 0   100.00%          26                 0   100.00%           0                 0         -
core/progress.h                                 20                 1    95.00%           3                 0   100.00%          17                 1    94.12%          14                 5    64.29%
coverage/engine_arena_branch_suite.cpp         211                78    63.03%           8                 2    75.00%         122                 5    95.90%          76                32    57.89%
engine/arena.cpp                                98                24    75.51%           6                 0   100.00%         131                11    91.60%          78                33    57.69%
engine/bot.h                                     2                 1    50.00%           2                 1    50.00%           2                 1    50.00%           0                 0         -
engine/engine.cpp                               84                 6    92.86%          22                 0   100.00%          92                 4    95.65%          40                 7    82.50%
engine/engine.h                                  1                 0   100.00%           1                 0   100.00%          14                 0   100.00%           0                 0         -
physics/fast.h                                  90                47    47.78%           7                 3    57.14%         109                48    55.96%          76                58    23.68%
physics/fidelity_math.h                         32                 4    87.50%           7                 1    85.71%          49                 1    97.96%          18                 4    77.78%
physics/fidelity_world_step.h                   75                 6    92.00%           6                 0   100.00%         125                 2    98.40%          56                11    80.36%
physics/physics.h                              172                45    73.84%          35                17    51.43%         369               179    51.49%         100                34    66.00%
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
TOTAL                                          802               226    71.82%         114                38    66.67%        1125               318    71.73%         458               184    59.83%

```

| File | Branches | Lines | Functions | Residual reason |
|---|---|---|---|---|
| **`engine/engine.cpp`** | **82.5%** (7 missed) | **95.7%** | **100%** | Remaining: rare LUT/angle/boost demotion orderings not all hit; core Apply* / EndTurn / FastSimulateTurn paths exercised. |
| **`engine/arena.cpp`** | **57.7%** (33 missed) | **91.6%** | **100%** | Terminal-condition matrix (both-won same turn, both-timeout, checkWinner draw variants, max-turns) not all forced with stub bots; **main loop + map gen + SyncView + win/timeout paths run via PlayGame**. Full matrix would need scripted long races only for branch cosmetics. |
| `engine/engine.h` | n/a | 100% | 100% | — |
| `engine/bot.h` | 50% regions | 50% lines | 50% functions | Default empty `SetRoles` body not required when StubBot overrides; virtual dtor path. **Deferred: interface-only residual.** |

---

## Table C — `//src/cg:ga_pure_test` instrumented source (same as Bazel target)

```
Filename                      Regions    Missed Regions     Cover   Functions  Missed Functions  Executed       Lines      Missed Lines     Cover    Branches   Missed Branches     Cover
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
ga_pure.h                          37                 1    97.30%          15                 0   100.00%          69                 0   100.00%          20                 3    85.00%
ga_pure_test.cpp                   14                 3    78.57%           3                 0   100.00%          75                 7    90.67%          10                 5    50.00%
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
TOTAL                              51                 4    92.16%          18                 0   100.00%         144                 7    95.14%          30                 8    73.33%

```

Note: physics suite achieves **100% branch on `ga_pure.h`**; standalone `ga_pure_test` is slightly lower because it does not exercise every multi-wrap angle edge the physics suite adds — both drive the **shipped** header.

---

## 10-battle EXACT characterization (step-through real CG games)

```
=== 10-battle step-by-step EXACT characterization ===
  battle_885912413.json: OK stream
  battle_891669739.json: OK stream
  battle_891669740.json: OK stream
  battle_891669741.json: OK stream
  battle_891669742.json: OK stream
  battle_891669743.json: OK stream
  battle_891669744.json: OK stream
  battle_891669745.json: OK stream
  battle_891669746.json: OK stream
  battle_891669747.json: OK stream
Usable: 10/10
OK 70 turns EXACT
OK 123 turns EXACT
OK 249 turns EXACT
OK 207 turns EXACT
OK 166 turns EXACT
OK 100 turns EXACT
OK 245 turns EXACT
OK 154 turns EXACT
OK 125 turns EXACT
OK 100 turns EXACT
SUMMARY games=10 fails=0 turns=1539 EXACT


exit 0
>> OK 70 turns EXACT
>> OK 123 turns EXACT
>> OK 249 turns EXACT
>> OK 207 turns EXACT
>> OK 166 turns EXACT
>> OK 100 turns EXACT
>> OK 245 turns EXACT
>> OK 154 turns EXACT
>> OK 125 turns EXACT
>> OK 100 turns EXACT
>> SUMMARY games=10 fails=0 turns=1539 EXACT

```

**10/10 games, fails=0, 1539 turns EXACT.**

---

## Product code intentionally not at 100% branch — full exception list

| Product path | Status | Reason |
|---|---|---|
| `src/cg/internal/ga_prelude_and_search.inc` | Deferred non-goal for 100% branch | ~2.4k GA search/thread/I/O; pure scoring extracted to `ga_pure` (100%). Search strategy not physics law. Amalgam smoke proves paste builds. |
| `src/cg/internal/ga_main.inc` | Deferred | CG stdin main loop. |
| `src/cg/internal/ga_factory.inc` | Deferred | Factory wiring. |
| `src/cg/cg_bot.cpp` | Thin includes only | No logic. |
| `src/cg/bot_config.h` | Defaults only | No branches. |
| `src/tournament/*` | Tool | Not library law. |
| `src/physics/bench_*.cpp`, `replay_driver.cpp`, `verify_battles.cpp`, `validate_*.cpp` | Tools | Own binaries; not required for product-library branch bar. |
| `src/physics/json_minimal.h` | Support for tools | Used by tests/tools only. |
| Empty-track `Game::initialize` | Unexercised by design | Crashes (ASAN-proven); invalid product input. |
| Arena terminal-condition full matrix | Deferred | See arena.cpp row above — functions 100%; residual outcomes need multi-minute scripted games for cosmetics only. |

---

## Confidence statement (Fowler/Feathers-aligned)

1. **Pure product law** (`ga_pure`, `progress`, `fidelity_math`, maps): **measured 100% branch**.  
2. **Physics façades / world step**: high function cover (often 100%) + EXACT multi-battle characterization.  
3. **Engine + Arena**: now **instrumented and published**; engine.cpp 100% functions / 82.5% branches; arena.cpp 100% functions / ~58% branches with explicit residual reasons.  
4. **Real unit targets** (`test_physics`, `ga_pure_test`, `arena_fidelity_trace_test`, amalgam smoke): **PASS**.  
5. **Not claimed:** 100% of every branch in the GA monster or every tool binary.

Re-run: `./tools/run_branch_coverage.sh` then open this file + `docs/coverage_*_latest.txt`.
