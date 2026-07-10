# Observation Features & Training Methods Catalog (Mad Pod Arena)

| Field | Value |
|---|---|
| **Date** | 2026-07-09 |
| **Status** | Draft (brainstorm-approved framing) |
| **Product** | mad_pod_arena — CG Mad Pod Racing / Coders Strike Back |
| **Related** | `docs/artifacts/ZERO_BIAS_SELFPLAY_VALUE_SEARCH_DESIGN.md` (system architecture, PR plan, KD1–KD21) |
| **Brainstorm choices** | Full catalog; CG-parity champion; tiered A; expand history/map; **Path C (search+V) primary** |

---

## 1. Purpose

Produce a **complete menu** of:

1. **Observations / features** the agent might use (raw, engineered, history, map-graph, privileged).
2. **Training methodologies** that could produce a ladder-competitive bot.

So we can **choose the right path** via explicit tiers and experiments—not by inventing more `BotConfig` weights.

This document is a **catalog + decision guide**. The end-to-end system design (env, league, V-search, PRs) lives in the Zero-Bias Self-Play design; this catalog **feeds** that design’s observation v2+ and training ladder.

---

## 2. Constraints (non-negotiable)

| Constraint | Implication |
|---|---|
| **Fidelity SSOT** | Features that assume wrong physics are invalid. Train/eval on EXACT `fast_physics` / `simulateFidelityWorld`. |
| **Champion CG-parity** | Ship path never uses features CG does not provide (or that cannot be reconstructed from agent history of own actions). Opp boost hidden → force 0. |
| **75 ms / turn** (first turn larger) | Observation encode + net + search must fit budget. |
| **Zero strategy bias in reward** | Outcome-first; no “align pretty” rewards as primary. |
| **Simultaneous game** | Both teams act each turn; any MCTS is **simultaneous-move only** (KD21). |

### Feature tiers

| Tier | Meaning | Used on champion ship path? |
|---|---|---|
| **T0** | CG-parity; reconstructible from CG I/O + own action history | **Yes** (default) |
| **T1** | CG-parity but richer / heavier; candidate ablations | Only after league win vs T0 |
| **TR** | Privileged / teacher-only (full sim state, opponent boost truth, future) | **Never** ship; offline teacher / distill only |

---

## 3. Observation catalog

### 3.1 Raw CG frame (T0 base)

Exactly what CG gives each turn (per pod, degrees UI):

| Feature | Notes |
|---|---|
| `x, y` | Map 16000×9000; origin top-left |
| `vx, vy` | After last commit |
| `angle` | Degrees absolute (0 = EAST); first-frame sentinel ~−1 until rotated |
| `nextCheckPointId` | Local track index |
| Own team: 2 pods; Opp: 2 pods | Order fixed by input protocol |
| Init: `laps`, `checkpointCount`, CP coordinates | Static per game |

**Legal bookkeeping (T0, agent-side):**

| Feature | Source |
|---|---|
| Own boost remaining (per-pod Fidelity SSOT) | Agent tracks after BOOST |
| Own shield timer | Agent tracks after SHIELD |
| Team timeout risk (approx) | Inferred from “no CP pass” pressure if exposed; else from Fidelity only offline |

### 3.2 Geometric / relative features (T0 default pack)

Derived from raw; no privileged info.

**Per controlled pod (and optionally relative to each other pod):**

| Group | Features |
|---|---|
| **To next CP** | Δx, Δy, dist, unit dir, angle_error (shortest), signed lateral error, closing speed (vel·û) |
| **To CP+1 / CP+2** | Same as next (lookahead geometry) |
| **Path angle** | Turn angle at next CP (vectors CP−1→CP→CP+1) |
| **Ego frame** | Vel in body frame; speed; facing vs vel slip |
| **Bounds** | Dist to arena edges; out-of-bounds flags (soft) |
| **Progress** | local next, lap estimate, **global_next** (from track size), remaining CPs to finish |

**Inter-pod (all pairs or ego-relative):**

| Group | Features |
|---|---|
| **Relative state** | Δpos, Δvel, dist, approach rate, time-to-closest-approach (ballistic), heading to other |
| **Collision geometry** | Separating vs closing; soft “will hit if both free-flight” flag (analytic, same as TOI style) |
| **Team structure** | Dist between allies; who is “ahead” by global_next |

**Normalization (T0):** divide lengths by map scale (e.g. /16000, /9000), angles as sin/cos, speeds by ~max free-flight scale; clip outliers.

### 3.3 Map / checkpoint graph features (T0–T1, expanded)

User requested deeper map structure. Encode once per game + update progress each turn.

| Feature | Tier | Description |
|---|---|---|
| **CP polyline** | T0 | Ordered (x,y) for up to 8 CPs; pad unused with zeros + mask |
| **Segment lengths** | T0 | \|CP_i → CP_{i+1}\| including wrap |
| **Turning angles** | T0 | Exterior/interior angle at each CP |
| **Cumulative race length** | T0 | Sum of segment lengths × laps |
| **Remaining race length** | T0 | Along-track distance from current global progress |
| **Next segment unit + curvature proxy** | T0 | û_seg, angle change next-next |
| **Graph Laplacian / adjacency** | T1 | For GNN-style encoder over CPs as nodes |
| **CP embedding table** | T1 | Learned id embedding (map-overfit risk; use carefully) |
| **Map fingerprint** | T0 | Hash or sorted stats of CP set for conditioning |
| **“Open space” rays** | T1 | Cast N rays from pod for free distance (expensive) |
| **Optimal free-flight heading (1-step)** | T1 | Geometric pure-pursuit toward next CP (heuristic feature—use only as optional T1, not reward) |
| **Multi-lap phase** | T0 | fraction of race complete |

**Encoder patterns for maps:**

1. **Flat concat** of padded CP list (simple MLP; matches current 70-dim spirit).  
2. **Attention over CPs** (query = pod state).  
3. **GNN on CP graph** (nodes = CPs, edges = race order).  
4. **Implicit map**: no explicit CPs beyond next-3 only (weaker transfer).

**Recommendation:** T0 = flat CP list + segment lengths + turn angles + remaining length; T1 = attention/GNN ablation.

### 3.4 History / multi-turn features (T0–T1, expanded)

| Feature | Tier | Description |
|---|---|---|
| **Last K positions** (K=2..8) | T0 | Ego + others; stack or delta-pos |
| **Last K velocities** | T0 | Finite-difference check vs reported vel |
| **Last K actions** (own) | T0 | Target bin / thrust / shield/boost one-hots |
| **Last K angle errors** | T0 | Did we track target? |
| **Collision events** | T0 | Binary “bounced this turn” proxy from vel jump |
| **CP pass events** | T0 | Edge when next_cp increments |
| **Timeout clock** | T0 | Turns since team last CP (if known) or proxy |
| **RNN / transformer memory** | T1 | Learned history instead of fixed stack |
| **Opp action inference** | T1 | Δpos-derived guessed thrust/turn (noisy) |
| **Long-horizon trajectory embedding** | T1 | Autoencoder of last 20 turns |

**Recommendation:** T0 = last 3 turns of (pos, vel, own action) for all pods + event flags; T1 = GRU/transformer.

### 3.5 Action / mask features (T0)

| Feature | Notes |
|---|---|
| Legal BOOST mask (per-pod) | From agent-tracked remaining boosts |
| Legal SHIELD mask | Always legal unless product rule forbids |
| Discrete action bin embedding | If using 105-bin grid (angle × thrust × special) |
| Search prior logits (online only) | π(a\|s) for GA mutation bias—not in pure reactive |

### 3.6 Privileged / teacher (TR only)

| Feature | Why TR |
|---|---|
| True opponent boost remaining | Not in CG input |
| Exact `playerTimeout` both teams | Not always identical to CG line protocol |
| Future bounce outcomes under simultaneous joint actions | Requires tree; not a single observation |
| Ground-truth TOI from Fidelity internals | Implementation detail |
| Opponent network hidden state | Only if multi-agent with shared train |
| Ideal continuous action from oracle search | Labels for distillation |

**Teacher use:** train V_teacher(s_full) → distill to V_student(s_parity); ship student only.

### 3.7 Feature groups to **avoid** on champion path

| Anti-feature | Why |
|---|---|
| Hand role id (runner/blocker forced) | Strategy bias |
| BotConfig weight vector | Human prior |
| GA evaluate_state score as input | Circular human fitness |
| Absolute pixel without norm | Scale issues |
| Future opponent actions | Cheating |
| Team-shared boost flag contradicting Fidelity | SSOT violation (KD13) |

### 3.8 Default champion observation (v1 → v1.1)

**v1 (existing design, obs_dim=70):** team-relative packs for 4 pods + track summary + masks (see Zero-Bias design Encode).

**v1.1 (this catalog’s recommended expansion, still T0):**

| Block | Content | Approx dims |
|---|---|---|
| Pods (×4), team-relative | pos, vel, sin/cos angle, has_rotated, next, shield_cd, own_boost, progress | ~48 |
| Map graph | up to 8 CPs xy + segment lens + turn angles + remaining length + mask | ~40 |
| History K=3 | stacked Δpos/Δvel/own actions for self pods; coarse opp Δpos | ~36 |
| Global | turn, laps, timeout_proxy, map fingerprint | ~8 |
| **Total order** | ~130 before compression | — |

**Ship options:**

- Keep **70-dim MLP** if amalgam size-bound.  
- Or **compact v1.1** (~96–128) with int8 weights.  
- Or **two-tower**: map encoder (once) + pod encoder (per turn).

### 3.9 Observation design principles

1. **Reconstructible under CG-parity** for anything marked T0.  
2. **sin/cos** for angles; never raw degrees alone.  
3. **Team-relative** coordinates for value symmetry (swap teams → swap view).  
4. **Versioned schema** (`obs_schema_version`); golden vectors in tests.  
5. **Ablate in blocks** (history off, map-graph off) via league—not one feature at a time forever.

---

## 4. Training methods catalog

### 4.1 Method cards

#### M1 — Hand heuristic + GA (status quo)

| | |
|---|---|
| **What** | Horizon search with `BotConfig` / `ga_pure` leaf |
| **Pros** | Works today; 75 ms proven culture |
| **Cons** | Human ceiling; roles/force_boost bias |
| **Use** | League **anchor** (`ga_baseline_v1`), not research endgame |

#### M2 — Behavioral cloning from battles

| | |
|---|---|
| **What** | Supervised map states → actions from CG JSON |
| **Pros** | Fast competence; uses huge battle corpus |
| **Cons** | Meta bias; cannot exceed teachers alone |
| **Use** | Optional warm-start; λ_bc → 0 mandatory |

#### M3 — Self-play RL (sparse / lightly shaped)

| | |
|---|---|
| **What** | PPO/V-trace; reward win/loss (+ optional Φ progress) |
| **Pros** | Discovers non-human styles; matches “zero bias” story |
| **Cons** | Sample-hungry; long races; may stall without ladder |
| **Use** | Core of offline learning after env ready |

#### M4 — Search + learned value / prior (**Path C — primary**)

| | |
|---|---|
| **What** | Online: GA shell or **simultaneous MCTS** on `fast_physics`; leaf = V_θ(s); mutations biased by π_θ |
| **Pros** | Best of planning + learning; reuses search culture; strong under 75 ms if T_step allows |
| **Cons** | Train/serve must stay EXACT; budget engineering (KD14, latency table) |
| **Use** | **Default path to “best bot”** |

#### M5 — Pure reactive policy (no search at deploy)

| | |
|---|---|
| **What** | π only; two lines out |
| **Pros** | Lowest latency; small amalgam |
| **Cons** | Weaker if search opponents exist |
| **Use** | Distillation fallback (A4) if search misses p99 |

#### M6 — Evolutionary strategies on weights

| | |
|---|---|
| **What** | ES on π/V params or on BotConfig |
| **Pros** | No backprop through game; robust |
| **Cons** | Sample inefficient; BotConfig ES preserves human features |
| **Use** | Fallback if V-trace stack fails (OQ-RL1) |

#### M7 — Population / league learning (PFSP, PBT)

| | |
|---|---|
| **What** | Pool of checkpoints; prioritize opponents you lose to |
| **Pros** | Handles non-transitivity (race vs block) |
| **Cons** | Storage + eval cost |
| **Use** | Required companion to M3/M4 |

#### M8 — Privileged teacher + distill (TR → T0)

| | |
|---|---|
| **What** | Teacher sees full state / deeper search; student CG-parity |
| **Pros** | Can teach hard tactics without shipping cheats |
| **Cons** | Two models; distill gap |
| **Use** | Optional accelerator after Path C baseline |

#### M9 — Offline RL on battle + self-play logs

| | |
|---|---|
| **What** | CQL/IQL-style on logged trajectories |
| **Pros** | Reuse data offline |
| **Cons** | Distribution shift; weaker than online self-play for this domain |
| **Use** | Secondary; not primary |

#### M10 — Simultaneous MCTS-only (no GA)

| | |
|---|---|
| **What** | PUCT simultaneous expansions; π prior + V |
| **Pros** | Princip principled for simultaneous games |
| **Cons** | Branching factor; engineering harder than GA shell |
| **Use** | PR-11 after GA+V works; form locked **simultaneous only** |

### 4.2 Path recommendation (updated)

| Path | Composition | Role |
|---|---|---|
| **Path C (primary)** | M4 + M7 + ladder M3 for V/π | **Default to best bot** |
| Path B | M2 → M3 → M4 | If V learning cold-starts fail |
| Path A | M3 pure, late M4 | Research purity; slower |
| Anchor | M1 always in league | Regression |

**Order of build (aligned with Zero-Bias PRs):**

1. Env + T0 Encode + league harness  
2. Train V/π (M3, optional M2)  
3. **ValueSearchBot** with `fast_physics` (M4) as soon as V exists  
4. PFSP population (M7)  
5. Optional simultaneous MCTS (M10)  
6. Optional teacher distill (M8)

### 4.3 Learning ladder (how we “figure out the right path”)

| Stage | Experiment | Pass if | Else |
|---|---|---|---|
| L0 | Sparse reward vs random, 1 map | WR ≫ 0.5 | Enable Φ progress |
| L1 | Self-play map 0, then 18 maps | Beats random on 18 | BC warm-start M2 |
| L2 | **V-search vs GA baseline @75 ms** | Wilson promotion protocol | Tune budget / horizon / pop; measure T_step |
| L3 | PFSP population | Non-transitive robustness | Expand panel / holdout maps |
| L4 | Ablate history / map-graph | Keep only if WR↑ | Revert to smaller T0 |
| L5 | Simultaneous MCTS vs GA shell equal time | Adopt if better p50 WR | Keep GA shell |
| L6 | CG canary | No regression vs GA on live | Rollback amalgam |

### 4.4 Ablation matrix (pathfinder)

| Ablation | Question answered |
|---|---|
| No history vs K=3 | Is memory needed? |
| Next-CP only vs full map graph | Is topology needed? |
| Reactive π vs V-search | Does search carry the win? |
| BC-on vs from-scratch | Does imitation help long-run? |
| Φ on vs sparse only | Is shaping required? |
| cg_parity vs privileged teacher | Does teacher distill help student? |
| GA shell vs simultaneous MCTS | Which planner fits 75 ms? |

Each ablation: **same league protocol** (18 maps × 2 sides × R, Wilson LB rule)—no vibes.

---

## 5. Architecture sketch (catalog view)

```text
CG / Arena view ──Encode T0──► π, V
                      │
                      ▼
              Search (GA shell or sim. MCTS)
                      │  rollouts = fast_physics
                      ▼
                 leaf = V(s')   prior = π
                      │
                      ▼
                 best actions → CG output

Offline: self-play + PFSP (+ optional BC) train π,V
League: promote only if Wilson LB vs panel @75ms
```

---

## 6. Success criteria

| Level | Criterion |
|---|---|
| Catalog done | T0/T1/TR features listed; methods listed; Path C primary |
| Plumbing | Encode golden + episode runner + league harness |
| Research | V-search beats `ga_baseline_v1` under promotion rule |
| Ship | p99 ≤75 ms; amalgam; GA rollback; cg_parity |

---

## 7. Non-goals (this catalog)

- Implementing networks or search in this doc  
- Changing physics constants  
- Claiming one magic feature (“lateral only”) without league proof  
- Turn-based MCTS  

---

## 8. Relationship to Zero-Bias system design

| Topic | Authority |
|---|---|
| Env, spawn, terminal, league k/R/Wilson, PR plan, KD1–KD21 | `ZERO_BIAS_SELFPLAY_VALUE_SEARCH_DESIGN.md` |
| Full feature menu, method menu, Path C priority, history/map expansion | **This document** |
| Conflict | System design wins on infra; this catalog wins on **what to ablate next** in obs/method space |

Recommended: freeze **Encode v1 (70)** for PR-1; schedule **v1.1 history+map** as PR-1b or post-first V-search win.

---

## 9. Open choices (defaults)

| Choice | Default |
|---|---|
| Champion path | **Path C** (search+V) |
| Obs ship | v1 70-dim; expand v1.1 after L2 |
| History | K=3 stack before RNN |
| Map | Flat graph stats before GNN |
| Planner | GA shell first; simultaneous MCTS second |
| BC | Off unless L1 fails |
| Privileged | Teacher-only, never ship |

---

## 10. Next step after approval

Invoke **writing-plans** for an implementation plan that:

1. Locks Encode v1 + golden tests  
2. Adds catalog-driven ablation flags for history/map  
3. Prioritizes ValueSearchBot (Path C) immediately after first V checkpoint  

---

*End of brainstorm design catalog.*


---

## Amendment v1.1 — Multi-critic upgrades (2026-07-09)

*Integrates five specialist critiques. Overrides earlier “expand dims before Path C” where they conflict.*

### Path priority (confirmed + strengthened)

**Path C remains primary**, but training data for V/π used in search **must include Expert Iteration games** (search-in-loop), not only reactive self-play. See KD22 in system design rev 3.2.

### Observation ship policy

| Stage | Schema | When |
|---|---|---|
| **Ship freeze** | **v1 obs_dim=70** (system design Encode) | Through first amalgam p99 pass |
| **Post-ship v1.1** | Geometry + history + map graph (~100–110 dims recommended by obs critic) | Only if league ablation shows gain |
| **T1** | GNN/RNN/rays | Research |

### Recommended v1.1 block order (ablation)

1. **Parity fix** — zero opp shield/boost/timeout leaks  
2. **Ego–next-CP geometry** — dist, angle_err, closing speed, lateral  
3. **CP+1 / path curvature**  
4. **Remaining race length**  
5. **Pair approach** (ally, lead opp)  
6. **Map segment lengths + turn angles + validity mask**  
7. **History K=3** (Δpos, own actions)  
8. T1 attention/GNN  

### Features marked harmful if misused

| Feature | Risk |
|---|---|
| Opp shield_timer from sim | Privilege leak vs CG |
| Raw playerTimeout | Not on CG pod lines |
| Zero-pad CPs without mask | Spurious geometry |
| Opp boost in dynamics zeroed | Corrupts physics — mask obs only |

### Methods catalog addenda

| ID | Addendum |
|---|---|
| M4 Path C | **Requires** Expert Iteration data mix (search plays) |
| M3 | Pod-conditioned advantages (KD23) |
| M7 | External anchors + holdout; not only PFSP soup |
| M8 Teacher | Allowed; distill to T0 only |
| Latency | int8 weights; capacity formula; N_min tripwire → reactive distill |

### Pathfinder ladder update

| Stage | Change |
|---|---|
| L1 | Add vs pure-racer / blocking competence, not only random |
| L2 | V-search trained with **search-in-loop data** |
| L2b | Apply-move EXACT soak on SP distribution |
| L4 | Obs ablations only after L2 pass |
| L6 | Ship: paste size + p99 + GA rollback + holdout if available |

---
