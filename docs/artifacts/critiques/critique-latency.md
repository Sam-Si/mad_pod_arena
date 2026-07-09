## Latency/Deploy Critic Report

**Scope:** 75 ms budget realism for V-search + `fast_physics` rollouts; weight blob vs amalgam paste limits; first-turn 1000 ms vs subsequent turns; fail-closed GA rollback; obs encode cost for expanded v1.1 (~130 dim); CG-platform failure modes.

**Sources verified:**
- `docs/artifacts/ZERO_BIAS_SELFPLAY_VALUE_SEARCH_DESIGN.md` (latency table, KD14, PR-7a/10, SearchConfig)
- `docs/superpowers/specs/2026-07-09-obs-features-training-methods-catalog-design.md` (v1.1 ~130, Path C, L2)
- `src/cg/bot_config.h` (`turn_time_limit_ms=75`, `first_turn_time_limit_ms=1000`)
- `src/cg/internal/ga_prelude_and_search.inc` (IBR fractions, safety buffers 8/100/1.5 ms, timer culture)
- `src/physics/fast_physics.h` + live `bench_fast_physics` (opt, arm64)
- `dist/cg_submission.cpp` size **124 958 B (~122 KB)** — docs’ “~125 KB” is accurate

**Local microbench (this machine, `-c opt`, arm64 Clang 17):**

| Workload | Median |
|---|---|
| `fast_physics::step` | **~469 ns/turn** (~0.47 µs) |
| Fidelity `Game::step` | ~512 ns/turn |
| Search-shaped batch | ~318 ns/turn |
| Clone + `step_batch` | ~174 ns/turn |
| Snapshot undo | ~180 ns/turn |

Design example `T_step=2 µs` is **~4× looser than this host** (reasonable as a placeholder for weaker CG CPUs), but **is still an unvalidated guess for the judge host** (OQ-RL10 open).

---

### Summary

The Zero-Bias design’s **timer culture is correctly borrowed from production GA** (75 / 1000 ms, 8 ms safety ≥50 ms, 100 ms safety ≥500 ms, timer-capped search). Path C (GA shell + V leaf + `fast_physics`) is **plausible under 75 ms on paper** *if* leaf V is cheap (≲0.05–0.1 ms) and candidate count is timer-capped rather than fixed-pop-to-completion.

The critical deploy risk is **not** “`fast_physics` is too slow per step” on a modern laptop; it is the **product of (1) CG host unknown T_step, (2) leaf Encode+V dominating capacity, (3) dual physics + dual bot path + weight embedding blowing amalgam size/compile, and (4) fail-closed dual-path that must live in one paste file**. Catalog v1.1 (~130-dim history/map) is **latency-cheap** relative to search, but **size- and correctness-relevant** if shipped too early.

**Bottom line:** Ship with **obs v1 (70)**, **int8/base64 compact weights**, **single-thread ValueSearch**, **boot canary → permanent GA for the process**, **hard measured N_cand/p99 on a CG-like x86 profile**, and treat **float32 text weights + dual full IBR + simultaneous MCTS** as default-off until budgets are proven.

---

### Critical

1. **Capacity model understates leaf V / Encode (dominant term, not `T_step`).**  
   Design table (`T_search ≈ 66.5 ms`, example `T_step=2 µs`, `T_V=0.05 ms`, H=6 → ~1000 cands) is directionally fine **only if `T_V` stays ~0.05 ms**.  
   - `T_net ≤ 0.3 ms` acceptance allows **~220 cands/turn** even if physics were free:  
     `N ≈ 66.5 / 0.3 ≈ 220`.  
   - Measured physics: H·T_step ≈ 6 × 0.5 µs ≈ **3 µs** — negligible vs a 50–300 µs MLP forward.  
   - **Missing from capacity model:** per-candidate state copy/snapshot, Encode-at-leaf (not just root `T_enc=0.05`), mutation, and any prior logit work.  
   **Failure mode:** “V is smart but search is 5–10× thinner than GA Fast+heuristic evals” → loses L2/L6 vs `ga_baseline_v1` despite good offline V.

2. **Amalgam size: weights as C source are the real bomb; soft 400 KB is already fragile.**  
   - Today paste: **~125 KB** (`dist/cg_submission.cpp`).  
   - MLP ~30k floats (70→128→128 + factorized heads): **~116 KB float32 binary**.  
   - Naïve `float w[] = {…}` text embed: **~450–500 KB source** for weights alone → **total paste ~600–700 KB** (soft **≤400 KB** broken; hard **1 MB** still OK).  
   - v1.1 ~37k floats: text embed **~580 KB weights alone**.  
   - Design’s “float32 blob 120–200 KB; int8 ≤64 KB; hard max 256 KB” is **binary weight payload**, not **CG paste character size**. Export PR-10 must treat **source bytes** as the constraint.  
   **CG-specific:** one-file paste; no external weight file; no mmap; large TU = slow IDE compile / possible judge compile pressure.

3. **Dual-path rollback doubles physics surface in the paste.**  
   Current amalgam genrule cats **`fast.h` only** (approx GA collision). Champion ValueSearch **must** pull `fast_physics` + `fidelity_world_step` (+ math/constants mirrors) for KD14 EXACT rollouts. Fail-closed GA path still needs **`csb::fast::SimulateTurn`**.  
   Without aggressive dead-code / single-kernel strategy, paste grows by **tens of KB of physics + search shell + net + weights**, and compile risk rises.  
   **Failure mode:** ship path either (a) drops GA rollback to save size (violates KD10 / fail-closed), or (b) ships both and blows soft size / compile time.

4. **CG host T_step / p99 is unmeasured (OQ-RL10); local arm64 ≠ judge.**  
   Bench host is Apple arm64; CG multiplayer is typically **shared x86_64 Linux**, often **1 effective core**, noisier timers, no turbo guarantees. 2–5× slowdown vs this bench is plausible → `T_step` 1–2.5 µs still fine, but **`T_V` and copy overhead** scale the same way → N_cand collapses.  
   Promotion league on M-series / fat workstation **does not certify CG p99**.  
   **Failure mode:** offline Wilson pass @75 ms, live timeout / starved search on CG.

5. **Fail-closed is specified for load/sha256, not for mid-turn budget / soft faults.**  
   Design: invalid weights → `CreateGABot`; boot canary; dual amalgam path. Good. Gaps:  
   - If V path **overruns 75 ms** (bug, cold cache, pathological bounce loops with safety=200), CG **timeout = loss** — no mid-turn rollback.  
   - Dual path must be **selected once at boot** and never re-enter a half-initialized ValueSearch.  
   - Canary that only checks sha256 but not a **timed dummy search+V** will miss “loads fine, too slow to search.”

---

### Major

6. **Legacy IBR multi-stage must not be copied blindly into ValueSearch v1.**  
   GA non-tight schedule (t0=0.13, t1=0.46, t2=0.60, full wall; absolute elapsed limits from turn start) runs **up to four sequential searches** sharing one timer — proven for heuristic Fast evals. Design correctly prefers **single timer-capped GA shell** for V leaves.  
   **Risk:** reintroducing 4-stage IBR with V at every leaf multiplies Encode+V cost and starves the final stage. Keep multi-stage **off** until single-stage N_cand is healthy.

7. **Safety buffer culture is correct; net of buffers shrinks usable search.**  
   From `RunGAParallel`:  
   - ≥50 ms → **8.0 ms** buffer  
   - ≥500 ms → **100 ms** buffer  
   - <15 ms → **1.5 ms**  
   Matches design `B_safe`. Effective search @75 ms ≈ **67 ms** before Encode/net; first turn ≈ **900 ms**.  
   **Gap:** ValueSearch must also reserve **I/O + action formatting** and forbid **verbose `cerr`** (GA still has debug `cerr` paths gated by `verbose` — ship path must force silent).

8. **First turn 1000 ms is real and underused in the deploy story.**  
   `bot_config.h` and prelude both set `first_turn_time_limit_ms = 1000`. Design: `1000 - 100` safety.  
   Correct uses: weight base64/int8 decode, sha256, one Encode+forward warm-up, optional deeper H=8 search, history buffers zero-init.  
   **Risks:**  
   - Spending first turn on **heavy search only** without canary → later turns still blow p99.  
   - Assuming first-turn budget covers **compile** (it does not; compile is offline on CG submit).  
   - Horizon scaling to 8 on turn 1 without capping leaf V cost.

9. **`num_threads`: GA defaults to `hardware_concurrency()`; ValueSearch defaults to 1 — keep 1 on CG.**  
   Design OQ-RL7: start single-thread. Correct. Multi-thread pool + migration channels in GA are **not free** under 75 ms and can worsen p99 on 1-core CG. Do not inherit `BotConfig.num_threads` auto-detect into champion ValueSearch.

10. **`fast_physics` step culture is fidelity-first, not microsecond-first.**  
    Hot path still: apply 4 moves → pack `WorldPod[4]` → `simulateFidelityWorld` (bounce loop `safety < 200`) → unpack. Clone/snapshot helpers exist (`copyFrom`, `saveSnapshot`/`restoreSnapshot`) — good for search.  
    Per-step cost is low on this host, but **multi-bounce frames** fatten the tail; p99 must be measured on **collision-dense** scenarios, not free-flight-only benches.

11. **Obs v1.1 (~130) is not a 75 ms problem; it is a size + bookkeeping problem if shipped early.**  
    Catalog blocks: pods ~48 + map ~40 + history K=3 ~36 + global ~8.  
    - Encode: still sub-millisecond if pure arithmetic (T_enc 0.05 ms is optimistic for full map+history but still ≪ search).  
    - Param growth 70→130 torso: modest (~7–8k extra floats) — **secondary to embedding format**.  
    - **CG failure:** history requires agent-side ring buffers of pos/vel/**own actions**; wrong pad on turn 1; boost/shield must be agent-tracked (KD20). Not latency — **train/serve parity**.  
    Catalog correctly freezes **Encode v1 (70)** until after L2; do not let v1.1 sneak into PR-10 amalgam.

12. **int8 quantize target (≤64 KB) is mandatory for paste realism, not optional polish.**  
    Without it, soft 400 KB is fiction. Design should make **int8 (or int8+base64 decode on turn 0)** a **ship gate**, with float32 only for offline / unit tests.

13. **Distillation fallback (A4) is the real CG safety valve — under-specified as a deploy trigger.**  
    “If search cannot fit 75 ms after PR-7a” → reactive π. Need **numeric tripwire**: e.g. if measured N_cand < N_min (say 100) or p99 > 70 ms on CG-class bench → ship distilled π, not starving V-search.

---

### Minor

14. **`T_enc = 0.05 ms` for root only** — fine for v1; re-measure for v1.1 map+history; still unlikely to matter.

15. **Example `T_step=2 µs` should be replaced with measured band** post-PR-7a (local + CI x86), and capacity tables should show **rows for T_V ∈ {0.05, 0.1, 0.3} ms**.

16. **Simultaneous MCTS (PR-11 / M10)** under 75 ms is a separate, harder budget problem (branching × joint actions). Correctly deferred; do not let catalog Path C wording imply MCTS is the first ship vehicle.

17. **League “@75 ms” on multi-thread tournament hosts** overstates GA strength vs single-thread CG. Pin `ga_baseline_v1` to **1 thread** (design already says this — enforce in harness).

18. **`#pragma GCC optimize` / `target("avx2")` in `fast_physics.h`:** ignored/warned on Apple Clang; may or may not apply on CG’s g++. Do not rely on AVX2 for budget math; keep scalar path correct.

19. **Export hard fail >1 MB** is a backstop, not a product target. Soft **≤400 KB** should be CI-visible for champion amalgam (code + embedded weights).

20. **Clock check frequency** (every 64–512 iters in GA) should be reused so timer polling does not dominate when N_cand is small and each cand is expensive (V leaf).

---

### Concrete design patches

| ID | Patch | Why |
|---|---|---|
| P1 | **Rewrite capacity formula** as `N_cand * (C_copy + H*T_step + T_encode_leaf + T_V) ≤ T_search`, with **required microbench columns** for each term on (a) ref laptop (b) x86 Linux CG-like. Drop “physics-limited ~1000 cands” as headline. | Prevent false comfort from T_step=2 µs story. |
| P2 | **Ship-gate weight encoding:** int8 (or smaller) + compact embed (base64/hex blob + decode on first turn); **forbid float32 text arrays** in amalgam. CI: `wc -c dist/cg_submission.cpp ≤ 400_000` (soft) / `≤ 1_000_000` (hard). | Soft 400 KB vs ~125 KB + text weights. |
| P3 | **Amalgam architecture:** either (A) single paste with compile-time `#if CSB_SHIP_VALUE_SEARCH` dual path and size report, or (B) two export targets (`ga_only`, `value_search`) with **runtime-impossible** mid-file switch — but rollback must still be **in-process** for canary fail → prefer (A) with GA always linked, V gated. Document expected byte budgets for Fast vs Fast+FP. | Dual physics size. |
| P4 | **Boot canary = load + sha256 + timed dummy:** Encode + 1 forward + H-step `fast_physics` rollout must finish in **≤ X ms** (e.g. 5 ms) on first turn; else permanent GA for session. | Fail-closed beyond corrupt weights. |
| P5 | **SearchConfig CG defaults (normative):** `num_threads=1`, `horizon=6` (8 only if N_cand headroom), `population` as **timer soft-cap not hard work**, `use_learned_value` only after canary, `verbose/cerr=0`. | Match CG 1-core / 75 ms. |
| P6 | **First-turn protocol (explicit stages):** (0) parse init/map (1) decode weights (2) canary (3) remaining budget → search with H≤8. Subsequent turns: **no re-decode**, history update + search only. | Use 1000 ms without blowing later turns. |
| P7 | **PR-7a acceptance must print:** T_step p50/p99, T_V p50/p99, N_cand/turn, wall p50/p99, collision-dense scenario set; **fail merge if wall p99 > 75 ms** on documented ref hardware; **fail ship if CG-class bench p99 > 75 ms**. | Close OQ-RL10. |
| P8 | **N_min tripwire → A4 distill:** if N_cand median < threshold (propose **100** @ H=6 on CG-class) after tuning, ship reactive π; keep V-search offline for data. | Avoid weak online search. |
| P9 | **Freeze obs v1 (70) through PR-10;** v1.1 history/map only after L2 win + separate size re-budget. History encode must define **turn-1 zero pad** and golden vectors. | Catalog already recommends this — elevate to ship constraint. |
| P10 | **No multi-stage IBR in ValueSearch v1;** optional later only if single-stage loses league and leaf cost is shown ≪ T_step. | Protect T_V budget. |
| P11 | **Leaf sharing:** evaluate V **only at horizon leaf** (not every tick); optional terminal ±huge without net; cache torso if root prior + leaf share network. | Cut T_V count. |
| P12 | **p99 budget split (normative table update):** e.g. B_safe 8 + I/O 1 + root Encode/net 0.5 + search 65.5; first turn: B_safe 100 + decode/canary 20 + search rest. | Align with real GetActions path. |
| P13 | **ga_baseline_v1 league pin:** force `num_threads=1`, `turn_time_limit_ms=75`, no verbose — already stated; add harness assert. | Fair 75 ms comparisons. |
| P14 | **CG-specific kill list in design:** multi-thread on, float32 paste weights, T1 open-space rays, GNN encoder, nested opp GA (`opp_model_ms>0`), simultaneous MCTS before GA+V proven, mid-turn path switch, diagnostic `cerr` on hot path. | Prevent platform failures. |

---

### Verdict by focus area

| Focus | Verdict |
|---|---|
| **75 ms + V + `fast_physics`** | **Feasible if leaf V is cheap and search is single-stage timer-capped.** Physics step cost is not the bottleneck on measured hardware; leaf net + CG host noise are. |
| **Weight blob vs amalgam** | **Critical gap.** Binary 256 KB cap ≠ paste size. int8 compact embed + dual-path physics size must be first-class PR-10 gates. |
| **1000 vs 75 ms** | **Aligned with code** (`bot_config.h` / prelude). Need explicit first-turn canary/decode protocol. |
| **Fail-closed GA rollback** | **Conceptually sound; incomplete without dual-physics size plan + timed canary + permanent session latch.** |
| **v1.1 ~130 encode** | **Not a 75 ms risk;** defer for size and history SSOT. |
| **What fails on CG** | Timeout from thin/slow search; paste too large / slow compile; missing weights file I/O; multi-thread regression; train/serve history/boost skew; unvalidated host T_V. |

*Critic: latency / amalgam / deploy realism only. No claim on RL sample efficiency or ladder Elo.*
