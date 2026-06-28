# Verification Truth Policy — Locked Spec (repo + implement)

**Audience:** Staff (or agent) **with this repo open**. Not a doc-only closed contract.  
**Task:** Verification **governance** — honest compound physics merge-green. **Not** bot/engine unspaghetti.  
**Historical label “choice 3”** = this task only (no in-repo choice menu).  
**`physics.h` behavior:** unchanged by this task.  
**`docs/agent_pack/**`:** out of scope (do not sweep).  
**Do not implement from** historical SOLID/spaghetti review text (`docs/archive/CODE_REVIEW_SOLID_AND_SPAGHETTI.md` is a pointer only).

| Term | Meaning |
|---|---|
| **`MERGE_PHYSICS_OK`** | Job **id** `physics-accuracy` (frozen) is green |
| **`PR_MERGE_OK`** | At least `battle-retention` ∧ `build-and-test` ∧ `physics-accuracy` (+ branch protection). This task does not change branch protection. |

**Enforcement:** §9 checker is **automated subset**. §10 is **full done** (includes runtime). **§9 green ≠ done** until §10 is satisfied.

---

## 1. Compound gate

```text
(U)  bazel test --config=ci //src/physics:test_physics
(A)  MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
(B)  MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
```

Copy Bazel `//src/physics:replay_driver` → `sim/replay_driver` before (A)/(B).  
Do **not** remove (U) because `build-and-test` also tests `//...`.  
(B) is a **second** corpus, not a subset of (A).  
C++ `verify_battles --dir` is **DIAGNOSTIC** only in PR CI (nightly may run it labeled diagnostic).

**Role lines (exact, stderr, single line):**

| Tool | Line |
|---|---|
| `verify_battles.py` without `--gate` | `role=DIAGNOSTIC` |
| `verify_battles.py --gate` (valid corpus) | `role=GATE` |
| `verify_golden_corpus.py` as `__main__` (any tier, before work) | `role=GATE_COMPONENT` |
| `compare_battle.py` as `__main__` | `role=DIAGNOSTIC` |

**Exit codes (`verify_battles.py`):** battle failures → **1**; wrong `--gate` corpus → **2**; success → **0**.

**`--gate` path rule:** `realpath(cwd/battles/test_session_battles) == realpath(directory)` (cwd = invocation cwd; CI uses repo root).

**Rejected:** `MAD_POD_CLAIM_GATE`; path-implies-gate without `--gate`.

---

## 2. Files owned by this task

| Path | Role |
|---|---|
| `sim/tolerance_policy.py` | `GATE_*` / `EXPLORE_*` constants |
| `sim/compare_util.py` | `pos_close`, `vel_close`, `angle_close`, `is_invalid_thrust` |
| `sim/verify_battles.py` | Gate (A) + diagnostic batch; argparse `--gate` |
| `sim/compare_battle.py` | Diagnostic; `--max-turns`, `--gate-tolerances`; legacy positional digit max-turns OK |
| `sim/physics_driver.py` | `ensure_driver_built()` priority rules |
| `sim/check_verification_policy.py` | §9 static checks |
| `.github/workflows/ci.yml` | Compound gate job |
| `.github/workflows/scheduled-tests.yml` | Diagnostic nightly labels |
| Mainline docs | README, GEMINI, `sim/README`, `docs/physics-verification.md` |

**GATE values (exact `==` in checker):** `(5.0, 3.0, 1.0, 1)` for POS/VEL/ANG/TIMEOUT.

**Driver (`ensure_driver_built`):** env `MAD_POD_REPLAY_DRIVER` (cwd abspath) wins over `driver_path` arg; else if candidate file exists + executable → use (print `driver=<abspath>` once/process, **no mtime rebuild**); else if not `MAD_POD_GATE_STRICT=1` → `g++ -std=c++17 -O2 -I<physics> -o <path> replay_driver.cpp` + WARN; else raise. Non-executable existing file → raise (no fallthrough).

**g++ WARN text (exact):** `WARN: ad-hoc g++ driver; CI uses Bazel //src/physics:replay_driver`

---

## 3. CI step names (mandatory exact strings in `ci.yml`)

- `Build physics targets (C++ diagnostic binary + tests + replay_driver)`
- `Gate (U): test_physics`
- `Gate (A): Python verify_battles --gate test_session`
- `Gate (B): golden corpus --tier pass`

Job display `name:` may be `Physics gate (compound)`. Job **id** stays `physics-accuracy:`.

**STRICT:** `MAD_POD_GATE_STRICT=1` must appear **≥2 times** in `ci.yml` (both Python gate steps). Prefer `env:` plus inline for clarity.

**Scheduled:** step title exactly `Nightly C++ verify test_session (diagnostic)`.

---

## 4. Checker §9 (`python3 sim/check_verification_policy.py` from repo root)

Implemented checks include: GATE tuple; policy files exist; `ci.yml` substrings (`physics-accuracy:`, `--gate` command, golden pass, `MAD_POD_GATE_STRICT`, `check_verification_policy.py`, `test_physics`); no `no Python required`; no non-comment C++ `--dir`/`--file` corpus runs; mandatory step titles; `docs/physics-verification.md` has `sim/tolerance_policy` and not the forbidden ±1 gate sentence; README/GEMINI/sim README greps; scheduled step title; no `MAD_POD_CLAIM_GATE` under `sim/`, `battles/scripts/`, `.github/workflows/`.

**Not in §9 (must still pass §10):** live corpus 100%, stderr role lines, exit 2 runtime, C++ banner, mtime behavior.

---

## 5. §10 Done checklist (human/runtime)

- [x] Policy + implementation in tree (this PR / commit series)
- [ ] `MERGE_PHYSICS_OK` commands green locally with Bazel driver copy
- [ ] `--gate` wrong dir → exit 2; no flag on test_session → `role=DIAGNOSTIC` on stderr
- [ ] `--gate` valid → `role=GATE` on stderr
- [ ] Golden → `role=GATE_COMPONENT` on stderr
- [ ] `check_verification_policy.py` exits 0
- [ ] No `physics.h` semantic change; no bot/standalone scope

---

## 6. Out of scope (F1/F2/F3)

Bot monolith, include-cpp, `csb_physics.h`, dual physics merge, CG_STANDALONE codegen, tighten `GATE_*`, promote C++ 0.01 to gate, `docs/agent_pack/**` sweep, rename job id without policy+checker PR.

---

## 7. Scorecard (honest)

| Audience | Grade |
|---|---|
| Staff + repo + §10 | Shipable |
| Agent optimizing only §9 | May miss runtime roles / exit 2 unless tests run |
| Doc-only / no repo | Fail by design |
| `docs/archive/CODE_REVIEW_SOLID_*` alone | Redirect only (F) |

**Scorecard note:** “zero invention on tolerance substring” = **§9 item for `sim/tolerance_policy` in physics-verification**, not the C++ `--dir` heuristic item.
