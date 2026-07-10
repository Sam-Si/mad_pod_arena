# Fowler *Refactoring* — compliance for mad_pod_arena

**Primary edition:** 2nd ed. 2018 (Fowler / Beck) — see [`FOWLER_2018_REPORT_AND_TEST_TRUTH.md`](FOWLER_2018_REPORT_AND_TEST_TRUTH.md).  
**Also:** 1st ed. 1999 (same process).  

**Behavioral source of truth:** **`./tools/run_truth_suite.sh`** (not docs, not untested paths).

## Process (Ch 2) — how we work

| Principle | Enforcement |
|---|---|
| Behavior-preserving structure change | Gate A, EXACT Fidelity vs rollout, fast goldens, amalgam smoke |
| Tests as safety net | `check_ssot_policy.py`, `check_verification_policy.py`, Bazel tests |
| Small steps | Shared world-step extracted; bot modularized for review |

## Smells (Ch 3) — status after goal close-out

| Smell | Status |
|---|---|
| **Duplicated Code** (Fidelity world step) | **Closed:** both `csb::Game` and `csb::fast_physics::Game` call `csb::simulateFidelityWorld` in `fidelity_world_step.h` |
| **Shotgun Surgery** (constants) | **Closed for friction/key physics numbers:** `core/constants.h` + `fidelity_math.h`; `fast.h` mirror guarded by policy |
| **Large Class** (bot) | **Reduced:** logic in `src/cg/internal/*.inc` modules; thin `cg_bot.cpp` driver; still large modules but reviewable ownership units |
| **Divergent Change** (config) | **Closed:** `BotConfig` in `src/cg/bot_config.h`; engine `bot.h` is thin `IBot` only |
| **Speculative Generality** | **Closed:** `experiments/` removed |
| **Wrong docs** | **Closed:** GEMINI/SSOT/Fowler docs aligned |

## Catalog moves applied

1. Extract Function / shared math — `fidelity_math.h`
2. Form Template Method / single world step — `fidelity_world_step.h` → `simulateFidelityWorld`
3. Replace Magic Number — constants SSOT + policy
4. Move Field — `BotConfig` → `src/cg/bot_config.h`
5. Extract Class (modular review units) — `internal/ga_*.inc`
6. Separate delivery — CG amalgam workflow (not physics gate)

## Remaining (non-blocking / optional further catalog)

| Item | Note |
|---|---|
| Further split of `ga_prelude_and_search.inc` into eval vs evolve TUs | Optional; would need non-static linkage |
| Standalone `BotConfig` mirror under `CG_STANDALONE` | Must stay value-synced with `bot_config.h` (amalgam constraint) |
| Full polymorphism rewrite of GA eval | Out of scope (Fowler catalog infinite) |

## Verify

```bash
python3 sim/check_ssot_policy.py
python3 sim/check_verification_policy.py
bazel test --config=ci //src/physics:test_physics //src/cg:amalgam_fast_smoke_test
python3 sim/validate_fast_physics_corpus.py --limit 100 --leaderboard-sample 0
./tools/export_cg_submission.sh
```
