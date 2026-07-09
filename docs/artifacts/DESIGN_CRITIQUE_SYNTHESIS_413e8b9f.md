# Design Critique Synthesis (5 specialists)

| Critic | File |
|---|---|
| RL multi-agent | /tmp/critique-413e8b9f-rl.md |
| Latency/deploy | /tmp/critique-413e8b9f-latency.md |
| Observation | /tmp/critique-413e8b9f-obs.md |
| League/eval | /tmp/critique-413e8b9f-league.md |
| Physics SSOT | /tmp/critique-413e8b9f-ssot.md |

## Combined critical themes (ranked)

1. **Path C distribution shift**: train π/V under reactive self-play, deploy as search leaf → Expert Iteration required (search-generated data).
2. **2v2 credit assignment**: shared team advantage + factorized heads → free-rider; need pod-level or counterfactual baseline.
3. **cg_parity incomplete**: mask *observation* of opp boost, not FP dynamics; shield/timeout channels may leak sim privilege vs CG stdin.
4. **applyMove not fully SSOT**: world-step shared; thrust ULP paths may diverge — soak required before ML claims.
5. **KD14 fragile**: champion TU must hard-ban FastSimulateTurn; dual amalgam size risk.
6. **Latency**: leaf V dominates budget; int8/embed size gates; freeze obs v1 (70) through first ship.
7. **League**: Wilson vs prev_champion is weak; need external anchors + holdout + noise-aware CI.
8. **Obs v1.1**: geometry + history + map graph (critic ~104-dim) after ship, not before.


---
## Appendix: rl critic

# RL Critic Report

**Scope:** Self-play stability / non-transitivity / 2v2 credit assignment; PPO–V-trace & sparse reward; value targets under search; Path C leaf-V distribution shift; PFSP holes; AlphaStar / OpenAI Five / AlphaZero deltas.  
**Sources:** `docs/artifacts/ZERO_BIAS_SELFPLAY_VALUE_SEARCH_DESIGN.md` (rev 3.1), `docs/superpowers/specs/2026-07-09-obs-features-training-methods-catalog-design.md`.  
**Spot-check:** `src/core/constants.h` (`kTimeoutLimit=100`, `kMaxGameTurns=500`); GA defaults horizon=6 / pop=50; Fidelity `Arena` path; no Expert-Iteration data path in PR plan.

---

## Summary (verdict)

**Verdict: Strong systems design, incomplete RL design for “best bot.”**

The documents are excellent on **physics SSOT, train/serve dynamics (KD14), CG-parity obs (KD20), promotion statistics (KD17), and “don’t imitate BotConfig.”** Those are real ceilings that most CG RL projects hit first. As an **AlphaZero / AlphaStar / OpenAI Five–class learning system**, the design still has a **category error** at its core:

> **Path C is specified as deploy-time search + leaf V, but training is specified as reactive self-play (M3) that never generates data under the search policy that will actually ship.**

That is **not** a minor engineering order-of-PRs issue. It is a **policy-improvement / distribution-shift** failure mode that routinely produces:

- a V that looks great vs random / weak GA in reactive self-play,
- a ValueSearchBot that **trusts wrong leaves** under contact, shield, timeout races, and long open-loop sequences,
- and a ladder that **promotes anti-GA overfitting** rather than a robust simultaneous-game equilibrium.

Secondary killers: **shared team advantage with factorized pod policies** (lazy-pod / free-rider), **sparse ±1 over ~80–250 (max 500) turns with only soft ladder gates**, and a **PFSP that is a checkpoint soup**, not a league that forces discovery of non-transitive counters (race ⇄ ram-block ⇄ shield timing).

Plumbing PRs (env, encode, league harness) can proceed. **Do not treat “train reactive V, then enable leaf V” as the path to best bot without an Expert Iteration (or equivalent) closed loop.**

---

## Critical issues (must fix)

### C1 — Path C train/serve *policy* shift: leaf V is not the V search needs

| | |
|---|---|
| **Severity** | **Critical** — primary killer of “best bot” under Path C |
| **Where** | Zero-Bias: Search with learned value (lines ~561–577), PR-5a→PR-7 order, KD5/KD15/KD16; Catalog: M3→M4, Path C = “M4 + M7 + ladder M3 for V/π”, §4.2–4.3 |

**What the docs claim (implicitly):** Train π, V with self-play (reactive actors: `a0 ~ π_θ`), then online:

```text
rollout: fast_physics::Game snapshot step
score   = V_θ(encode_team_rel(state))
// optional: prior_mix * log π_θ(a|s) in mutation / PUCT
```

Catalog Path C: *“Online: GA shell or simultaneous MCTS … leaf = V_θ(s); mutations biased by π_θ”* while offline is *“self-play + PFSP train π,V”* — **no search in the data-generating policy**.

**Why it kills best-bot:**

1. **V is approximately V^{π_reactive, μ_PFSP}**, i.e. expected return when *both* sides (or at least self) play the **reactive** policy mixture seen in training.
2. **Search uses V as a cost-to-go for open-loop (or tree) plans of horizon H≈6**, under an **opponent model** (`π_opp` ensemble / frozen ckpt) that may not match training opps, and under **own continuation** that is GA-mutated sequences — **not** `a ~ π_θ` iid each step.
3. Classic **policy improvement** (or AlphaZero-style Expert Iteration) requires roughly: improve π using search that is guided by a value that is **consistent with the improved policy’s state distribution and backups**. Here search **changes the policy** (deploy π_search ≫ π_reactive) while V stays frozen under the old distribution → **systematic leaf bias**.
4. Worst states for this gap in CSB: **collision sequences, shield windows, timeout-pressure endgames, dual-pod role switches, boost timing**. Reactive π rarely visits the exact leaf geometries that GA+H produces; V will be **confident and wrong** exactly where search “thinks hardest.”
5. Optional π prior compounds this: mutations biased toward π_reactive while scoring with V^{π_reactive} **self-confirms** a mediocre style; search cannot freely discover open-loop tactics that reactive entropy never proposed.

**Feasibility note:** Dynamics train/serve match (KD14) is correctly treated as critical. **Policy/value train/serve match is equally critical and almost absent from the risk table** (Risks list dynamics skew, sparse reward, collapse — not “V trained without search”).

**Suggested fix (normative, pick one primary):**

- **Primary (AlphaZero-like Expert Iteration):**  
  - Data generation for L2+ **must** use the **same search operator** as deploy (ValueSearchBot / sim-MCTS) at a **train-time budget** (can be longer than 75 ms offline).  
  - Store **search-improved actions** (first-step of best sequence or MCTS visit policy) + **game outcome** (and optionally search root value).  
  - Train:  
    - π ← CE / KL to search policy (or BC on search actions) + entropy;  
    - V ← regression to **Monte Carlo return from search-played games** (or n-step + bootstrap under search policy).  
  - Reactive self-play (PR-5a) is only **L0/L1 cold start**, not Path C champion training.

- **Minimum viable closed loop if full EI is deferred:**  
  - **Reanalyze / search-consistent targets:** for each logged state, run short H-step search (or fixed GA pop) and set  
    `V_target(s) = backup_H(r, V; search_continuation)`  
    not only GAE under reactive π.  
  - **Distill periodically:** generate trajectories with ValueSearchBot vs PFSP; fine-tune V/π offline; freeze for deploy.

- **Hard acceptance gate for PR-7:** not “V leaf enables and p99≤75ms”, but  
  **“search-with-V WR ≥ reactive-π WR by a statistically clear margin on the same panel, AND search-with-V ≥ search-with-heuristic only if V is not anti-correlated with outcomes on held-out leaves.”**  
  Add a **leaf calibration** diagnostic: sample search leaves, play out to terminal with frozen opps, plot V vs empirical win rate — if Spearman is weak, **do not promote**.

---

### C2 — Shared team advantage + factorized pods: credit assignment is broken for 2v2

---
## Appendix: latency critic

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

---
## Appendix: obs critic

## Observation Critic Report

**Sources:**  
- `docs/artifacts/ZERO_BIAS_SELFPLAY_VALUE_SEARCH_DESIGN.md` (Encode v1, `obs_dim=70`, KD13/KD18/KD20)  
- `docs/superpowers/specs/2026-07-09-obs-features-training-methods-catalog-design.md` (full catalog, v1.1 sketch)  
- `docs/rules.md` (CG I/O: x,y,vx,vy,angle°,nextCheckPointId only; thrust 0–200; BOOST; SHIELD; timeout 100; first turn ≤1000 ms, then ≤75 ms)  
- Arena view extras (`src/engine/arena.cpp` SyncViewFromGame): shield_cd, boost_available, approximated timeout — **not** CG stdin

**Scope:** observation / feature design only (not training method choice).

---

### Summary

v1 (70-d) is a **valid CG-reconstructible skeleton** and a good freeze for PR-1 plumbing, but it is a **weak “best bot” observation**. It is almost entirely absolute map-frame state plus a flat CP list. It omits the relative geometry that actually makes continuous 2D racing/combat learnable and transferable (CP-frame errors, closing speeds, remaining path length, inter-pod approach geometry). Catalog v1.1 correctly pushes history + map-graph, but the ~130-d sketch is **over-budget**, under-specified on **CG-parity for shield/timeout**, and still light on **combat-relative** features.

**Main judgment:**  
- Ship **v1 = 70** only as a bitmatch scaffold.  
- Champion research path should move to a **compact v1.1 @ 96–112 dims (T0)** with relative geometry first, map path stats second, short history third.  
- Fix **dead / leaking channels** (always-zero `opp_boost_known`, sim-truth shield/timeout that CG never sends) before adding GNN/attention.

Without relative features, V-search still works (search can invent geometry in the tree), but **π prior + sample efficiency + map transfer** will lag a well-designed relative pack—exactly the Rocket-League-style lesson the catalog invokes but does not yet encode into the frozen layout.

---

### Critical missing

High-value gaps vs a ladder-competitive / “best bot” observation. Ordered by expected impact × CG-reconstructibility.

| # | Missing feature | Why it matters | Reconstructible on CG? |
|---|---|---|---|
| 1 | **Next-CP ego geometry (per own pod):** dist, û, angle_error (sin/cos), signed lateral (v×û), closing speed (v·û) | Core racing control law; GA already lives on these terms; absolute (x,y,sinθ) forces the net to relearn the same map-frame transform every time | Yes (from raw + CP list) |
| 2 | **CP+1 / path curvature:** dist or Δ to CP+1, exterior turn angle at next CP | Apex / early rotate / boost line selection; 1-step “point at next CP” policies fail hairpins | Yes |
| 3 | **Remaining race length** (along-track + current segment remainder) | Continuous progress for V; `next_global/40` is coarse; GA’s `dist_to_end` is a known strong leaf signal—encode as **feature**, never as reward bias | Yes (init CP list + next) |
| 4 | **Inter-pod approach pack:** dist, relative vel along LOS, ballistic TTCA / closing flag (analytic free-flight) | Blocking, ramming, shield timing; non-transitive combat is the whole point of PFSP | Yes |
| 5 | **Ally progress structure:** Δglobal_next (own0−own1), who-is-ahead bit, ally dist | Emergent runner/blocker without role labels | Yes |
| 6 | **Race lead margin:** max(own next) − max(opp next), plus remaining-length lead | Value is almost monotone in lead; currently only raw next_global per pod | Yes |
| 7 | **Timeout clocks (reconstructed):** turns since team last CP pass, both teams | Timeout is a **lose condition**; v1 assumes `playerTimeout` fields exist—**not in CG stdin** | Yes if agent tracks next_cp edges for both teams |
| 8 | **Own shield / boost bookkeeping + legal masks in obs** | Masks alone are not enough for V; shield downtime is multi-turn commitment | Own: agent history of SHIELD/BOOST. Opp boost: **no** (KD20). Opp shield: **no exact timer** |
| 9 | **Short history (K=2..3):** Δpos or vel, own last action (shift/thrust/special), collision proxy (‖Δv‖ jump), CP-pass edge | Infer opp BOOST (Δv spike), shield (unexpected bounce mass), intent; pure Markov v1 is blind to “just boosted” | Yes (agent buffers) |
| 10 | **Map path stats:** segment lengths, turn angles, cumulative / remaining length, cp_valid mask | Flat CP xy without topology is a weak inductive bias; padding zeros collide with real (0,·)/(·,0) risk without mask | Yes (init) |
| 11 | **Body-frame velocity** (forward/side slip) for own pods | Slip vs heading is the difference between grip line and scrape; cheap given sin/cos | Yes |
| 12 | **Won / finished flags** (per pod) mid-search | Online rare; **search leaves** after a pod finishes need terminal clarity—prefer terminal override in search, optional 1-bit in obs for training rollouts | Sim only mid-horizon; ship can omit if search handles terminal |

**Not critical for v1.1 (defer T1/TR):** GNN Laplacian, CP id embeddings, open-space rays, RNN memory, privileged opp boost truth, pure-pursuit “optimal heading” as a fixed heuristic channel (strategy-flavored; catalog correctly marks optional T1).

---

### Harmful/redundant

| Issue | Severity | Notes |
|---|---|---|
| **`opp_boost_known` ×4 always 0 under `cg_parity=true`** | High waste | 4 dead dims on champion path. Either drop, or replace with a **single** research flag outside ship schema. Do not train ship weights on constant zeros. |
| **Opp `boost_available` forced 0 but opp `shield_timer` left as sim truth (v1 layout)** | **Critical parity hole** | CG stdin has **no** shield field. Training V on true opp shield while serving with… what? Arena leaks shield via SyncViewFromGame; live CG does not. **Mirror KD20 for shield:** own shield agent-tracked; opp shield **0** (or separate inferred proxy from history, never sim timer). |
| **`timeout_own` / `timeout_opp` from Fidelity `playerTimeout`** | **Critical parity hole** | Not in CG input. Arena fabricates a display timeout from team clock. Encode must use **agent-reconstructed** clocks (reset on observed CP index advance for either team pod) at train **and** serve under champion mode—or V learns sim oracle timeouts. |
| **Absolute-only pod/CP coordinates** | High (sample efficiency / transfer) | Not “wrong,” but translation-variant. Without CP-relative and pair-relative channels, 18-map transfer is unnecessarily hard. Absolute CP list can stay; **add** relatives rather than only abs. |
| **`team_id` in obs under team-relative layout** | Medium | Breaks pure team-swap symmetry; net can overfit “I am side 0.” Prefer drop; side identity is already baked into pod order `[own0,own1,opp0,opp1]`. |
| **`next_cp_local/8` + full CP list + `next_global`** | Low–med redundancy | Local index is recoverable from next_global and track_n. Prefer **next_global** + **remaining length** + geometry to **next and next+1**; keep local only if you drop something else. |
| **`kNormVel = 1000`** | Medium scale risk | Boost 650 + multi-turn accel routinely exceeds 1000 before friction settles; features saturate. Prefer **~2000–2500** or soft clip after norm; document measured speed histogram. |
| **`shield_timer/4` scale** | Low | OK if meaning is {0..4}; ensure 0 means “can accelerate” consistently with Fidelity (timer 4 on activation frame, thrust forced while >0). |
| **CP pad-with-zeros without validity mask** | Medium | Unused CP slots as (0,0) are in-map coordinates. **Must** add `cp_valid[i]` (8 bits → 8 floats or 1 packed—prefer 8 floats for MLP simplicity) or pad to (−1,−1) **and** mask. |
| **Catalog v1.1 ~130-d kitchen sink** | Medium (amalgam / 75 ms) | History K=3 × all pods × full state + 8 CP graph + pods 48 is too fat for the 256 KB weight cap story before compression. **Block-ablate**; don’t ship 130 as default. |
| **Map fingerprint hash as float** | Harmful / useless | Arbitrary hash bits are not a smooth feature; use `cp_count`, length stats, or learned embedding (T1) instead. |
| **“Optimal free-flight heading” as obs (T1)** | Strategy-flavored | Borderline human prior; if used, ablate hard; never put in reward. |
| **Hand roles / BotConfig / GA score as inputs** | Forbidden (already documented) | Keep out. |
| **rules.md “boost common between pods” vs Fidelity per-pod `boosted`** | Docs trap | Champion masks/obs follow **KD13 (per-pod)**. Do not encode a team-shared boost bit that contradicts physics SSOT. |

**Non-stationarity note:** `turn/500` is fine (episode phase). Absolute positions are non-stationary across maps—another argument for relative geometry. History stacks without Δ-encoding are non-stationary in level; prefer **deltas** and event flags.

---

### Recommended v1.1 table (feature → dim → tier)

**Design goals:**  
1) Strict T0 / CG-parity champion path.  
2) Relative geometry first.  
3) Fix shield/timeout leakage.  
4) Fit amalgam: **target `obs_dim = 104`** (acceptable band **96–112**).  
5) Version: `obs_schema_version = 2` (v1.1); keep v1=70 frozen for PR-1 goldens.


---
## Appendix: league critic

## League/Eval Critic Report

**Sources reviewed**
- `/Users/samsi/csb/mad_pod_arena/docs/artifacts/ZERO_BIAS_SELFPLAY_VALUE_SEARCH_DESIGN.md` (rev 3.1: league, KD17, panel k=5, Wilson LB)
- `/Users/samsi/csb/mad_pod_arena/docs/superpowers/specs/2026-07-09-obs-features-training-methods-catalog-design.md` (ladder L0–L6, ablation matrix, catalog-18)

**Scope:** evaluation methodology only (promotion rules, panel design, pairing/seeds, metrics, exploitability, transfer). Not physics/obs/training-algorithm design.

---

### Summary

The closed league is a real step up from vibes and same-bot self-play: fixed panel, 18×2×20 grids, 75 ms-only promotion, pinned `ga_baseline_v1`, and a single Boolean Wilson LB rule (KD17) that correctly kills the inconsistent dual 0.52 gate. That is operationally clear and better than most CG RL prototypes.

It is **not** yet a sound definition of “best of all bots.” The primary test is a **non-inferiority / weak-superiority bar vs one predecessor** (Wilson LB ≥ 0.50 vs `prev_champion`), not a multi-opponent exploitability estimate. The k=5 panel is **endogenous** (median/hardest vs subject), so it overstates robustness under non-transitive race/block metas. Map×side pairing and bot seeding reduce noise but do **not** make GA/search deterministic enough for clean paired Bernoulli tests. Catalog-18 is necessary and **openly insufficient** (KD9, OQ-RL6), yet promotion, ladder L2–L5, and ablations all treat it as the authority surface. Secondary GA gate with ε=0.01 on Wilson LBs is under-specified and easy to game with draw-heavy or timeout-skewed play. Ladder L0–L6 stages mix training diagnostics with ship gates without a shared multi-metric dashboard.

**Bottom line:** keep Wilson + 75 ms + pinned GA as the **operational** spine, but redefine promotion as a **multi-metric, multi-opponent protocol** with (1) fixed external panel, (2) proper paired / block design accounting for GA stochasticity, (3) holdout maps, (4) timeout/progress/diversity health metrics, and (5) an explicit exploitability / rock-paper-scissors readout—not just “LB ≥ 0.50 vs last self.”

---

### Critical

**C1 — Wilson LB ≥ 0.50 vs prev_champion is not a “better bot” test**

- Design (KD17): N=720 games subject vs `prev_champion`; promote iff Wilson lower 95% CI on WR ≥ 0.50, with win=1, loss/draw=0.
- At N=720, Wilson LB ≥ 0.50 requires roughly WR̂ ≳ 0.537 (as the doc notes). That is a **one-sided “strictly better than coin-flip”** bar against a single opponent, not a multi-agent ranking.
- Failure modes this allows:
  1. **Cyclic promotion:** A beats B, B beats C, C beats A → each successive champion can pass LB≥0.50 vs predecessor while the pool’s cycle remains; Elo dashboard is optional and non-binding.
  2. **Stagnation as “progress”:** a near-clone of the champion with mild noise can sit at WR̂≈0.52–0.54, pass after enough lucky seeds, and replace the champion without strategy improvement.
  3. **Draw inflation / silent refusal:** treating draw as non-win (0) is conservative for *claiming* wins, but a bot that forces many draws can still pass secondary gates or look “non-regressing” vs GA while being useless on live CG (draws may be rare in CSB, but timeout dual-elim draws and max-turns draws exist).
  4. **No simultaneous control of family-wise error:** 4 pairings × optional soft metrics × ladder stages; only one Boolean is “authoritative,” so researchers will still cherry-pick non-authoritative heatmaps.

**C2 — Panel k=5 is not an independent evaluation panel**

Fixed panel slots (P0–P4):

| Slot | Role | Methodological problem |
|---|---|---|
| P0 subject | candidate | OK |
| P1 prev_champion | last promote | OK as regression anchor |
| P2 ga_baseline_v1 | pinned heuristic | OK as external anchor |
| P3 ckpt_median | median Elo **vs subject** in last train epoch | **Selected relative to the candidate** — not a fixed probe |
| P4 ckpt_hard | lowest WR **vs subject** | **Adversarially selected after training** — good for training (PFSP), bad as a fixed eval set |

- Under non-transitivity (race vs ram-block vs shield timing—design’s own motivation), P3/P4 answer “does subject beat *its* recent pool slice?” not “is subject hard to exploit by a stable diverse set?”
- Panel composition **changes every evaluation**, so historical league reports are not comparable; you cannot plot multi-week “panel WR” without re-running against frozen opponents.
- P4 “hardest” is **post-hoc selection**: expected WR vs P4 is biased low by construction; if promotion ever depended on P3/P4 (v1 primary does not), that would be circular. Currently primary ignores them for the Boolean—so **k=5 is marketing for a k≈2 decision** (prev + GA secondary). The hard/median slots are unpaid compute unless used in a real gate.

**C3 — Catalog-18 promotion as transfer gate is self-deception**

- KD9 correctly says catalog-18 ≠ full CG distribution; OQ-RL6 defers procedural holdouts; risk table rates overfit Medium; catalog ladder L6 is only a canary.
- Yet **every** ship-adjacent gate (promotion grid, catalog ablations, L2 map uniformity, L3 “full promotion,” L4 feature ablations) optimizes and measures on the same 18 tracks.
- With R=20 × 2 sides, the agent can memorize **spawn geometry and CP topology** for 18 fixed layouts (especially with map fingerprint / CP embeddings in catalog v1.1). Wilson “confidence” then describes **in-distribution resampling**, not generalization.
- Live CG maps (or even orderings/laps variants) can flip the meta; L6 “no regression vs GA on live” is too late and too vague to be a research gate.

**C4 — Map-side pairing + seed policy does not neutralize GA/search stochasticity**

- Design claims paired design: same map, side, shared env seed stream; bot RNG from `hash(map, side, repeat, agent_id)`.
- Problems:
  1. **Different agents get different seeds** (`agent_id` in hash). That is good for reproducibility of *each* bot’s internal RNG, but it **destroys perfect pairing**: subject and opponent do not share the same GA mutation stream; the “paired” unit is only (map, side, repeat) for the *environment*, not for joint stochastic search noise.
  2. **GA at 75 ms is timer-capped and path-dependent.** Wall-clock jitter, thread scheduling (`num_threads=1` helps), and candidate order make search **non-reproducible** across machines even with fixed seeds if any time-based stop is used. Then Wilson intervals treat games as i.i.d. Bernoulli while the true variance includes **implementation noise**.
  3. **Side swap is not a full symmetry control** for learned bots with residual training noise or non-symmetric action decoding; still useful, but not enough to claim unbiased estimate of true P(win).
  4. **Repeat R=20 on identical deterministic spawn** mostly re-samples **search RNG**, not map diversity. That inflates N for Wilson without adding ecological validity—classic **pseudo-replication**.

This is critical for KD17: the Wilson LB assumes approximately i.i.d. trials of a fixed win probability. Here N=720 is closer to “720 noisy evaluations of 36 fixed conditions (18 maps × 2 sides)” with clustered dependence. Unclustered Wilson **overstates** precision (intervals too tight → false promotions).

---

### Major

**M1 — Secondary GA gate is weak and statistically awkward**

- Rule: `WilsonLB(subject vs GA) ≥ WilsonLB(prev vs GA) − ε` with ε=0.01; first champion LB≥0.45 vs GA.
- Issues:
  - Comparing two **lower bounds** with a fixed ε is not a proper non-inferiority test; it does not control Type I error for “subject is worse than prev vs GA.”
  - First-champion bar 0.45 is **below coin-flip vs GA**—compatible with a still-losing bot becoming champion of an empty lineage.
  - Subject can **specialize to beat prev_champion** while trading off vs GA within 1 pp of the bound (often within noise), especially if prev was already mediocre vs GA.
  - No requirement that subject beat GA on a **per-map** floor (one map cluster can hide total collapse).

**M2 — WR-only (and draw-as-loss for WR) hides timeout / progress / style collapse**

---
## Appendix: ssot critic

## SSOT/Physics Critic Report

**Scope:** train/serve consistency for Path C (search+V), BridgeViewToFastPhysics, `cg_parity`, simultaneous joint apply, Fast-fragment reintroduction risk, and must-pass physics gates before any “best bot” claim.  
**Inputs reviewed:** `docs/SSOT.md`, `docs/artifacts/ZERO_BIAS_SELFPLAY_VALUE_SEARCH_DESIGN.md` (KD13/KD14/KD20/KD21), `docs/superpowers/specs/2026-07-09-obs-features-training-methods-catalog-design.md`, `src/physics/{fast.h,fast_physics.h,fidelity_world_step.h,fidelity_math.h,physics.h}`, `src/engine/arena.cpp`, `src/engine/engine.h`, GA apply/search path in `ga_prelude_and_search.inc`, `docs/rules.md`, `sim/check_ssot_policy.py`.  
**Code state:** `src/rl/` does **not** exist yet; Path C / Encode / Bridge are design-only. Critique is code-vs-design skew, not implementation bugs in missing RL code.

---

### Summary

Physics SSOT for the **world step** is real: both `csb::Game` and `csb::fast_physics::Game` call `simulateFidelityWorld` only. That is necessary but **not sufficient** for train/serve EXACT. Move application (`applyMove` / `applyThrust`) is **duplicated and diverged** between `physics.h` and `fast_physics.h`. The GA path still lives on a third, intentionally wrong kernel (`csb::fast::SimulateTurn`). KD14 correctly forbids champion rollouts on Fast; the design still leaves multiple easy ways to reintroduce it under Path C schedule pressure. KD20 zeros **opp boost** only; Encode v1 still ships several fields that pure CG stdin does not provide unless reconstructed from history. Arena joint apply order is correct (observe → four pending moves → one step); terminal porting must not invent a second branch order. **No “best bot” claim is legitimate until FP↔Fidelity EXACT on train distributions, bridge Encode bitmatch, terminal parity, and closed league @75 ms with `use_fast_physics_rollouts=true` and hybrid Fast off.**

---

### Critical

1. **Dual `applyThrust` / move owners (train dynamics ≠ gate Fidelity on knife-edges)**  
   - World-step is SSOT (`fidelity_world_step.h`).  
   - **Pre-world move physics is not SSOT.** `csb::Pod::applyThrust` in `physics.h` contains extensive ULP / nextafter / 3-4-5 / short-axis knife-edge logic used for Gate A. `fast_physics::Game::applyThrust` only does pole snap + `snapNearInteger` — no shared helper, no nextafter branches, and `evalSinCos` does not call `thrustCosSin` (which zeros |sin/cos|<1e-15).  
   - EXACT validators (`validate_fast_physics_battles`, `bench_fast_physics`) only prove parity on **corpora/scenarios they run**. Self-play will explore novel thrust/angle states; any residual FP↔Fidelity drift becomes **train ≠ serve ≠ CG** under Path C.  
   - **Patch:** extract one `applyThrustFidelity` (and ideally full `applyMove`) into a shared header consumed by both façades; fail CI if `fast_physics` reimplements thrust/rotate. Do not claim “Fidelity-equal rollouts” until that is true on adversarial random move streams, not only battle JSON.

2. **KD14 violation risk is structural, not just a flag**  
   - Today’s production search **must** call `FastSimulateTurn` → `csb::fast::SimulateTurn` (`ga_prelude_and_search.inc`). Fast is a **different game**: no CP/timeout/won, different bounce formula, mass 10 vs Fidelity inverse-mass 0.1, 10-collision cap, degrees pods, no global next.  
   - Design severity is correct: V trained on EXACT states + leaf on Fast-fragment states = **Critical** train/serve skew.  
   - Path C “GA shell + V” will default to copy-paste of `SimulateAndEvaluate` unless PR-7a is a hard compile-time fork (`#error` / separate TU that does not link `FastSimulateTurn` on champion path). Soft flags (`CSB_SEARCH_HYBRID_FAST`, `use_hybrid_fast`) are not enough if the default codepath still includes the Fast call site.

3. **Opp-boost dynamics under KD20 are underspecified (obs parity ≠ state parity)**  
   - KD20 / Bridge: Encode forces opp boost fields to 0; Bridge sets opp `boosted = 0` in FP root.  
   - That makes **search dynamics** treat both opp pods as still BOOST-eligible. Rollouts can invent opp BOOST after they already spent it on CG → wrong V backup and wrong shield/contact values.  
   - Conversely, never allowing opp BOOST underestimates ram threats.  
   - **This is a critical simultaneous-game modeling hole**, not a minor obs quirk. Need an explicit policy: e.g. (a) belief state / sample opp boost remaining, (b) conservative assume-available for threat, assume-spent for own planning, or (c) mask BOOST in **all** opp rollout policies while documenting bias. Champion Encode alone does not fix dynamics.

4. **`cg_parity` is incomplete relative to real CG stdin**  
   Pure CG per turn (`docs/rules.md`): `x y vx vy angle nextCheckPointId` only.  
   Encode v1 still includes under default champion path:

   | Field in Encode v1 | On CG stdin? | Reconstruction? |
   |---|---|---|
   | pos/vel/angle/local next | Yes | — |
   | own `boost_available` | No | Agent bookkeeping (OK if enforced) |
   | opp `boost_available` | No | KD20 zeros (OK for obs) |
   | `shield_timer` own/opp | No | Own: track SHIELD; **opp: not known** |
   | `timeout_own` / `timeout_opp` | No | Partial via observing `next` edges; **sim truth is privileged** |
   | `turn` | No | Agent counter (OK) |
   | `has_rotated` | No | Infer from angle sentinel / first real face |
   | `next_global` / laps | No | Count CP passes + track_n (OK if not fed sim `next`) |
   | `won` | No | Inferred when progress stalls at finish |

   Arena `SyncViewFromGame` is **privileged vs CG**: it fills `boost_available`, `shield_cd`, and approximate `timeout` for **all four pods**. Training/league through Arena IBot views without a CG-parity filter will teach V/π features that the amalgam cannot see.  
   **KD20 only closed the opp-boost hole; timeout + opp shield remain open privilege channels.**

5. **Amalgam / ship path can force Fast reintroduction**  
   - SSOT amalgam path still lists `fast.h` as the CG collision kernel; `fast_physics` pulls `fidelity_math` + `fidelity_world_step` + larger Game state.  
   - If Path C misses 75 ms or size budget, the shortest “fix” is hybrid Fast or pure Fast + V — exactly A8, rejected for champion, but still the operational escape hatch.  
   - Without a **hard ship gate** (export refuses `use_hybrid_fast` / refuses any `FastSimulateTurn` in ValueSearch TU; size/latency failures → distill reactive π, not Fast rollouts), “best bot” ships will silently re-skew.

---

### Major

1. **BridgeViewToFastPhysics correctness gaps (design sketch)**  
   Concrete failure modes if PR-7a implements the sketch literally:

   - **Timeout inverse:** Arena exposes `timeout = (kTimeoutLimit+1) - playerTimeout` (clamped ≥0). Bridge must invert to `playerTimeout`, not pass view.timeout as remaining. Wrong timeouts → wrong terminal mid-horizon and wrong Encode timeout channels.  
   - **Global next:** Prefer `GlobalNext(laps_completed, next_cp_id, track_n)`. Arena sets `laps_completed = 3` when `won` (legacy signal) — that can **corrupt** global next if used naively after finish. Bridge must use Fidelity `next` when available (league) or a dedicated finish flag, never overload lap=3.  
   - **Angle / hasRotated:** View uses degrees; FP uses radians. Sentinel `-1°` must map to `kInitAngleSentinel` rad + `hasRotated=false`. Mid-race angle wrap in degrees then `*kDegToRad` must match training (Fidelity keeps continuous rad). Bitmatch golden required.  
   - **Boost polarity:** View `boost_available` vs FP `boosted` are inverted. Own: agent bookkeeping; never trust view opp boost even if Arena filled it.  
   - **Integer targets:** Arena applies `static_cast<int>(tx/ty)` into `applyAction`; training `Move` uses doubles. Search decode R=5000 continuous targets can diverge from Arena cast-to-int serve path. Normalize both to int targets for champion consistency.  
   - **Track / laps:** Bridge must `setTrack` same global CP expansion as `buildGlobalCp` / `setTrack` (laps × track + return-to-start). Off-by-one on `global_n` desyncs CP pass forever.  
   - **Turn counter:** Arena local `turn` and `game.turn` both mean completed steps after `step`; Encode `turn/kNormTurn` must use the same origin as training Reset (0 pre-first-step).

2. **Terminal contract port vs Arena branch order**  
   - Normative: mirror `Arena::PlayGame` (`arena.cpp` ~142–170) + `csb::Game::checkWinner`.  
   - `checkWinner` already encodes dual-finish draw, single finish, dual/single timeout. Arena then **re-checks** timeout and won flags (partially dead / defensive). Design steps 1–5 restate the same logic twice — high risk of double-draw or max-turns interaction bugs when porting to `terminal.h` over FP fields only.  
   - FP has **no** `checkWinner`/`teamAlive` methods (by design). Shared free functions over `{won,playerTimeout,turn}` must be the **only** terminal owner for EpisodeRunner, league, and search early-exit leaves.  
   - Max turns: Arena uses loop counter `turn >= kMaxGameTurns` (500), not only `game.turn`. Keep one definition.

3. **Simultaneous joint vs sequential apply**  
   - **Arena is correct joint:** both bots `GetActions` on the **same** pre-step view → four `applyAction` → one `step(Fidelity)`. No mid-turn re-observation.  
