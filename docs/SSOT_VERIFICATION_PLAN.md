# SSOT Refactor — Verification Plan (accuracy non-regression)

| Field | Value |
|---|---|
| **Purpose** | Prove **before == after** on every accuracy-critical surface while executing the SSOT refactor |
| **Status** | Normative for implementers — run these gates; do not merge a wave if red |
| **Repo** | `/Users/samsi/csb/mad_pod_arena` |
| **Tied plans** | `docs/SSOT_REFACTOR_DESIGN.md`, `docs/SSOT_REFACTOR_IMPLEMENTATION_PLAN.md` |
| **Gate policy** | `docs/VERIFICATION_TRUTH_POLICY.md` (immutable job id / `GATE_*` unless co-PR) |

---

## 0. What “accuracy remains the same” means

We distinguish **locked accuracy** (must not change) from **intentional alignment** (allowed, must be tested as OQ2).

| Surface | Non-regression rule | How we prove it |
|---|---|---|
| **A. Referee Fidelity physics** (`physics.h` default path, `replay_driver`, Python gates) | **Zero semantic delta** — same states within `GATE_*` on all gated corpora | `(U)(A)(B)` + policy checker; optional golden snapshot hashes |
| **B. Fast / GA search physics** | After PR-3/PR-6: trajectories **match pre-refactor `GAPhysicsSimulator`** on pinned seeds (byte-semantic / tight tol) | C++ Fast goldens captured **before** deleting GA class |
| **C. Bot actions (modular / amalgam)** | After PR-7/PR-9: **identical stdout actions** on pinned seeds vs pre-change baseline | Capture baseline actions; compare exact |
| **D. Arena outcomes** | After PR-4: must match **referee / Fidelity `Game` / `replay_driver`**, **not** legacy arena (OQ2) | Action-trace suite vs driver; legacy arena may diverge — **not** a regression |
| **E. Build / link surface** | `bazel build //...` and `bazel test //...` stay green | Full graph each PR |
| **F. Verification governance** | Job id `physics-accuracy`, roles, `GATE_*` unchanged | `sim/check_verification_policy.py` |

**Bug definition for this program:** any unintended change on A, B, C, E, F; or failure of D’s OQ2 acceptance; or new crashes/UB/timeouts.

**Not a bug:** arena win rates shifting from legacy `PhysicsSimulator` toward CG referee; tournament max turns 1000→500; team timeout semantics aligning with CG.

---

## 1. Baseline capture (do this FIRST, before any code change)

Record commit SHA and artifacts under `logs/ssot_baseline/` (gitignored if huge; keep small traces in `docs/agent_pack` only if policy allows — prefer local `logs/`).

### 1.1 Environment stamp

```bash
cd /Users/samsi/csb/mad_pod_arena
git rev-parse HEAD > logs/ssot_baseline/COMMIT.txt
date -u +%Y-%m-%dT%H:%M:%SZ >> logs/ssot_baseline/COMMIT.txt
uname -a >> logs/ssot_baseline/COMMIT.txt
bazel --version >> logs/ssot_baseline/COMMIT.txt
```

### 1.2 Locked suite L0 — governance + unit physics

```bash
python3 sim/check_verification_policy.py
# expect: POLICY OK, exit 0

bazel test --config=ci //src/physics:test_physics
# expect: PASSED
```

Save exit codes to `logs/ssot_baseline/L0.txt`.

### 1.3 Locked suite L1 — compound physics gate (MERGE_PHYSICS_OK core)

```bash
bazel build //src/physics:replay_driver
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver
chmod +x sim/replay_driver

MAD_POD_GATE_STRICT=1 MAD_POD_REPLAY_DRIVER="$(pwd)/sim/replay_driver" \
  python3 sim/verify_battles.py --gate battles/test_session_battles \
  | tee logs/ssot_baseline/L1_test_session.txt

MAD_POD_GATE_STRICT=1 MAD_POD_REPLAY_DRIVER="$(pwd)/sim/replay_driver" \
  python3 battles/scripts/verify_golden_corpus.py --tier pass \
  | tee logs/ssot_baseline/L1_golden_pass.txt
```

**Pass criterion:** 100% battles pass at current `GATE_*` (5.0 / 3.0 / 1.0° / 1 timeout); exit 0 both commands.

### 1.4 Locked suite L2 — full Bazel graph

```bash
bazel build //...
bazel test //...   # or CI-equivalent; note engine:test_physics is cc_binary not test
```

Record any pre-existing failures (should be none on clean main).

### 1.5 Baseline B — Fast trajectory goldens (capture before PR-3 mutates / PR-6 deletes)

Until a dedicated harness lands, minimum baseline:

```bash
# Optional: run engine GA benchmark binary if still present
# Prefer: add //src/physics:test_fast_goldens in PR-3 that embeds fixed seeds.
# PRE-PR-3: document “goldens not yet in tree; PR-3 first commit adds compare vs GAPhysicsSimulator live”
```

**PR-3 rule:** first commit on the PR must add tests that fail if Fast ≠ current GA class, **before** deleting GA class in PR-6.

### 1.6 Baseline C — bot action snapshots (before PR-7/9)

```bash
# Prefer a small deterministic harness; if none, defer capture to PR-7 start:
# Run modular cg_bot on fixed stdin fixture if available.
# Record: logs/ssot_baseline/bot_actions_seed*.txt
```

If no stdin fixture exists, PR-7 creates fixture **and** baseline from pre-split `cg_bot` in the same PR’s first commit (compare parent SHA).

### 1.7 Baseline D — arena vs driver traces (for OQ2, not legacy lock)

Prepare N synthetic action traces (JSON or text protocol sequences) that will be used **after** PR-4 to prove arena == driver. Can author during PR-4; list paths in PR description.

### 1.8 Baseline T — tournament oracle (strength smoke, not accuracy lock)

```bash
BOT_THREADS=1 bazel run //src/tournament:benchmark_tournament -- \
  --start-map 0 --end-map 18 --repeats 10 --time-budget 7.5 \
  | tee logs/ssot_baseline/T_oracle.txt
```

Store scores for **comparison only** (may change under OQ2 arena alignment). Gate does not depend on T.

---

## 2. Per-PR verification matrix

Run **after every PR**, before merge. Copy checklist into PR body.

### Universal (every PR)

| ID | Command / check | Must |
|---|---|---|
| U1 | `python3 sim/check_verification_policy.py` | exit 0 if policy/docs/workflows/sim touched; else recommended |
| U2 | `bazel test --config=ci //src/physics:test_physics` | PASSED |
| U3 | `bazel build //...` | success |
| U4 | No rename of job `physics-accuracy`; no silent `GATE_*` edit | review |
| U5 | No new crashes in touched binaries smoke | manual/CI |

### PR-0 (docs only)

| ID | Check | Must |
|---|---|---|
| P0.1 | U1 | POLICY OK |
| P0.2 | `docs/SSOT.md` exists; lists oracle CLI exactly as §1.8 | yes |
| P0.3 | `docs/README.md` links SSOT design/plan/verification | yes |
| P0.4 | States SSOT authorizes bot/engine deletions; gate numbers frozen | yes |
| P0.5 | No `src/**` behavior change | `git diff --stat` src empty |

### PR-1 (delete `csb_physics.h`)

| ID | Check | Must |
|---|---|---|
| P1.1 | U2, U3 | green |
| P1.2 | `test -e src/engine/csb_physics.h` | **false** |
| P1.3 | `rg csb_physics src` | no hits |
| P1.4 | L1 gate (A)(B) | **identical pass rate to baseline** (100%) |
| P1.5 | `//src/engine:test_physics` gone or rewritten without csb_physics | build graph OK |

### PR-2 (constants + maps)

| ID | Check | Must |
|---|---|---|
| P2.1 | U2, U3, L1 | green / 100% |
| P2.2 | Arena map coords 0..17 **byte-identical** to pre-PR `ALL_MAPS` | array diff empty |
| P2.3 | `physics.h` Fidelity behavior unchanged (L1) | 100% |
| P2.4 | No teaching Go-13 as subset | docs |

### PR-3 (Fast profile in canonical module)

| ID | Check | Must |
|---|---|---|
| P3.1 | L0 + L1 | **zero gate delta** vs baseline (same passes; no new fails) |
| P3.2 | Fast goldens vs **live** `GAPhysicsSimulator` (while class still exists) | all seeds pass |
| P3.3 | Default `Game::step` / world path = Fidelity | code review + L1 |
| P3.4 | No edits to Fidelity CP/bounce/friction/angle storage in same PR | review |
| P3.5 | `g_friendly_collision` set on Fast teammate pairs | unit or golden covers |

**Stop-ship:** any L1 failure.

### PR-4 / PR-4b (arena on `csb::Game` + RaceProgress)

| ID | Check | Must |
|---|---|---|
| P4.1 | L0 + L1 | still 100% (arena not on gate path, but no accidental physics.h break) |
| P4.2 | Action-trace suite: arena winner + pod state within `GATE_*` vs `replay_driver` | **100% traces** |
| P4.3 | Tie/draw traces included | pass |
| P4.4 | Angle sentinel bot↔core↔bot tests | pass |
| P4.5 | Max turns 500; no `laps_completed == laps_` sole win path | review |
| P4.6 | Full 4-pod `GetActions` preserved | review / test |
| P4.7 | U3 | green |

**Note:** Divergence from **legacy** arena on same actions is **expected** and must be listed in PR; regression is only P4.2 failure.

### PR-5 (PhysicsSimulator Fidelity façade)

| ID | Check | Must |
|---|---|---|
| P5.1 | L1 | 100% |
| P5.2 | Any remaining façade callers behave as Fidelity | tests |
| P5.3 | No GA path uses Fidelity façade | review |

### PR-6 (GA → Fast; delete `GAPhysicsSimulator`)

| ID | Check | Must |
|---|---|---|
| P6.1 | L1 | 100% |
| P6.2 | Fast goldens still pass **without** GA class (goldens use Fast API only) | pass |
| P6.3 | `rg GAPhysicsSimulator src` | no hits |
| P6.4 | Oracle T vs baseline — record delta; investigate crashes only as bugs | no crash |
| P6.5 | Caller-owned CP still advances in eval smoke | no stuck next_cp |

### PR-7 (modular bot; kill include-cpp)

| ID | Check | Must |
|---|---|---|
| P7.1 | L1 | 100% |
| P7.2 | Action snapshots == baseline C (exact) on fixtures | pass |
| P7.3 | `rg 'cg_bot\.cpp' src/tournament` and no `cg_bot_hdr` include-cpp | clean |
| P7.4 | `benchmark_tournament` links and runs oracle T | runs |
| P7.5 | U3 | green |

### PR-9 (amalgam; kill STANDALONE fork)

| ID | Check | Must |
|---|---|---|
| P9.1 | L1 | 100% |
| P9.2 | Amalgam vs modular actions **exact** on seeds | pass |
| P9.3 | Amalgam vs **pre-PR-9** standalone actions exact (captured baseline) | pass |
| P9.4 | `rg CG_STANDALONE src` for engine fork blocks | gone |
| P9.5 | Size budget | under limit |

### PR-10 / PR-11 (cleanup / docs)

| ID | Check | Must |
|---|---|---|
| P10.1 | L1 + U3 | green |
| P11.1 | U1 | POLICY OK |
| P11.2 | Single SSOT narrative; no dual-SSOT teaching | review |

### PR-12 (optional arena-fidelity CI)

| ID | Check | Must |
|---|---|---|
| P12.1 | Job non-blocking | workflow |
| P12.2 | Same traces as P4.2 | pass on job |

---

## 3. Continuous integration mapping

| CI job / local | Maps to suites |
|---|---|
| `physics-accuracy` | L0 unit + L1 (U)(A)(B) |
| `build-and-test` | L2 / U3 |
| `battle-retention` | retention policy (unchanged) |
| Local T oracle | strength smoke only |
| Local Fast goldens | B non-regression |
| Local arena traces | D OQ2 |

**Merge rule:** PR is mergeable only if **Universal + that PR’s rows** are green. Prefer also L1 on every PR even if “docs-only” when `physics.h` might be touched transitively.

---

## 4. Before/after comparison protocol

For any suite that emits logs:

1. Store baseline under `logs/ssot_baseline/`.
2. Store post under `logs/ssot_post_<PR>/`.
3. Diff:
   - **L1:** pass counts must be equal; failure lists must not grow.
   - **Fast goldens:** all cases still PASS.
   - **Actions:** `diff -u` baseline vs post must be empty.
   - **Arena traces:** only compare to **driver**, not to legacy arena log.
4. On failure: **revert PR or fix forward** before starting next PR. No “fix later.”

---

## 5. Execution order with verification gates (program schedule)

```text
BASELINE L0 L1 L2 [T] [start Fast golden harness in PR-3]
    │
    ▼
PR-0 ── verify P0.* ──► PR-1 ── P1.* + L1 ──► PR-2 ── P2.* + L1
    │
    ▼
PR-3 ── P3.* (L1 zero delta + Fast==GA class) ── STOP-SHIP if L1 red
    │
    ▼
PR-4⊕4b ── P4.* (L1 + arena==driver traces) ── STOP-SHIP if traces red
    │
    ▼
PR-5 ── P5.* ──► PR-6 ── P6.* (Fast goldens without GA class)
    │
    ▼
PR-7 ── P7.* (actions exact) ──► PR-9 ── P9.* (amalgam exact)
    │
    ▼
PR-10/11 ── P10/11.* ──► FINAL L0 L1 L2 + full checklist §6
```

Optional PR-12/13 after FINAL.

---

## 6. Final program acceptance (done = all true)

- [ ] L0 policy checker OK
- [ ] L1 test_session gate 100% at `GATE_*`
- [ ] L1 golden `--tier pass` 100%
- [ ] `//src/physics:test_physics` PASS
- [ ] `bazel build //...` OK
- [ ] No `csb_physics.h`, no `GAPhysicsSimulator`, no tournament include-cpp, no STANDALONE engine fork (per plan phase reached)
- [ ] Fast goldens pass (if PR-3+ landed)
- [ ] Arena action-traces == driver within `GATE_*` (if PR-4 landed)
- [ ] Bot action exact match baselines (if PR-7/9 landed)
- [ ] `docs/SSOT.md` status table updated
- [ ] Job id still `physics-accuracy`; `GATE_*` unchanged

---

## 7. Incident response (accuracy regression)

| Symptom | Likely PR | Action |
|---|---|---|
| L1 battle fails | PR-3 Fidelity drift | Revert PR-3; bisect `physics.h` |
| Fast golden fails | PR-3/6 incomplete GA port | Restore GA class or fix Fast body to match goldens |
| Arena ≠ driver | PR-4 adapter / progress | Fix adapter; do not “relax GATE” |
| Action drift | PR-7/9 | Restore baseline; fix modular split |
| `//...` red, L1 green | PR-1 missed target | Fix build graph (PR_MERGE_OK requires both) |

---

## 8. Commands cheat sheet (copy-paste)

```bash
cd /Users/samsi/csb/mad_pod_arena

# Governance
python3 sim/check_verification_policy.py

# Unit physics
bazel test --config=ci //src/physics:test_physics

# Driver + gates
bazel build //src/physics:replay_driver
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver
export MAD_POD_REPLAY_DRIVER="$(pwd)/sim/replay_driver"
export MAD_POD_GATE_STRICT=1
python3 sim/verify_battles.py --gate battles/test_session_battles
python3 battles/scripts/verify_golden_corpus.py --tier pass

# Full graph
bazel build //...

# Strength smoke (not accuracy lock)
BOT_THREADS=1 bazel run //src/tournament:benchmark_tournament -- \
  --start-map 0 --end-map 18 --repeats 10 --time-budget 7.5
```

---

## 9. Document control

| Ver | Date | Notes |
|---|---|---|
| 1.0 | 2026-06-27 | Full verification plan for SSOT execution; pairs with implementation plan 1.1 |

**Full path:**

```text
/Users/samsi/csb/mad_pod_arena/docs/SSOT_VERIFICATION_PLAN.md
```
