# `src/` — first-party C++ code

| Package | Role | Owns (SSOT) |
|---|---|---|
| [`core/`](core/) | Shared law + data | `constants.h`, `maps/catalog.h` (18 maps), `progress.h` |
| [`physics/`](physics/) | Physics products | World step `fidelity_world_step.h`; math `fidelity_math.h`; façades `physics.h` / `fast_physics.h`; GA fragment `fast.h` |
| [`engine/`](engine/) | Arena + degrees pods | `IBot`, degrees `Pod` / apply actions, arena — **no** collision/CP geometry |
| [`cg/`](cg/) | GA bot product | `bot_config.h`, search/eval in `internal/ga_*.inc`; paste via amalgam only |
| [`tournament/`](tournament/) | Benchmarks | Links `//src/cg:ga_bot` (not a physics owner) |

Normative register: [`../docs/SSOT.md`](../docs/SSOT.md). Policy: `python3 sim/check_ssot_policy.py`. Behavioral truth: `./tools/run_truth_suite.sh`.
