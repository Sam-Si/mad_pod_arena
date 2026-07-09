# SSOT Implementer Runbook — definitive (v2)

| Field | Value |
|---|---|
| **Status** | **Normative. No implementation in this document.** |
| **Date** | 2026-06-28 |
| **Audience** | Human or agent executing Phases 0–8 without inventing decisions |
| **Code examined for this version** | `src/physics/physics.h` (incl. Fast stub L719–725), `src/engine/engine.{h,cpp}` (`GAPhysicsSimulator` L294–412, `g_friendly_collision` L7), `src/engine/arena.cpp` (`PlayGame` on `csb::Game`), `src/core/progress.h`, `src/core/constants.h`, `src/cg/cg_bot.cpp` (`CG_STANDALONE` fork + `GAPhysicsSimulator::SimulateTurn` call sites), `src/tournament/cg_bot_wrapper.h`, `src/physics/test_physics.cpp`, `sim/tolerance_policy.py` / verification policy via docs, `GEMINI.md` dual-SSoT teaching |

**This file is the only sequencing + Phase 1–4 contract.** Annexes may be wrong where they conflict; update annexes in the same PR that lands a phase.

---

## A. Document authority (no second roadmap)

| Topic | Wins |
|---|---|
| Phase order, Fast API, goldens, deletion, acceptance bands, L-promotion | **This runbook** |
| `GATE_*` values, job id `physics-accuracy`, gate roles, driver rules | [`VERIFICATION_TRUTH_POLICY.md`](VERIFICATION_TRUTH_POLICY.md) + `sim/check_verification_policy.py` |
| Living phase status + full OQ table after each phase PR | [`SSOT.md`](SSOT.md) — **must be updated every phase PR** |
| Architecture narrative / extra inventory | [`SSOT_REFACTOR_DESIGN.md`](SSOT_REFACTOR_DESIGN.md), [`SSOT_REFACTOR_IMPLEMENTATION_PLAN.md`](SSOT_REFACTOR_IMPLEMENTATION_PLAN.md) as **annexes only** |
| Fidelity long-tail narrative (golden ~10 fails) | [`PHYSICS_SUPER_ACCURACY.md`](PHYSICS_SUPER_ACCURACY.md) — **orthogonal** to SSOT phases |
| Conflict on sequencing or Phase-1 API | **Runbook wins**; annex text that still says `Game::step(Fast)` / radians Fast / “pick in PR description” is **void** |

**Void annex claims (do not follow):**

- Fast authoritative entry = `Game::step(Fast)` or `simulateWorldFast` on `csb::Pod` radians buffers.
- “Pick radians vs degrees Fast pods in PR description.”
- Arena still on `PhysicsSimulator` (as-built is already `csb::Game`).
- Fast = Fidelity with mass-factor toggle.

---

## B. North star (one sentence)

**One authoritative editable owner per concern:** Fidelity world math and Fast search-collision math both live under the physics package; bots/arena/submit/gate **consume** them; delete every other collision owner; never trade the merge gate.

### B.1 Principles with judgment (not theater)

| Principle | Means here |
|---|---|
| SSOT | One editable artifact per concern (table §C) |
| DRY | One Fast body, one Fidelity body; adapters convert views only |
| YAGNI | No Fast≡Fidelity; no Elo climbing in this program; no >8-file bot split unless needed for amalgam |
| Testability | L0–L7 ladder (§N); every deletion has a green predecessor |
| SOLID | Split **ownership** (physics vs search vs I/O), not interface soup |

### B.2 Non-negotiables (never violate)

1. Do not rename CI job id **`physics-accuracy`** without policy + `check_verification_policy.py` co-PR.
2. Do not change **`GATE_*` = (pos 5.0, vel 3.0, ang 1.0°, timeout 1)** as a refactor side effect.
3. Do not change Fidelity `simulateWorld` / `bounce` / `applyMove` / rotate / CP / friction **in the same PR** as Fast plumbing (Phase 1).
4. Every PR: **`PR_MERGE_OK`** = battle-retention ∧ (`bazel build //...` and/or CI `build-and-test`) ∧ physics-accuracy compound gate **(U)∧(A)∧(B)**.
5. `third_party/referees/**` is read-only.
6. Do not run GA search on Fidelity “to simplify” unless a human explicitly accepts that product change (default: **out of scope**).
7. Do not promote C++ `verify_battles` 0.01/0.001rad tolerances to the merge gate.
8. Do not invent answers for **OQ1/4/5/6/7** in code — only log in `SSOT.md` when an owner decides (table §O).
9. Do not loosen Fast golden **exact `==`** without an explicit runbook revision PR.

### B.3 Universal PR checklist (paste into every PR body)

```text
[ ] bazel build //src/physics:replay_driver //src/physics:test_physics
[ ] cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver
[ ] bazel test //src/physics:test_physics
[ ] MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
    # expect: 312/312, role=GATE
[ ] MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
    # expect: 188/188 ok
[ ] python3 sim/check_verification_policy.py
[ ] bazel build //...
[ ] docs/SSOT.md phase/OQ status updated
```

Compound gate definition (policy-owned, copied for implementer convenience):

```text
(U)  bazel test //src/physics:test_physics
(A)  MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
(B)  MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
```

Copy `replay_driver` before (A)/(B).

---

## C. Ownership table (end-state SSOT)

| Concern | Authoritative editable artifact | Non-authoritative consumers |
|---|---|---|
| Fidelity kinematics, CP, timeouts, won, overlap sep, snapNearInteger | `src/physics/physics.h` — `csb::Game`, profile **Fidelity only** on `Game` | `replay_driver`, Python gate, `arena.cpp` |
| Fast collision + integrate + EndTurn fragment | `csb::fast` in physics package (`physics.h` and/or `src/physics/fast.h` included by it) — **`SimulateTurn` on degrees pods** | GA search, amalgam submit |
| Rotate/thrust for **search** genes | Caller: `Pod::ApplyGAAction` (engine / bot) — **not** moved in Phase 1 | GA eval loops |
| Rotate/thrust for **referee** | `csb::Pod::applyMove` on `Game` | arena via `applyAction` |
| Constants (radii, friction, rotate, timeouts, …) | `src/core/constants.h` | physics may alias; no second magic numbers |
| Tournament maps (18 CG-captured) | `src/core/maps/catalog.h` | arena, tests |
| Progress index math (lap/local/global) | `src/core/progress.h` (`csb_progress::{GlobalNext,Decode,LocalNext}`) | `SyncViewFromGame` only |
| Terminal win/timeout/max-turns **predicates** | **`csb::Game` fields** (`won`, `playerTimeout`, `turn` / `kMaxGameTurns`) | arena `PlayGame` — **not** a separate `RaceProgress::HasWon` type (design’s richer struct is **deferred**; not required) |
| Gate tolerances / roles / job id | `sim/tolerance_policy.py` + verification policy | all verifiers |
| Bot search / eval / weights | `src/cg/cg_bot.cpp` today → modular `src/cg` / `src/bot` later — **no collision math after Phase 3** | tournament, CG I/O |
| CG paste file | **Generated amalgam only** (Phase 4) | CodinGame IDE |
| Go/C referees | `third_party/**` read-only | sketches only |

---

## D. As-built ground truth (verified against code)

### D.1 Done (do not redo)

| Item | Evidence |
|---|---|
| Experimental third physics deleted | No `csb_physics.h` |
| Constants / maps catalog / progress helpers | `src/core/constants.h`, `src/core/maps/catalog.h`, `src/core/progress.h` |
| Arena world owner | `arena.cpp` `PlayGame`: `csb::Game`, `game.step(fidelity)`, terminals from `won` / `playerTimeout` / `kMaxGameTurns` (**500**) |
| Degrees IBot view | `SyncViewFromGame`; full **4-pod** vector to **both** bots |
| `PhysicsProfile` / `StepOptions` / `Game::step` | `physics.h` |
| Gate A | **312/312** turn-perfect under GATE on `test_session_battles` |
| Gate B | **188/188** golden **pass tier** |

### D.2 Not done / explicit debt

| Item | Evidence |
|---|---|
| Fast is a lie on `Game` | `Game::step` ignores `opt` and always `nextTurn()` (`physics.h` ~719–725) |
| Fast body location | `GAPhysicsSimulator` in `engine.cpp` ~294–412 |
| Standalone Fast twin | `cg_bot.cpp` `#ifdef CG_STANDALONE`: class named **`PhysicsSimulator`** with Fast body; `using GAPhysicsSimulator = PhysicsSimulator` |
| Friendly flag dual def by build mode | `thread_local bool g_friendly_collision` defined in `engine.cpp` L7 (shared) **and** in `cg_bot.cpp` under `CG_STANDALONE` ~L346 |
| GA call sites (shared) | `GAPhysicsSimulator::SimulateTurn` in `cg_bot.cpp` (e.g. ~797, ~841, ~962, ~2785) after `ApplyGAAction` |
| Teaching residue | `GEMINI.md` still documents **two physics SSoTs** (labeled transitional) |

### D.3 “312/312” vs “~10 residual fails” (no contradiction)

| Phrase | Meaning | Blocks Phase 1? |
|---|---|---|
| Gate A **312/312** | Merge-blocking `test_session_battles` under GATE | Must **stay** green always |
| **~10 residual fails** | `golden_physics_battles` **all 200** under GATE (~190/200 turn-perfect on tip) | **No** |
| Fidelity fixes for those ~10 | Separate PRs, Fidelity code only, L1 held | Never same PR as Fast port |

GATE tolerances (policy): pos≤5, vel≤3, ang≤1°, timeout≤1, exact `next_cp` (mod track). **Goldens for Fast use exact `==`, not GATE_*.**

### D.4 Temporary dual ownership (allowed debt, not failure)

| After | Allowed | Forbidden |
|---|---|---|
| Phase 1 lands | `csb::fast` exists **and** engine/cg_bot still call `GAPhysicsSimulator` | Claiming “GA uses Fast SSOT” in teaching docs |
| Phase 3 lands | `CG_STANDALONE` may still embed a Fast **copy** until Phase 4 | Second **different** Fast algorithm |
| Phase 4 lands | No competing Fast/Fidelity owners for runtime | Hand-edited submit physics |

---

## E. Semantic matrix (normative — Fast is Alt E)

Fast **`SimulateTurn` fragment only** (matches `engine.cpp` `GAPhysicsSimulator` L294–412).

| Dimension | Fidelity (`csb::Game`) | Fast (`csb::fast` / today’s `GAPhysicsSimulator`) |
|---|---|---|
| Impulse construction | Unit normal; `force = n·relv/(m1+m2)`; then `force += 120` or `force += force`; vel `+= impulse * m` **once**; shield inv-mass **0.1** | `mcoeff=(m1+m2)/(m1*m2)`; `fx,fy` from `(n*(n·dv))/(\|n\|²*mcoeff)`; apply **twice** (before and after min-120 rescale of fx/fy); `Mass()` **10** if `shield_cd==4` else **1** |
| Overlap separation | Yes when `dd <= 800` (+ ε) | **No** |
| `snapNearInteger` | Yes on selected Fidelity paths | **No** |
| Collision time | Rel-vel disc; sentinel **10.0** = no hit this turn | Quadratic `a,b,c`; sentinel **-1.0**; early-outs `c > 3360000`, `c>=0 && b>=0`; `a < 0.00001` → −1 |
| Pair loop | Nested indices | **Unrolled 6 pairs**; `col_count < 10`; if `first_col_t < 0.0001` then `first_col_t = 0.0001` |
| EndTurn | Fidelity `trunc(v*0.85)`, `floor(p+0.5)`, shieldtimer-- | Fast: `trunc(v*0.85)`, `Round` = **`floor(x+0.5)`** (same formula as `engine.cpp` `Round`), `shield_cd--` if >0 |
| CP / timeout / won | Inside Fidelity world step | **Not in Fast** — caller-owned in GA (`next_cp_id` / `laps_completed`; disk `dist² <= 360000` checks in bot **after** SimulateTurn) |
| Rotate / thrust | `applyMove` on Game | **Caller** `ApplyGAAction` (or equivalent) **before** each `SimulateTurn` |
| `g_friendly_collision` | Not set by Fidelity bounce | **Must set `true`** when resolving teammate pairs **(id 0 with 1) or (id 2 with 3)** — same condition as `engine.cpp` L322–324 |

**Phase 1 requires Fast ≡ GA only, not Fast ≡ Fidelity EndTurn bit-identity.**

---

## F. Locked API (Phases 1–3) — no alternatives

### F.1 Authoritative Fast entrypoint (GA + goldens)

```text
// Lives in physics package (physics.h and/or fast.h included by physics.h).
// Pods are DEGREES / pos / vel — engine-isomorphic. NOT csb::Pod radians.

namespace csb {
namespace fast {

struct Pod {
  int id;
  int team;
  // Position / velocity: use double x,y fields named to match engine access pattern.
  // LOCKED layout target: identical field set to engine::Pod for trivial assign:
  //   id, team, pos.x, pos.y, vel.x, vel.y, angle (degrees),
  //   next_cp_id, boost_available, shield_cd, timeout, laps_completed
  // Implementation may embed Vec2-equivalent {double x,y} as pos, vel.
  double Mass() const;           // shield_cd==4 ? 10.0 : 1.0
  void Move(double t);           // pos += vel * t
  void EndTurn();                // Round pos; trunc vel*0.85; if shield_cd>0 shield_cd--
};

double GetCollisionTime(const Pod& a, const Pod& b);
void ResolveCollision(Pod& a, Pod& b);   // sets ::g_friendly_collision on teammate pairs
void SimulateTurn(Pod* pods);            // pods points to array of length 4

}  // namespace fast
}  // namespace csb
```

**Type bridging (locked):**

- Phase 1 goldens and tests use **`csb::fast::Pod[4]`** filled from captured JSON.
- Phase 3: shared `cg_bot` uses **`::Pod` from `engine.h`** (degrees). Call shape locked as either:
  - **(Required approach)** `csb::fast::SimulateTurn(reinterpret_cast<csb::fast::Pod*>(g_sim))` **only if** `sizeof` and field layout are **asserted equal** in a static test, **or**
  - Field-wise copy `::Pod` → `csb::fast::Pod` → SimulateTurn → copy back (always correct; use if layout assert fails).
- Prefer making `csb::fast::Pod` a **duplicate of the field list** of `engine.h` `Pod` so copies are trivial; do **not** make Fast operate on `csb::Pod` radians.

### F.2 Friendly-collision symbol (locked spelling — one global)

| Rule | Value |
|---|---|
| **Symbol name** | **`g_friendly_collision`** (global namespace) |
| **Type** | `thread_local bool` |
| **Declaration today** | `extern thread_local bool g_friendly_collision;` in `engine.h` L10 |
| **Definition today (shared)** | `engine.cpp` L7 |
| **Definition today (standalone)** | `cg_bot.cpp` under `CG_STANDALONE` ~L346 — **debt until Phase 4** |
| **Phase 1 Fast code** | `ResolveCollision` sets **`g_friendly_collision = true`** on teammate pairs (same if as GA). Physics Fast TU must **declare** `extern thread_local bool g_friendly_collision;` (do **not** define in a header). Link against the existing definition in `engine.cpp` for tests that link engine; for `//src/physics:test_physics` that only links physics, **Phase 1 must provide exactly one definition** in a physics `.cpp` **or** define in `test_physics.cpp` for tests — **locked choice:** add **`src/physics/fast_globals.cpp`** with `thread_local bool g_friendly_collision = false;` and put it in `//src/physics:physics` as a **src** (physics library becomes hdrs+that one cpp) **OR** keep physics header-only and define the symbol in `test_physics.cpp` for tests only while Fast in production links engine’s definition. |

**Locked choice for Phase 1 production symbol:**

1. Keep **single production definition** in `engine.cpp` for shared builds (unchanged).
2. In `csb::fast` header code used from physics tests: declare `extern thread_local bool g_friendly_collision`.
3. **`//src/physics:test_physics`** must link a TU that **defines** `g_friendly_collision` — add file **`src/physics/fast_test_support.cpp`** (or define at bottom of `test_physics.cpp`):

   `thread_local bool g_friendly_collision = false;`

4. Do **not** introduce `csb::fast::g_friendly_collision` as a second variable.
5. GA code continues to read/write **`g_friendly_collision`** unqualified (as today).

### F.3 `Game::step` / Fast profile (locked mechanism — not “preferred or”)

**Locked behavior after Phase 1:**

```text
void Game::step(const StepOptions& opt) {
  if (opt.profile == PhysicsProfile::Fast) {
    // UNSUPPORTED on Game. Fast search uses csb::fast::SimulateTurn(degrees pods).
    // Must NOT call nextTurn() / simulateWorld() (that was the pre-Phase-1 lie).
#ifndef NDEBUG
    assert(false && "Game::step(Fast) unsupported; call csb::fast::SimulateTurn");
#endif
    return;  // leave Game state and pendingMoves unchanged
  }
  nextTurn();  // Fidelity only
}
```

| Rule | Value |
|---|---|
| GA entrypoint | **`csb::fast::SimulateTurn` only** |
| Goldens | **Never** call `Game::step(Fast)` |
| Arena | Always `StepOptions{Fidelity}` or `nextTurn()` / `step` default Fidelity |
| Debug builds | `assert` on mistaken `step(Fast)` |
| Release builds | No-op return (no Fidelity under Fast label) |

Comments on `PhysicsProfile::Fast` must state: **profile enum reserved / unsupported on Game; Fast lives in `csb::fast`.**

### F.4 What “done Fast” includes (exit criteria for the port body)

All of the following, nothing more:

1. `GetCollisionTime` (GA algebra + −1 sentinels + early-outs)
2. `ResolveCollision` (mcoeff, double apply, Mass 10, no overlap sep, sets `g_friendly_collision` on teammate pairs)
3. Substep integrate via `Move`
4. Loop structure: `t_current`, `col_count < 10`, unrolled 6 pairs, `first_col_t` floor 0.0001
5. `EndTurn` on all 4 pods after collisions
6. **Does not** implement rotate/thrust
7. **Does not** implement CP / team timeout / `won`
8. **Does not** use Fidelity `bounce` / `newCollide` / `snapNearInteger`

---

## G. Phase map ↔ legacy PR numbers

| Phase | Legacy PR(s) | Deletes GA? | Notes |
|---|---|---|---|
| **0** | PR-0 | No | Teaching: one SSOT + profiles |
| **1** | **PR-3** | **No** | Fast port + goldens; Fidelity frozen |
| **2** | PR-4 remainder / PR-12 | **No** | OQ2 **harness + CI**; arena already on Game |
| **3** | **PR-5 / PR-6** | **Yes — GAPhysicsSimulator** | Switch GA; delete engine GA (+ PhysicsSimulator if unused) |
| **4** | PR-7 / PR-9 | Standalone Fast copy | Amalgam; kill include-cpp + CG_STANDALONE body |
| **5** | PR-10 slice | — | Engine has zero collision math |
| **6** | leftovers | — | Constants/maps grep hygiene |
| **7** | PR-11 / promote PR-12 | — | L4/L5 promotion, docs |
| **8** | ongoing | — | No new forks (`cg_rust` watch) |

**Critical path:** `0 → 1 → 3 → 4`. Phase 2 may run **in parallel anytime after** arena-on-Game (already true), including parallel with late Phase 1. **Phase 2 never deletes GA.**

---

## H. Phase 0 — Teaching cleanup

### H.1 Goal

Docs teach **one** physics package SSOT (Fidelity + `csb::fast`), not “two physics SSoTs” as permanent law.

### H.2 Steps

1. Edit `GEMINI.md`, `README.md`, `src/README.md` so bot search physics is described as **`csb::fast` (target) / transitional engine GA (until Phase 3)**, not a permanent second SSOT.
2. Point implementers to **this runbook** + `SSOT.md`.
3. Ensure `SSOT.md` contains **full OQ table** (§O).

### H.3 Acceptance

```text
[ ] No doc claims permanent dual physics SSOT without “transitional until Phase 3/4”
[ ] SSOT.md lists OQ1–OQ8 with status
[ ] PR_MERGE_OK (docs-only PR still runs checklist if CI requires)
```

### H.4 Rollback

Revert doc PR.

---

## I. Phase 1 — Fast port + goldens (critical path)

### I.1 Goal

`csb::fast::SimulateTurn` matches **unmodified** `GAPhysicsSimulator::SimulateTurn` under **exact equality** goldens. Fidelity untouched. Engine GA **still used** by bots.

### I.2 Order of operations (do not reorder)

```text
1. Start from green tip; run universal checklist; save outputs in PR notes
2. CAPTURE goldens from unmodified GAPhysicsSimulator FIRST (§I.4)
3. Commit goldens (same PR as port is OK only if capture commit is first in the PR / clearly from pre-port tree)
4. Implement csb::fast line-faithful to engine.cpp L294–412
5. Wire g_friendly_collision per §F.2
6. Add tests loading goldens; exact ==
7. Implement Game::step Fast behavior per §F.3 (stop the lie)
8. Run universal checklist + Fast tests
9. Update SSOT.md Phase 1 = done; dual ownership note
```

### I.3 Files

| Action | Path |
|---|---|
| Add/edit | `src/physics/physics.h` and/or `src/physics/fast.h` (if split, physics.h includes fast.h) |
| Edit | `src/physics/BUILD.bazel` — export new hdr; ensure tests link |
| Edit | `src/physics/test_physics.cpp` — golden loader + cases; may define `g_friendly_collision` for test link |
| Create | `src/physics/testdata/fast_goldens.json` |
| Optional capture tool | `scripts/capture_fast_goldens.cpp` or Python+pybind **or** one-off binary linking `//src/engine:engine` — **delete or keep** after capture; not part of merge gate |
| **Do not edit** | Fidelity methods (`simulateWorld`, `bounce`, `applyMove`, `newCollide`, CP) except comments |
| **Do not delete** | `GAPhysicsSimulator`, `PhysicsSimulator`, `CG_STANDALONE` blocks |

### I.4 Golden contract (normative — no “N scenarios” handwaving)

| Parameter | Locked value |
|---|---|
| Path | `src/physics/testdata/fast_goldens.json` |
| Source | **`GAPhysicsSimulator::SimulateTurn`** in `src/engine/engine.cpp` on **unmodified** tree |
| Format version field | `"version": 1` |
| Scenario count **N** | **≥ 32** |
| K mix | **≥ 8** scenarios with **K=1**; **≥ 12** with **K=5**; **≥ 12** with **K=20** |
| Pods per scenario | **4** |
| PRNG for synthesis | `seed = 0xC5BFA57u + (uint32_t)scenario_id` using engine `SeedRand` / `FastRand` **or** an explicit documented LCG in the capture tool — must be **reproducible**; record `seed` per scenario in JSON |
| Required scenario classes | (a) general random in map bounds; (b) **≥ 4** clustered (all pods within 800 units, opposing velocities) to force collisions; (c) **≥ 2** with some `shield_cd == 4`; (d) **≥ 2** with `angle < 0` (uninit degrees −1) |
| Input fields per pod | `id, team, pos.x, pos.y, vel.x, vel.y, angle, shield_cd, boost_available, next_cp_id, laps_completed, timeout` |
| Output fields compared | `pos.x, pos.y, vel.x, vel.y, angle, shield_cd` × 4 pods after **K** turns |
| Unchanged asserts | After K turns: `next_cp_id`, `laps_completed`, `boost_available`, `id`, `team` **equal** to input (Fast must not touch them) |
| Friendly flag | For each scenario, store `friendly_after` = value of `g_friendly_collision` after last SimulateTurn; start each turn with `g_friendly_collision = false` before SimulateTurn (same as GA eval sites that clear it) |
| Tolerance | **Exact `==`** for all floating and integer fields (no epsilon). Not GATE_*. Not “near-bit.” |
| Capture loop per scenario | Set pods → for t in 1..K: `g_friendly_collision=false`; `GAPhysicsSimulator::SimulateTurn(pods)` → record pods + friendly flag |

**JSON shape (normative keys):**

```json
{
  "version": 1,
  "source": "GAPhysicsSimulator",
  "source_ref": "src/engine/engine.cpp",
  "scenarios": [
    {
      "id": 0,
      "seed": 3312349031,
      "k": 5,
      "friendly_after": false,
      "pods_in": [
        {
          "id": 0, "team": 0,
          "pos": [1000.0, 2000.0], "vel": [10.0, -20.0],
          "angle": 45.0, "shield_cd": 0, "boost_available": true,
          "next_cp_id": 1, "laps_completed": 0, "timeout": 0
        }
      ],
      "pods_out": [ ... four pods ... ]
    }
  ]
}
```

(`pods_in` / `pods_out` length 4.)

### I.5 Test requirements

- `//src/physics:test_physics` reads `fast_goldens.json` (Bazel `data = ["testdata/fast_goldens.json"]` on the test target).
- For each scenario: build `csb::fast::Pod[4]` from `pods_in` → run K× `SimulateTurn` with friendly cleared each turn → **assert `==`** on all compared fields and `friendly_after`.
- Add **one** unit test: `Game` with pending moves + `step({Fast})` does **not** change pod positions (no-op path §F.3) in release; in debug may abort — test only the Fidelity path for motion.

### I.6 Acceptance (Phase 1 exit)

```text
[ ] fast_goldens.json committed; N≥32; K mix satisfied; source is pre-port GA
[ ] csb::fast::{GetCollisionTime,ResolveCollision,SimulateTurn} exist in physics package
[ ] All golden scenarios exact-match
[ ] Game::step(Fast) does not call nextTurn()/simulateWorld (code review + test)
[ ] Fidelity methods unchanged (diff empty of logic)
[ ] GAPhysicsSimulator still present; cg_bot still calls it
[ ] Gate A 312/312; Gate B 188/188; policy checker green; bazel build //...
[ ] SSOT.md updated
```

### I.7 Rollback

Revert PR. Keep goldens only if captured from true pre-port GA (otherwise delete goldens too).

### I.8 Risks

| Risk | Mitigation |
|---|---|
| Capture after partial port | Capture commit first on clean tree |
| Port uses Fidelity bounce | Code review vs matrix; goldens fail |
| Missing friendly flag | Goldens with teammate collisions fail `friendly_after` |
| test_physics link error on `g_friendly_collision` | Define in test TU per §F.2 |

---

## J. Phase 2 — OQ2 harness (reduced scope)

### J.1 OQ2 status split (removes honesty gap)

| Layer | Status as-built | Phase 2 obligation |
|---|---|---|
| **World ownership** | **Done** — arena uses `csb::Game` Fidelity only | Do not re-implement |
| **OQ2 acceptance tests** (“recorded action traces” within GATE vs referee/driver) | **Not done** as an automated harness | **Phase 2 delivers this** |
| **OQ2 “closed”** | Ownership yes; **fully closed only when harness green** | Harness + non-blocking CI |

`progress.h` stays small (`GlobalNext` / `Decode` / `LocalNext`). **No requirement** to add design’s `RaceProgress::HasWon` while `csb::Game` remains terminal authority.

### J.2 Deliverables only

1. **Harness target** (name locked): `//src/engine:arena_fidelity_trace_test` (cc_test) **or** `//src/physics:arena_fidelity_trace_test` if preferred — **pick `//src/engine:arena_fidelity_trace_test`**.
2. **Method (locked):**
   - Load map `i` from `GetTournamentMapsRaw()[i]` for `i = 0 .. 17`.
   - Construct `csb::Game` A: `initialize(track, 3)`.
   - Construct `csb::Game` B identically.
   - For `turn = 1 .. 50`: apply the **same** four actions to A and B via `applyAction` + `step({Fidelity})` (or `nextTurn` after pending).
   - Actions come from a **scripted table** (not live GA): for turn `t`, pod `p` uses deterministic targets derived from map CP coordinates and thrust `200` (document formula in test comments). Example locked formula:
     - `tx = track[(t + p) % track.size()].x`, `ty = track[(t + p) % track.size()].y`, thrust `"200"`.
   - After each turn: assert **exact `==`** on all 4 pods’ `p.x,p.y,s.x,s.y,angle,next,shieldtimer,boosted,won` and both `playerTimeout` values between A and B.
   - Rationale: proves deterministic Fidelity path; same code as arena world step. Full “vs replay_driver subprocess” is **optional extra**, not required for Phase 2 exit (A/B in-process is enough because both are `csb::Game` Fidelity — this catches non-determinism / pending-move bugs). **OQ2 wording “vs replay_driver”:** add **one** integration case: dump actions for map 0 / 20 turns; drive `replay_driver` via the existing Python `CppPhysics` **or** C++ spawning driver — compare to Game A within **GATE_*** (pos/vel/ang/timeout). **Minimum:** in-process A/B exact for 18×50; **plus** at least **1** map vs `replay_driver` within GATE_*.
3. **CI (OQ8):** non-blocking step or `continue-on-error: true` running the test. **Promote to blocking** after **7 consecutive green runs on the default branch** (count in `SSOT.md`). Until promotion, program DoD may list L4 as **“deferred, non-blocking, soak 7/7 pending”** with date noted in `SSOT.md`.

### J.3 Explicit non-goals for Phase 2

- Do **not** delete `GAPhysicsSimulator` or `PhysicsSimulator`.
- Do **not** change Fast or GA.

### J.4 Acceptance

```text
[ ] arena_fidelity_trace_test in Bazel
[ ] 18 maps × 50 turns in-process A/B exact match
[ ] ≥1 map vs replay_driver within GATE_*
[ ] Non-blocking CI step present OR SSOT.md records explicit deferral date + reason
[ ] Gate A/B still 100%
```

### J.5 Rollback

Remove test/CI step.

---

## K. Phase 3 — Switch GA to `csb::fast`; delete engine GA

### K.1 Goal

Single **runtime** Fast owner for shared builds: `csb::fast`. Delete `GAPhysicsSimulator` from `engine.cpp` / `engine.h`.

### K.2 Steps (ordered)

1. Confirm Phase 1 goldens green on tip.
2. Replace every `GAPhysicsSimulator::SimulateTurn` in **non-`CG_STANDALONE`** paths with `csb::fast::SimulateTurn` (§F.1 bridging).
3. Confirm `g_friendly_collision` is the same global Fast sets (§F.2).
4. Delete `GAPhysicsSimulator` class and methods from `engine.h` / `engine.cpp`.
5. `grep -R GAPhysicsSimulator src/` → **no matches** (except docs).
6. Delete `PhysicsSimulator` **if and only if** `grep -R PhysicsSimulator src/` shows no required references (arena must not use it; shared cg_bot must not). If only standalone uses the name under `#ifdef`, leave standalone until Phase 4 **or** point standalone at `csb::fast` early (allowed, reduces Phase 4 risk).
7. **Do not require** deleting `CG_STANDALONE` body in Phase 3.

### K.3 Acceptance bands (concrete)

| ID | Priority | Requirement |
|---|---|---|
| **P0a** | Primary | Phase 1 Fast goldens still **100% exact** |
| **P0b** | Primary | `grep -R 'GAPhysicsSimulator' src/` → empty (code); all search `SimulateTurn` calls are `csb::fast::SimulateTurn` in shared build |
| **P0c** | Primary | **Rollout fixtures** (locked): extend or add `src/physics/testdata/fast_rollout_goldens.json` with **≥ 16** scenarios, each with `k` in {1,5}, and per turn **four** `(angle_shift, thrust)` applied via **engine `Pod::ApplyGAAction`** then `SimulateTurn`. Capture **before deleting GA** using engine Pod + ApplyGAAction + GAPhysicsSimulator. Phase 3 tests: same ApplyGAAction + `csb::fast::SimulateTurn` → exact `==` on pos/vel/angle/shield_cd. **This is the concrete “transcript”** that does not depend on GA timing/RNG. |
| **P1** | Secondary | Tournament self-play informational: `BOT_THREADS=1 bazel run //src/tournament:benchmark_tournament -- --start-map 0 --end-map 18 --repeats 10` vs **parent commit baseline** recorded in PR. Win totals within **±5% relative** **or** skip if P0a–P0c pass and baseline not recorded — if skipped, note in PR. **P0 beats P1.** |

**Do not** use live multi-thread GA action logs as P0 (timing noise). P0c replaces vague “action transcripts.”

### K.4 Acceptance checklist

```text
[ ] P0a, P0b, P0c green
[ ] L1 100%
[ ] SSOT.md: Phase 3 done; engine Fast ownership ended
```

### K.5 Rollback

Revert PR; restore GAPhysicsSimulator from git.

---

## L. Phase 4 — Amalgam + kill submit fork

### L.1 Goal

No hand-maintained Fast/engine fork for CodinGame; tournament links a normal library.

### L.2 Amalgam contract (locked)

| Item | Locked value |
|---|---|
| Generator | Bazel `genrule` or `tools/amalgamate_cg_bot.py` invoked by Bazel — target name **`//src/cg:cg_bot_amalgam`** |
| Output | `$(GENDIR)/src/cg/cg_bot_amalgam.cpp` (build artifact; not hand-edited SSOT) |
| Contents order | (1) header comment “GENERATED — do not edit” (2) inlined minimal deps (3) **`csb::fast` + `g_friendly_collision` definition** (4) bot GA/eval/I/O (5) `main` for CG |
| Kinematics | **Fast only** (`ApplyGAAction` + `csb::fast::SimulateTurn`) — not Fidelity Game |
| Parity test target | `//src/cg:amalgam_action_parity_test` |
| Parity method | Fixture stdin worlds **or** internal API: run **modular** bot vs **amalgam** bot for maps **0–4**, **50 turns** each, **BOT_THREADS=1**, **SeedRand(1)** if bot uses engine RNG for anything in action selection; compare **exact** stdout action lines (two lines per player turn as CG format). If full bot RNG still nondeterministic under SeedRand, parity test compares **Fast rollout fixtures only** (P0c style) for amalgam-linked `csb::fast` **plus** compile smoke of amalgam — **locked fallback:** amalgam must compile and P0c goldens run against amalgam’s linked `csb::fast` (same code); full bot action parity is **best effort** with SeedRand(1) + BOT_THREADS=1; if flaky, document in PR and keep P0c + compile as Phase 4 exit **minimum**. **Prefer** fixing determinism over dropping parity. |
| Delete | `CG_STANDALONE` **physics/engine bodies** in `cg_bot.cpp`; `cg_bot_wrapper.h` `#define main` + `#include "cg/cg_bot.cpp"` |
| Tournament | Links `cc_library` (e.g. `//src/cg:ga_bot`) implementing `IBot` without including `.cpp` as text |

### L.3 Bot split (YAGNI ceiling)

Maximum split unless pain demands more:

```text
ibot/bot_config (from engine/bot.h)
ga_search (GA loop)
io_cg (CG stdin/stdout main)
# heuristics may remain inside ga_search
```

**≤ 8** translation units for bot package.

### L.4 Acceptance

```text
[ ] No CG_STANDALONE physics body in source
[ ] No include-cpp tournament wrapper
[ ] Amalgam target builds
[ ] Minimum: P0c goldens pass against amalgam-linked fast; Prefer: action parity maps 0–4 × 50 turns exact
[ ] L1 100%; Phase 1 goldens 100%
```

### L.5 Rollback

Restore previous Bazel targets and sources from git.

---

## M. Phases 5–8 (brief but actionable)

### M.1 Phase 5 — Engine shrink

```text
[ ] grep -E 'ResolveCollision|GetCollisionTime|mcoeff|GAPhysicsSimulator' src/engine → no collision math
[ ] arena + adapters + RNG/Timer only (or RNG moved next to bot)
[ ] L1 green
```

### M.2 Phase 6 — Constants / maps hygiene

```text
[ ] Fidelity/Fast use constants.h values for radii/friction/rotate/timeouts (no stray literals for those)
[ ] physics/maps.h not taught as SSOT (delete or rename to go_referee_maps_reference.h)
[ ] grep 640000|360000 in src/ → only physics/fast/constants or justified bot disk checks
```

### M.3 Phase 7 — Docs + CI promotion

```text
[ ] L4 blocking after 7/7 soak (update SSOT.md)
[ ] L5 blocking after Phase 4
[ ] README/GEMINI/src/README match ownership table §C
[ ] Annex implementation plan status rows aligned or marked superseded
```

### M.4 Phase 8 — No new forks

```text
[ ] cg_rust / other predictors: experimental label OR consume csb / csb::fast
[ ] PR checklist question: “Adds second collision implementation?” → reject unless quarantined
```

---

## N. Verification ladder (L0–L7)

| Level | Content | Blocks PR merge? | When |
|---|---|---|---|
| **L0** | `check_verification_policy.py` + `//src/physics:test_physics` (incl. Fast goldens after Phase 1) | **Yes** | now / after P1 for goldens |
| **L1** | Gate A + Gate B | **Yes** | now |
| **L2** | `bazel build //...` | **Yes** | now |
| **L3** | Fast goldens (+ rollout goldens after Phase 3) | **Yes** | after Phase 1 (rollout after Phase 3) |
| **L4** | Arena fidelity trace test | Non-blocking Phase 2 → **Yes after 7 green default-branch runs** | Phase 2 |
| **L5** | Amalgam parity / amalgam-linked P0c | **Yes** after Phase 4 minimum | Phase 4 |
| **L6** | Full leaderboard fidelity | Nightly / research | never default PR gate |
| **L7** | Bot Elo | **Never** physics merge gate | — |

---

## O. OQ table (complete — also mirror into SSOT.md)

| ID | Question | Status | Owner rule |
|---|---|---|---|
| **OQ1** | Can GA run Fidelity under CG time budget? | **Deferred** — notes only when Phase 3 implementer measures | Phase 3 implementer may add notes; no code requirement |
| **OQ2** | Arena outcomes = Fidelity on action traces | **Ownership done**; **harness = Phase 2**; closed when §J acceptance green | Phase 2 |
| **OQ3** | Core radians + degrees adapter | **Locked** — `csb::Game` + `SyncViewFromGame` | — |
| **OQ4** | C++20? | **Deferred** — remain C++17 unless staff decides in SSOT.md | Staff |
| **OQ5** | Keep `src/physics/` vs move to `src/core/physics`? | **Deferred** — **default stay `src/physics/`**; no move in Phases 1–4 | Staff / only with explicit SSOT.md decision |
| **OQ6** | Adopt or remove `nlohmann_json`? | **Deferred** — Phase 6/7 optional; not blocking SSOT physics | Phase 6/7 author |
| **OQ7** | Full GA eval on global CP? | **Deferred** — caller-owned local CP remains until explicit bot migration | Bot owner; optional post–Phase 4 |
| **OQ8** | Arena-fidelity CI non-blocking first | **Locked** — Phase 2 non-blocking; promote after 7/7 | Phase 2 / 7 |

Implementers **must not invent** OQ1/4/5/6/7 outcomes in PRs without updating `SSOT.md`.

---

## P. Program definition of done

- [ ] Physics package owns Fidelity (`csb::Game`) and Fast (`csb::fast::SimulateTurn`)
- [ ] `GAPhysicsSimulator` gone from `src/`
- [ ] Engine has no collision math
- [ ] `CG_STANDALONE` physics body gone; amalgam exists; tournament does not include-cpp
- [ ] OQ2 harness green **or** L4 explicitly deferred in `SSOT.md` with date (ownership already on Game)
- [ ] Teaching docs match §C
- [ ] Continuous `PR_MERGE_OK`
- [ ] No second collision implementation outside `third_party` + docs

---

## Q. Fidelity interleaving

| Allowed | Forbidden |
|---|---|
| Separate PRs fixing golden long-tail (~10) Fidelity-only with L1 held | Same PR as Phase 1 Fast |
| Updating SUPER_ACCURACY with live numbers | Changing GATE_* to hide fails |

---

## R. Start-tomorrow queue (execution order)

1. **Phase 0** if GEMINI/README still teach permanent dual SSOT.
2. **Phase 1 step 2:** capture `fast_goldens.json` on clean tree; commit.
3. **Phase 1 steps 4–9:** port `csb::fast`, fix `Game::step`, tests, checklist.
4. **Phase 2** anytime in parallel after step 3 starts (harness).
5. **Capture `fast_rollout_goldens.json` (P0c)** before Phase 3 delete (can be end of Phase 1 or start of Phase 3).
6. **Phase 3:** switch + delete GAPhysicsSimulator.
7. **Phase 4:** amalgam + kill forks.
8. **Phases 5–8:** shrink, hygiene, promote CI, watch forks.

---

## S. Double-check summary (self-review of this document)

| Critique gap | Resolution in v2 |
|---|---|
| Doc authority | §A explicit; annex void list |
| Game::step(Fast) mechanism | §F.3 single mechanism (assert + no-op return; never Fidelity) |
| Friendly flag spelling | §F.2 global `g_friendly_collision` only; no `csb::fast::` second flag; test TU defines for physics tests |
| Golden N/K/ε/path/source | §I.4 fully numeric |
| 312 vs ~10 fails | §D.3 |
| Phase 2 vs OQ2 completeness | §J.1 ownership vs harness split |
| Phase 2 remaining work | §J.2 only harness+CI; scripted actions formula; A/B exact + 1× driver GATE |
| Phase 3 transcripts | §K.3 P0c rollout goldens with ApplyGAAction — no live GA RNG |
| Phase 3 acceptance bands | §K.3 P0a/b/c + optional P1 |
| PhysicsSimulator delete gate | Phase 3 + grep-clean (§K.2.6) |
| Dual ownership | §D.4 |
| progress.h / RaceProgress | §C + §J.1 — Game terminals; no HasWon required |
| OQ1/4/5/6/7 | §O complete table; default OQ5 = stay `src/physics/` |
| Design radians Fast | Void (§A) |
| Fast ≡ Fidelity EndTurn | Explicitly not required (§E) |
| L4 soft DoD | §J.2 / §N / §P allow documented deferral |
| Amalgam parity flaky GA | §L.2 minimum = amalgam-linked P0c + compile; prefer full parity with SeedRand |

**No implementation was performed in producing this plan.** Ready for Phase 1 execution only when a human asks for code.

---

## T. Mandate (final)

> Capture exact goldens from today’s `GAPhysicsSimulator`, port that fragment to `csb::fast::SimulateTurn` on degrees-isomorphic pods, make `Game::step(Fast)` refuse Fidelity, keep the merge gate sacred, switch search and delete every other collision owner, generate the CG paste — without inventing OQ answers, loosening exact golden equality, or mixing Fidelity fixes into Fast PRs.
