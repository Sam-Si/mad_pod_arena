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

| | |
|---|---|
| **Severity** | **Critical** for role emergence (blocker/runner, sacrificial shield, intentional ram) |
| **Where** | Zero-Bias KD16; Credit assignment §; Action space “two independent heads (not full joint 11k)”; Catalog zero-bias / no roles |

**What the docs claim:**

> “Centralized team value V(s) … Both pods’ discrete actions share the **same team advantage** (joint team reward).”  
> “Fancy counterfactual multi-agent baselines deferred (OQ-RL9).”

**Why it kills best-bot:**

CSB’s interesting strategies are **asymmetric within the team**: one pod races (progress), one contests (collisions, shields, angle denial). Under **identical advantage A_t for both pods**:

- The racer’s thrust/angle and the blocker’s ram get the **same learning signal**.
- A pod that **does nothing useful** still rides the teammate’s progress when Φ or terminal win arrives → **lazy agent / free-rider**.
- Factorized π (105×105 independent) cannot represent **joint** coordination except indirectly through the shared critic; without a counterfactual baseline, gradients for the sacrificial pod are pure noise relative to the race pod.

Pod-swap augmentation (p=0.5) helps **permutation symmetry**, not **credit separation**. It can even **hurt** by teaching the net that pods are interchangeable when optimal play is specialized mid-race.

Simultaneous opp actions make the transition P(s'|s,a0,a1,b0,b1) extremely noisy; a single scalar team advantage **attributes environmental and opponent variance to both pods**.

**Suggested fix:**

1. **v1.1 critic (do not ship champion on KD16 alone):**  
   - **COMA-style** or **leave-one-out** counterfactual baseline per pod:  
     `A_i = Q(s, a_i, a_{-i}) − Σ_{a'_i} π_i(a'_i) Q(s, a'_i, a_{-i})`  
     with centralized Q or sampled baselines.  
   - Or cheaper: **difference rewards** / **shaped per-pod potentials** only as *baselines*, not as primary reward: e.g. advantage residual after regressing team return on teammate action.

2. **Two-level policy (OpenAI Five–adjacent, lighter):**  
   - High-level: discrete **role / intent** (race, contest, intercept, idle-safe) with team Q;  
   - Low-level: factorized continuous/discrete control conditioned on intent.  
   Roles **emerge from a learned discrete latent**, not from `SetRoles` — preserves G5 spirit.

3. **Minimum patch if timeline is brutal:**  
   - Per-pod value heads `V0, V1` with **team return still shared**, but **GAE computed with a learned baseline that includes the other pod’s action** (centralized critic, decentralized actors — CTDE done properly).  
   - Log **per-pod contribution proxies** (Δglobal_next, collision impulse involving pod, timeout proximity) as **diagnostics**, not rewards; alert if one pod’s action entropy collapses while the other carries WR.

4. **Keep joint reward for the environment** (±1 team win) — the bug is the **baseline**, not the team objective.

---

### C3 — Sparse ±1 over race length with weak ladder: learning signal is mostly luck until shaping, then shaping may mis-specify multi-agent optima

| | |
|---|---|
| **Severity** | **Critical** for sample efficiency / whether L0–L2 ever produce a search-worthy V |
| **Where** | Reward design; Learning ladder L0–L3; γ=0.997, GAE-λ=0.95; Catalog L0–L1 |

**Facts grounded in repo:** timeout 100 turns without CP progress; max game 500 turns; real races often **~80–250** turns. With γ=0.997:

- γ^100 ≈ 0.74, γ^200 ≈ 0.55, γ^400 ≈ 0.30 — terminal ±1 is **not** zero but is **heavily delayed** and shared across hundreds of joint actions (2 pods × ~105 bins × opp noise).

**Why it kills best-bot:**

1. **L0 (WR≥0.70 vs random on map 0)** mostly measures **“can you drive toward checkpoints?”** Random opps barely contest. A policy can pass L0 with **no blocking competence, no shield skill, no boost skill.** That V is a **racing progress estimator**, not a simultaneous 2v2 value.

2. **L1 (WR≥0.45 vs ga_baseline_v1 @75ms)** is the first real multi-agent gate — and it is **likely impossible** for early reactive RL without BC/Φ, **or** achievable only by **exploiting a narrow GA hole** that does not transfer. The ladder’s “on failure → enable BC / Φ” is correct operationally but **does not define what competence means**.

3. **Φ = max_{pod} global_next** (Ng-style):  
   - Preserves optimal policy only in a **stationary single-agent MDP** sense; self-play is **non-stationary**. Φ still **reshapes the equilibrium set** you find.  
   - **max** progress **ignores the second pod** for shaping: a pure runner + dead weight is **preferred by Φ** over a slightly slower runner + effective blocker **until terminal**.  
   - No timeout potential: agents can learn to greedily take CPs while sitting on 2-turn timeout clocks.

4. GAE-λ=0.95 over 200 steps with sparse rewards → **high variance**; V-trace helps actor lag, **not** sparse multi-agent variance.

5. Catalog and Zero-Bias both treat Φ as optional and principled; **neither specifies multi-agent-safe dense signals** (timeout margin, relative race position, contested CP races).

**Suggested fix:**

- **Redesign ladder as competence curriculum, not only WR thresholds:**  
  - L0: progress / finish race vs **static ghost** or **scripted pure racer** (not “random legal,” which includes suicidal shields).  
  - L0b: vs **scripted blocker** that only rams.  
  - L1: vs GA @75ms with **per-map** WR floors (not one pooled 0.45).  
  - L2: PFSP only after **search-consistent** training starts (tie to C1).

- **Shaping menu (still “zero strategy bias” if outcome-primary):**  
  - Φ_team = α·max(next) + β·min(next)  (β≪α) so trailing pod isn’t free.  
  - Φ_timeout = f(playerTimeout) potential (Ng form on remaining timeout).  
  - Optional **relative race potential**: max_own_next − max_opp_next (zero-sum shaped; strong, but **explicitly zero-sum**, not “align pretty”).  
  - Always: **terminal override** as already specified; anneal shaping coefficients to 0 and **require ablation** (already partially there — make anneal mandatory for champion).

- **Return estimation:** for terminal-only phases, prefer **Monte Carlo episode returns** (or V-trace with λ→1 on done episodes) over short-horizon GAE when r_t=0 almost everywhere; keep γ close to 1 or use **episodic undiscounted** returns for V targets (standard in AlphaZero: z ∈ {-1,0,+1}).

- **AlphaZero-style target:** V predicts **game outcome z**, not heavily discounted sum of zeros + attenuated z — document explicitly if you keep γ<1 for GAE stability.

---

### C4 — Non-transitivity: PFSP-as-written will not discover or preserve the RPS of race / block / shield

| | |
|---|---|
| **Severity** | **Critical** for “best bot” vs a field (not just vs ga_baseline_v1) |
| **Where** | PFSP 40/40/20; Population N=16; Collapse detector; League panel k=5; Catalog M7; KD3 |

**What the docs claim:**

> Opponent sampling: 40% latest, 40% historical weighted by (1−win_rate), 20% ga_baseline / frozen battle agents.  
> Collapse: entropy < 0.05 nats **and** win matrix near-diagonal → raise entropy / PFSP hard fraction.

**Why it kills best-bot:**

1. **(1−WR) weighting only samples “opponents that beat me lately.”** That is **one** PFSP ingredient. AlphaStar also needed **league exploiters**, **main exploiters**, and **diversity / forgotten strategies** so the agent does not spin on a local cycle or forget how to beat old styles.

2. **20% permanent GA** is a double-edged anchor: great for not regressing vs the ship baseline; **dangerous** as a large fraction of training mass — the meta becomes **“beat BotConfig GA”**, which is a **fixed, highly biased style** (roles, force_boost, heuristic leaf). You can climb Wilson vs GA while remaining **weak to human / LB non-GA styles** and to your own forgotten checkpoints.

3. **N=16 frozen checkpoints** is small for a continuous non-transitive game with map×side structure. Cycles of length > few will **overwrite** rare but essential counters.

4. **Promotion panel** includes `ckpt_hard` = lowest WR vs subject and `ckpt_median` — selected **as a function of the subject**. That is useful stress, but **adaptive panels** without a frozen external set allow **promotion of niche exploiters** that beat the adaptive soup yet fail on a **fixed holdout population**.

5. **No explicit non-transitivity objective:** win matrix is only a **collapse detector**, not a training target (no Nash / NashConv / regret, no “must beat all historical mains above floor”).

6. Self-play actors use **reactive π vs frozen π**, never **search vs search** or **search vs GA** during learning (until league eval). Non-transitive **search tactics** (open-loop rams) never enter the population as first-class citizens.

**Suggested fix (AlphaStar-shaped, CSB-scaled):**

| League slot | Role |
|---|---|
| **Main** (1–3) | Current candidates; train mostly vs league |
| **Main exploiters** | Train **only** to beat mains; frozen when mains update; never promote to ship alone |
| **League exploiters** | Train vs full historical mix; discover forgotten strategies |
| **Anchors** | `ga_baseline_v1` **+** 2–4 **frozen LB-cloned** or past champions (fixed forever) |
| **PFSP** | Sample props ∝ `f(WR)` with **prioritize unsolved** (WR in (0.2,0.8)) not only (1−WR); mix **uniform over neglected** branches of the win graph |

- Cap **GA fraction at ≤10%** after L1; use GA as **promotion secondary gate**, not 20% of all gradients forever.  
- Maintain a **frozen evaluation population** (size ≥32) disjoint from adaptive PFSP for promotion.  
- Log **cycle metrics**: rock-paper-scissors triples among top-K; if cycle strength ↑ while vs-GA ↑, **block ship**.  
- Optionally **condition** a small fraction of training on **map clusters** (tight hairpins vs open) so map×strategy interaction is not averaged away.

---

## Major improvements

### M1 — GA shell + V is not “policy improvement”; treat it as trajectory optimization with a learned heuristic

| | |
|---|---|
| **Severity** | Major |
| **Where** | ValueSearchBot v1 “single timer-capped GA shell”; KD5; Catalog M4 vs M10 |

**Issue:** AlphaZero’s improvement guarantee (informal) relies on **tree search + visit counts + value backup** under a **correct dynamics model**. A GA over open-loop sequences scored by V(leaf) is **stochastic local search in plan space**. It can:

- overfit to **opp model error** (open-loop opp sample ≠ closed-loop opp),
- prefer plans that reach **high-V leaves that are off-policy fantasies**,
- and **drop legacy multi-stage IBR** (doc explicitly simplifies away 4-stage IBR) — the old bot’s partial answer to simultaneous move coupling.

**Fix:**

- Document ValueSearchBot as **open-loop plan opt + V**, not “learned AlphaZero.”  
- Reintroduce **at least 2-stage IBR** (own plan given opp plan; then opp response) before claiming simultaneous competence.  
- For MCTS path (PR-11): specify **factored simultaneous PUCT** (or decoupled planning with alternating best responses inside the tree) and **how opp actions are expanded** — “joint or factored” is hand-wavy at 105² team actions.  
- Add **opponent robustness**: evaluate each candidate with **K opp samples** (min or CVaR of V), not a single draw — otherwise search maximizes V against a ghost.

---

### M2 — Multi-agent non-stationarity: V-trace fixes actor lag, not opponent lag

| | |
|---|---|
| **Severity** | Major |
| **Where** | KD15; trajectory schema; throughput lag K=3 |

**Issue:** ρ-clipping corrects π_learner / π_behavior for **the acting team**. When opp_id changes every game and population updates, the **environment kernel is non-stationary**. Mixing all opps into one V(s) yields an **average-case value** that is wrong for:

- hard exploiters,
- GA’s specific style,
- and online search’s chosen opp model.

**Fix:**

- Condition V (or a hypernetwork bias) on **opp embedding** / `opp_id` hash during training; at deploy use **mixture** or **worst-of-panel** embedding.  
- Or train **quantile / expectile** value (conservative V) for search leaves.  
- Separate **policy training opps** from **value training weights** (more weight on hard opps for V calibration).  
- Explicitly state: **V-trace ≠ solved multi-agent off-policy.**

---

### M3 — Exploration is entropy on factorized heads; joint team tactics are under-explored

| | |
|---|---|
| **Severity** | Major |
| **Where** | Collapse detector ε=0.05 nats; factorized 7×5×3; no intrinsic exploration |

**Issue:** Sum of per-head entropies can look healthy while **joint** (pod0,pod1) mass collapses to “both full thrust to CP.” Coordinated tactics (one boosts, one shields; scissor intercept) have **tiny measure** in 105².

**Fix:**

- Track **joint action entropy** / mutual information between pod heads.  
- Occasional **team-level options** or correlated noise.  
- **Population diversity** bonus (PSRO-style) > raw entropy floor.  
- Intrinsic: count **novel collision geometries** or **timeout-save events** for exploration bonus **only in research**, never as ship reward without anneal.

---

### M4 — Observation v1 (70) freezes away history the catalog correctly wants

| | |
|---|---|
| **Severity** | Major (for opponent modeling & Path C leaves) |
| **Where** | Encode v1 obs_dim=70; Catalog §3.4 history K=3; OpenAI Five memory |

**Issue:** Without history, opp intent (thrust vs shield commit, turn-in) is weakly identifiable from one frame. Search opp models and V both suffer. Catalog recommends K=3; system design freezes 70 for PR-1 — fine for plumbing, **bad if Path C champion ships on v1 forever**.

**Fix:** Schedule **obs v1.1 (history + map graph)** as a **hard research dependency before promotion to “best bot”**, not “after first V-search win.” Search cannot invent information that Encode destroyed.

---

### M5 — League statistics vs training objective mismatch

| | |
|---|---|
| **Severity** | Major |
| **Where** | Wilson LB promotion; train optimizes expected return under PFSP mixture |

**Issue:** Promote on **paired Wilson vs prev_champion** and non-decrease vs GA. Training never optimizes that statistic. You can improve mean PFSP return while **failing** Wilson vs prev, or pass Wilson by **variance / draw gaming** (draw=0 in WR tests is harsh and can incentivize “never force draws” weirdly).

**Fix:**

- Periodically run **mini-promotion grids** as **RL eval**, not only offline ship gates.  
- Consider **win−loss** scoring (draw=0.5) for a secondary train metric aligned with Elo.  
- Add **calibration**: predicted V at root vs empirical WR on league games.

---

### M6 — BC + self-play without search distillation path is underspecified for Path C

| | |
|---|---|
| **Severity** | Major |
| **Where** | M2/M8 catalog; A4 distillation only if 75 ms fails |

**Issue:** A4 distillation is framed as **latency fallback**. The more important distillation is **search → π** as the **training** method (Expert Iteration), even if deploy keeps search.

**Fix:** Make **search-to-policy distillation** a **first-class Path C stage** (between L2 and L3), independent of amalgam size.

---

### M7 — Simultaneous MCTS branching is not budgeted honestly

| | |
|---|---|
| **Severity** | Major (for PR-11 expectations) |
| **Where** | KD21; PR-11; Catalog M10 |

**Issue:** Per-pod 105 actions ⇒ team joint 105² ≈ 11k, both teams ~10^8 joint pairs — impossible. “Factored simultaneous” needs a **concrete algorithm** (e.g. decoupled UCT, sequential move ordering inside a simultaneous ply with pass, policy-prior top-k×top-k). Without it, PR-11 is a research fantasy while GA shell remains default — fine, but **don’t market MCTS as the principled fix** until the factorization is written.

**Fix:** Write the simultaneous expansion algorithm and complexity before PR-11; until then Path C = **IBR-GA + V + EI data**.

---

### M8 — L2 gate is too weak; L3 secondary GA gate can dominate the objective

| | |
|---|---|
| **Severity** | Major |
| **Where** | Ladder L2 WR≥0.55 vs “random pool median”; KD17 secondary vs GA |

**Issue:** Beating “random pool median” is cheap once racing works. Secondary non-decrease vs GA with ε=0.01 will **drive the research program** toward anti-GA. That’s good for shipping past current bot; **insufficient** for global best.

**Fix:** L2 must include **holdout frozen agents** (old champions, LB clones). Primary research gate: **NashConv-like** or **minimum WR vs all anchors ≥ τ**, not only Wilson vs prev.

---

## Minor / nits

1. **γ=0.997 vs AlphaZero z-targets:** Pick one story (discounted RL vs outcome regression) and align V loss, plots, and search backups.  
2. **Draw reward 0 / WR draw=0:** Document that high-level play may draw rarely; if draws rise, gradients vanish — track draw rate.  
3. **Trajectory schema** has `bootstrap_v` but no `opp_logp` / joint density — fine for team-centric, but blocks future off-policy multi-agent estimators.  
4. **Entropy threshold 0.05 nats** on sum of factorized heads is arbitrary; calibrate on a random policy baseline (~log 105 per pod).  
5. **Population Elo in registry** without specifying rating system (TrueSkill vs Elo, map clustering).  
6. **Catalog Path B/C order** still allows “train V then search” without EI — contradict C1; catalog should mark **EI required for Path C**.  
7. **Privileged teacher (M8)** without search is still the wrong teacher; teacher should be **deeper search / more compute**, not only full boost bits.  
8. **Rules.md team-shared boost** vs KD13 per-pod Fidelity — training correctly follows code SSOT; keep a **parity test** so BC from JSON doesn’t reintroduce folklore.  
9. **First-turn 1000 ms** vs train-time search budget: use extra train compute; don’t claim train=serve budget.  
10. **SPS gates (10k env, 2k with net)** may be optimistic with full `fast_physics` — not RL-theoretic, but will starve sample complexity fixes above.

---

## Concrete design text to add (bullet patches)

Paste/adapt into Zero-Bias design (and cross-link catalog Path C):

### Patch set A — Path C closed loop (new KD22)

- **KD22 — Expert Iteration for Path C:** Champion π/V training data for L2+ is generated by **ValueSearchBot (or sim-MCTS) self-play** under train-time budgets. Reactive-only actors are restricted to L0–L1 cold start. Deploy search operator ⊇ train search operator (same dynamics, same Encode, same leaf definition).  
- **Training targets:**  
  - `z` = episode outcome from the acting team’s perspective ∈ {+1,0,−1};  
  - `π*` = normalized search policy / best-first-action distribution at root;  
  - Loss: `L = (V(s)−z)² − Σ_a π*(a) log π_θ(a|s) + c_H H[π_θ]` (factorized factorization of π* as needed).  
- **Forbidden as champion path:** train V solely on reactive GAE, then enable `CSB_USE_LEARNED_VALUE` without a search-play fine-tune stage.  
- **PR-7 acceptance addendum:** leaf calibration curve V vs empirical winrate on ≥N search leaves; Spearman ρ ≥ ρ_min; else block default-on V leaf.  
- **Risk table add:** “Policy/value distribution shift (reactive train, search serve) | Critical | KD22 EI / reanalyze.”

### Patch set B — Credit assignment (replace OQ-RL9 default)

- **KD16′ (amend):** Centralized **team** reward retained. **v1 critic is CTDE with per-pod advantages** via counterfactual/leave-one-out baseline (COMA-lite). Shared-advantage-only is **prototype-only**, not ship.  
- Log free-rider metric: fraction of wins where min_pod progress < threshold.  
- Pod-swap aug remains, but **disabled** in positions where pods are role-differentiated by a learned intent head (if used).

### Patch set C — Reward / ladder

- L0 opponent := `scripted_racer_v0` (full thrust pure pursuit), not uniform random legal. Success: finish rate ≥ 0.9 and WR≥0.7.  
- Add L0b vs `scripted_blocker_v0`.  
- Φ default remains off; if on:  
  `Φ = a·max(next) + b·min(next) + c·Ψ(timeout)` with documented (a,b,c) and **mandatory anneal to 0** before L3.  
- Optional zero-sum potential `max_own_next − max_opp_next` as research flag `CSB_RL_REWARD_RELATIVE`.  
- V target preferred form for sparse phase: **MC outcome z** (AlphaZero-style); GAE reserved for dense-shaping phases.

### Patch set D — League / PFSP (AlphaStar-lite)

- Population structure: `{mains, main_exploiters, league_exploiters, anchors[]}`.  
- PFSP weights prioritize opponents with WR∈[0.2,0.8] vs current main; 10% uniform historical (forgotten); **≤10% anchors** after L1.  
- **Frozen eval population** (size≥32) for promotion; adaptive PFSP never alone decides ship.  
- Training mix after L1: **≥50% games involve at least one search-powered agent** (self or opp).  
- Collapse detector: add **joint** pod-head MI and **cycle strength** on top-K win graph.

### Patch set E — Search operator honesty

- ValueSearchBot v1.1: restore **2-stage IBR** (own/opp) under timer split; score each candidate with **K≥4 opp rollouts** (mean or CVaR).  
- Search backup: if mid-horizon non-terminal, leaf = V; if shaping active in train, **search must use the same Φ** or none — no train/serve reward skew.  
- PR-11 prerequisite: publish factored simultaneous MCTS algorithm + top-k prior expansion complexity under 75 ms.

### Patch set F — Catalog Path C one-liner fix

Replace Path C composition with:

> **Path C (primary) = M4 search operator + M7 league + Expert Iteration (search-generated data) training π/V; M3 reactive self-play is warm-start only.**

Add ablation row:

| Ablation | Question |
|---|---|
| Reactive-trained V leaf vs EI-trained V leaf | Does search data fix leaf calibration? |

### Patch set G — “What we are not claiming”

- We do **not** claim AlphaZero policy improvement until KD22 holds.  
- We do **not** claim AlphaStar-level non-transitivity handling until exploiters + frozen eval population exist.  
- We do **not** claim OpenAI Five–level credit assignment until per-pod counterfactual baselines ship.

---

## What AlphaStar / OpenAI Five / AlphaZero would do differently (concise)

| System | Their answer to this game | Gap in current design |
|---|---|---|
| **AlphaZero** | Self-play **only through search**; π matches search; V fits outcomes of search games; improve ↔ evaluate closed loop | Reactive M3 trains V; search bolted on at PR-7 |
| **AlphaStar** | League with **mains + exploiters**; PFSP + forgotten strategies; huge population; supervised init then RL; no reliance on one GA anchor | Flat checkpoint PFSP; 20% GA; N=16; no exploiters |
| **OpenAI Five** | Team reward but **massive scale PPO**, long memory (LSTM), careful **reward shaping anneal**, true continuous self-play vs latest; pure policy deploy | Tiny net, no history in v1, shared advantage, sparse-first hope, search deploy without Five-style shaping discipline |
| **All three** | Eval is a **first-class training signal** (continuous rating vs diverse pool), not an offline Wilson ceremony after the fact | League is ship-gate; mini-league not in actor loop |

**Most important single transplant for mad_pod_arena:**  
**AlphaZero’s data path (search plays the games that train V/π)** + **AlphaStar’s league structure (exploiters + anchors)** + **OpenAI Five’s honesty about shaping/credit (anneal + better baselines)**.

---

## Bottom line

Ship the **env / Encode / league harness** with confidence.  
Rewrite Path C training so that **the policy that generates data is the policy family you deploy** (search-in-the-loop).  
Fix **per-pod credit** and **league non-transitivity** before claiming zero-bias self-play will exceed a well-tuned GA.

Without those, the most likely “success” is: **a reactive racer that slightly beats `ga_baseline_v1` on catalog maps under Wilson noise**, then a **ValueSearchBot that is slower and not smarter** — the classic leaf-V mirage.

---

*End of RL Critic Report.*
