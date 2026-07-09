# Top-3 SSOT refactors + automated CG submission workflow

**Repo:** mad_pod_arena  
**Kind:** analysis / recommendation (not implementation)  
**Anchors verified against tree:** 2026-07-07  

Out of scope for this note: implementing the refactors; Fidelity math changes; GATE_* changes; golden long-tail fidelity; full `cg_bot.cpp` modularisation; binary battle cache; leaderboard wall-clock optimisations.

---

## Live multi-owner hazards (today)

| Hazard | What exists now |
|---|---|
| **Fidelity dual-body** | Rules in `src/physics/physics.h` (`csb::Game`) **and** a parallel Fidelity-equal body in `src/physics/fast_physics.h` (`csb::fast_physics::Game`). SSOT table names both: `docs/SSOT.md` ownership rows for Fidelity world + Fidelity-equal rollout. |
| **Constants / agent-doc drift** | Claimed owner `src/core/constants.h` (`docs/SSOT.md`), but `physics.h` / `fast_physics.h` re-declare `kFriction` etc., and `fast.h` still uses raw `0.85`. `GEMINI.md` still documents deleted `PhysicsSimulator` / `GAPhysicsSimulator` and “transitional” engine physics. |
| **Bot / search delivery fork** | Library path: `//src/cg:ga_bot` + `ga_bot.h` `CreateGABot`. Standalone: `-DCG_STANDALONE` in `cg_bot.cpp` with inline Pod/RNG. Paste path: genrule `//src/cg:cg_bot_amalgam` cats `fast.h` + `cg_bot.cpp`. Collision fragment is intended to be only `csb::fast` (`src/physics/fast.h`), but delivery modes and stale docs make a second owner easy to reintroduce. |

Gate merge job remains separate: `physics-accuracy` / gate A+B per `docs/VERIFICATION_TRUTH_POLICY.md` — **not** the CG paste workflow.

---

## Top 3 prioritised SSOT refactors

Exactly three. Primary purpose of each: **one durable owner per concern**.

### Refactor 1 — Single Fidelity rules body (eliminate dual physics implementation)

| | |
|---|---|
| **Rank** | 1 (highest structural SSOT risk) |
| **Concern owned** | Full CG-faithful world step (rotate/thrust/collide/CP/timeout/commit) |
| **Problem removed** | Today a fidelity fix can land in `physics.h` (gate/`replay_driver`/arena) while `fast_physics.h` silently diverges—or the reverse. EXACT suites paper over this only when someone runs them. |
| **What to do** | Extract one implementation core (free functions or internal `.cpp`/shared header used by **both** `csb::Game` and any exact-rollout façade). `fast_physics` becomes a thin API/layout wrapper (or is retired as a second rules engine) that **calls the core**, not a line-copy of simulate/bounce/CP. |
| **Observable success** | (a) Grep shows no second full `simulateWorld`/`newCollide`/`applyRotate` family outside the core. (b) `validate_fast_physics` / EXACT corpus still 100% vs Fidelity after the extract. (c) Changing friction/CP rule is **one edit** that both gate driver and exact-rollout paths pick up. |
| **Anti-drift (stays SSOT)** | CI job (PR or required smoke): build + EXACT compare Fidelity vs rollout API on `test_session` + golden pass. Optional static check: forbid new `namespace csb { ... simulate` forks outside `physics/` core paths. SSOT table lists **one** “Fidelity rules implementation” row pointing at the core. |

**In-repo anchors:** `docs/SSOT.md` (Fidelity + fast_physics rows); `src/physics/physics.h`; `src/physics/fast_physics.h`; tools `//src/physics:validate_fast_physics_battles`, `sim/validate_fast_physics_corpus.py`.

---

### Refactor 2 — Constants + ownership text SSOT (numeric law and agent truth in one place)

| | |
|---|---|
| **Rank** | 2 |
| **Concern owned** | Numeric game constants **and** human/agent ownership map |
| **Problem removed** | `src/core/constants.h` is the *claimed* SSOT, but physics headers re-define the same symbols and `fast.h` hard-codes `0.85` / collision thresholds. `GEMINI.md` still trains agents on deleted simulators and engine-owned bot physics—directly undoing SSOT phase work. |
| **What to do** | (1) All physics headers **only alias** `csb_constants::*` (no second `kFriction = 0.85`, no raw magic in `fast.h`). (2) Rewrite `GEMINI.md` to a thin pointer: ownership = `docs/SSOT.md`; build/test/gate commands only; ban resurrecting `PhysicsSimulator` names. (3) Optional: extend `sim/check_verification_policy.py` (or a sibling `check_ssot_policy.py`) to fail if forbidden strings reappear under `GEMINI.md` / if raw `0.85` appears outside `constants.h`. |
| **Observable success** | (a) `rg 'kFriction\\s*=' src` only hits `constants.h` (aliases use `csb_constants::kFriction`). (b) `GEMINI.md` matches SSOT ownership table (no GAPhysicsSimulator). (c) Policy checker green in CI. |
| **Anti-drift** | Automated grep/policy job on every PR; SSOT register update required when ownership changes (already the living rule in `docs/SSOT.md`). |

**In-repo anchors:** `src/core/constants.h`; `src/physics/physics.h` / `fast_physics.h` / `fast.h` duplicate constants; `docs/SSOT.md` Constants row; `GEMINI.md` stale §1b/§2; `sim/check_verification_policy.py` pattern for static enforcement.

---

### Refactor 3 — Bot delivery + search-collision SSOT (one fragment, one public API, generated paste only)

| | |
|---|---|
| **Rank** | 3 |
| **Concern owned** | GA search collision fragment **and** how the bot is built/shipped for CG |
| **Problem removed** | Collision for search is *supposed* to be only `csb::fast::SimulateTurn` in `src/physics/fast.h`, via `FastSimulateTurn` in engine/standalone. But `cg_bot.cpp` still carries a large `CG_STANDALONE` fork of types/RNG/actions; amalgam is a genrule that only some people know; nothing in the **merge gate** proves the paste artifact is the unique submission path. A future edit can re-inline collision into the bot TU. |
| **What to do** | Lock the contract: (1) **Only** `src/physics/fast.h` may implement search collision. (2) **Only** `ga_bot.h` / `CreateGABot` is the library API for arena/tournament. (3) **Only** generated amalgam (or a script that always rebuilds it) is the CG paste body—no hand-maintained second file. (4) Smoke test that amalgam contains a single `csb::fast` collision path and builds. (5) Document that approx-search is intentional; exact search (if ever) is an optional flag, not a second default owner. |
| **Observable success** | (a) `bazel build //src/cg:cg_bot_amalgam //src/cg:cg_bot_amalgam_bin //src/cg:ga_bot` green. (b) Amalgam smoke / layout tests pass (`amalgam_fast_smoke_test` and/or stronger “no second SimulateTurn body” check). (c) README/SSOT: “CG paste = amalgam only.” (d) Tournament links `ga_bot`, not a private copy of physics. |
| **Anti-drift** | Dedicated **CG submission workflow** (below) always rebuilds amalgam from `cg_bot.cpp` + `fast.h`. CI fails if amalgam target breaks. Grep policy: no new `GetCollisionTime` / full collision loop under `src/cg/` outside includes of `fast.h`. |

**In-repo anchors:** `src/physics/fast.h`; `src/cg/cg_bot.cpp` (`CG_STANDALONE`, `FastSimulateTurn`); `src/cg/ga_bot.h`; `src/cg/BUILD.bazel` (`cg_bot_amalgam`, `ga_bot`, `amalgam_fast_smoke_test`); `src/engine/engine.h` layout bridge; `docs/SSOT.md` phase 3–4 / Fast fragment row.

---

## Separate automated CodinGame submission workflow

**Distinct from:** job `physics-accuracy`, gate A (`verify_battles.py --gate`), gate B (golden `--tier pass`), retention, and ad-hoc “open `cg_bot.cpp` and paste.”

### Purpose
Produce a **single pasteable C++ file** for the CodinGame IDE from **shared sources only**, with automated pass/fail.

### Inputs
| Input | Path / target |
|---|---|
| Bot sources | `src/cg/cg_bot.cpp` |
| Search collision SSOT | `src/physics/fast.h` (via genrule) |
| Public API (library consumers) | `src/cg/ga_bot.h` — not the paste body |
| Build | Bazel `//src/cg:cg_bot_amalgam` (and optionally `//src/cg:cg_bot_amalgam_bin` compile smoke) |

### Entry points (pick one primary; document both)

**Local (required developer path):**
```bash
# From repo root — one command
bazel build //src/cg:cg_bot_amalgam //src/cg:cg_bot_amalgam_bin //src/cg:amalgam_fast_smoke_test
# Paste body (path may be under bazel-bin):
#   bazel-bin/src/cg/cg_bot_amalgam.cpp
# Convenience (recommended to add as tools/export_cg_submission.sh):
#   copies amalgam to dist/cg_submission.cpp and prints path + byte size
```

**CI (separate workflow/job — not the physics gate):**
```yaml
# Suggested: .github/workflows/cg-submission.yml
# on: workflow_dispatch, push to main/physics/** affecting src/cg or src/physics/fast.h
jobs:
  cg-submission-artifact:
    # 1) bazel build //src/cg:cg_bot_amalgam //src/cg:cg_bot_amalgam_bin
    # 2) bazel test //src/cg:amalgam_fast_smoke_test
    # 3) upload-artifact: cg_bot_amalgam.cpp
    # 4) fail if amalgam missing, empty, or missing markers:
    #      "GENERATED", "CG_STANDALONE", "csb::fast" / SimulateTurn from fast.h
```

Do **not** fold this into `physics-accuracy` (U)∧(A)∧(B). Submission can be green while you work on Fidelity long-tail, and vice versa—different products.

### Pass / fail checks
| Check | Pass means |
|---|---|
| `bazel build //src/cg:cg_bot_amalgam` | Genrule output exists |
| `bazel build //src/cg:cg_bot_amalgam_bin` | Amalgam **compiles** as a single TU (usable paste shape) |
| `bazel test //src/cg:amalgam_fast_smoke_test` | Artifact present in runfiles / non-trivial content |
| Size / markers | File non-empty; header says GENERATED; defines `CG_STANDALONE`; includes collision from `fast.h` (not a second hand-written collision block) |
| Optional artifact upload | Humans download one file for CG IDE paste |

### Guarantees relative to SSOT refactor 3
- Paste is always **regenerated** from `cg_bot.cpp` + `fast.h` — never a second maintained submission file in git.  
- Search collision owner remains `csb::fast` in `fast.h`.  
- Library/tournament continues via `CreateGABot` / `ga_bot` without forking collision math.

### Explicit non-goals of this workflow
- Running gate A/B or leaderboard exact.  
- Producing a multi-file CMake project for CG (CG wants one paste).  
- Replacing approx search with Fidelity inside the amalgam (unless a future optional flag is SSOT-documented).

---

## How the three + workflow lock “always one SSOT”

```text
Numeric law          →  constants.h only          (Refactor 2)
Fidelity world rules →  one physics core          (Refactor 1)
GA collision search  →  fast.h only               (Refactor 3)
CG paste body        →  generated amalgam only    (CG workflow)
Agent ownership map  →  docs/SSOT.md (+ fixed GEMINI) (Refactor 2)
Merge physics truth  →  physics-accuracy gate     (unchanged; separate)
```

## Later work (not top-3 SSOT)
- Full modular split of `cg_bot.cpp` beyond delivery SSOT.  
- Binary battle cache / full-LB wall clock.  
- Optional `CSB_SEARCH_EXACT` flag.  
- Golden long-tail Fidelity PRs.

