# `src/` — first-party C++ code

| Package | Role | Modify? |
|---|---|---|
| [`cg/`](cg/) | GA bot (CodinGame submission) | Yes — bot logic; keep `CG_STANDALONE` engine block in sync |
| [`engine/`](engine/) | Bot/arena physics + maps + IBot | Yes — single source of truth for bot simulation |
| [`physics/`](physics/) | Referee-faithful physics + verifiers | Yes — single source of truth for CG server fidelity |
| [`tournament/`](tournament/) | Benchmark harness | Yes |

Build with Bazel from the repo root (`bazel build //src/...`).
