# Physics fidelity milestones (`physics/max-fidelity`)

> **SUPERSEDED as live dashboard** — systematic verification 2026-06-28 (`physics/max-fidelity` @ `0e0141ae`+).  
> **Prefer** [`PHYSICS_SUPER_ACCURACY.md`](PHYSICS_SUPER_ACCURACY.md) for current scores, findings, and kill list.  
> This file remains only as a **short milestone index**; numbers below are **live-refreshed** so they no longer contradict the master log.

Goal: **replicate CodinGame physics as closely as possible** — super-strict, not “good enough for the gate alone.”

## Success criteria (in order)

| Milestone | Definition | **Live status** (GATE 5/3/1°, tip with tooltip M1 + ref hints) |
|---|---|---|
| **M1 — Outcomes** | Align with CG `log.winner` / `ranks` using **tooltip-aware** rules (invalid action, elimination), then finish / sole timeout / Go progress; **dual-elim uses CG ranks** (platform fallback — see SUPER_ACCURACY) | **test_session 312/312 = 100%** (harness); golden all **197/200** |
| **M2 — Turn-perfect (GATE)** | Every turn within GATE pos≤5, vel≤3, ang≤1°, timeout≤1, exact `next_cp` | **test_session 312/312**, **46364/46364 turns**; golden **190/200** turn-perfect; pass tier **188/188** |
| **M3 — Breadth / strict** | Full leaderboard + EXPLORE/C++-strict | Sample 500 only (~98% battles); not full 17.5k |
| **M4 — Perfection** | M1+M2+M3 on all retained non-timeout corpora | **10** golden turn fails remain |

## Live baseline (do not use historical ~87% outcome figures)

| Corpus | Outcomes (tooltip M1) | Turn-perfect | Turn accuracy |
|---|---:|---:|---:|
| `test_session_battles` (312) | **312/312 (100%)** | **312/312 (100%)** | **46364/46364 (100%)** |
| `golden_physics_battles` (200) | **197/200 (98.5%)** | **190/200 (95%)** | **~97.51%** turns |
| golden **pass tier** (188) | — | **188/188 Gate B** | — |
| `leaderboard_physics_divergences` (44 battles) | **~41/44** | **~34/44** | (live re-run) |
| Leaderboard sample 500 | **~97.6%** | **~98.0%** | **~99.18%** turns |

**Historical note:** `logs/fidelity_milestones/baseline_gate.json` recorded **272/312 outcomes** under the **old progress-only** metric — that is **not** current M1. Do not quote 87% as live truth.

**M1 honesty:** “100% outcomes” on test_session includes **ranks fallback on dual elimination** (~31 battles) and tooltip rules (~240 decisive). It is **platform-aligned matching**, not pure independent progress physics for every ranking.

## Residual M2 debt (10 battles — not 12)

`885827873`, `885912413`, `885928301`, `886449550`, `886469116`, `887715689`, `887820683`, `890666841`, `890670385`, `891370461`  
(Fixed since research snapshot: `882151685`, `885624120`.)

## Working rules

1. Prefer **`PHYSICS_SUPER_ACCURACY.md`** over this file for narrative and audit trail.  
2. Edit **CG keyframe behavior** in `src/physics/physics.h`; M1 **labeling** lives in `sim/verify_quick_accuracy.py`.  
3. Never claim 100% without a **fresh** corpus run.  
4. Gate A/B must stay green while chasing M2 on the 10.

## Commands

```bash
bazel build //src/physics:replay_driver //src/physics:test_physics
cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver
python3 sim/verify_quick_accuracy.py --gate --dir battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles
MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass
```
