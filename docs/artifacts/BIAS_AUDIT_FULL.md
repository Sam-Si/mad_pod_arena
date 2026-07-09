# Full bias audit — every design choice as bias or not

**Date:** 2026-07-09  
**Scope:** Zero-Bias Self-Play design (rev 3.2), obs/training catalog, Path C, Encode v1, league, physics SSOT.  
**Question for each item:** *Is this a bias? What kind? Acceptable for “best bot”?*

### Bias taxonomy (use this vocabulary)

| Code | Kind | Meaning |
|---|---|---|
| **R** | Rules / physics | Inherent to CSB; not “tactic bias,” but shapes optimal play |
| **S** | Structure | Representation / architecture / compute; not human racing style |
| **T** | Temporary prior | Warm-start that should anneal or ablate |
| **H** | Human strategy | Encodes designer taste about *how* to race/fight |
| **D** | Data / meta | Inherits behavior distribution from demos or self-play pool |
| **E** | Evaluation | What we call “better” is warped by the metric |
| **P** | Privileged info | Sees what CG won’t; train/serve or fairness skew |
| **C** | Compute / latency | Suboptimal under budget → systematic style (e.g. short horizon) |
| **Ø** | Not a bias | Neutral plumbing or pure math without strategic preference |

**Acceptable for champion path:** R, S (documented), T (with ablation), C (honest).  
**Forbidden on champion path:** H as reward/roles/features; P on serve.  
**Manage carefully:** D, E.

---

## 1. Physics & environment

| Item | Bias? | Code | Notes |
|---|---|---|---|
| Win = first pod finish race | Yes (unavoidable) | **R** | Defines the objective |
| Timeout elimination (100 turns no CP) | Yes | **R** | Forces progress pressure |
| Max turns 500 → draw | Yes | **R** | Encourages finishing vs stalling |
| Friction 0.85, impulse ≥120, 18° rotate | Yes | **R** | Physics “meta” |
| Pod r=400, CP r=600 | Yes | **R** | |
| EXACT Fidelity world-step only | **No tactic bias** | **S/R** | Anti-bias vs approx physics; *required* honesty |
| Using `fast_physics` vs `csb::Game` if EXACT | Ø if proven equal | **Ø/S** | Implementation dual-use |
| Training on Fast fragment (`csb::fast`) | **Yes — harmful** | **S/P** | Different bounce/CP/timeout → false skill |
| Catalog 18 maps only | **Yes** | **D/E** | Overfits geometry; not full CG disposition |
| Spawn law `initializeFromTrack` | Yes | **R** | Fixed start geometry |
| Simultaneous 4-pod step | Yes | **R** | |
| Laps = 3 default | Yes | **R** | |

**Verdict:** Physics is the *right* bias. Catalog-18 is a **real eval bias** for “best on CG.”

---

## 2. Observation features (Encode v1 = 70)

### 2.1 Per-pod channels

| Feature | Bias? | Code | Critique |
|---|---|---|---|
| x,y normalized | Mild | **S** | Frame choice; team-relative reduces absolute map bias |
| vx,vy / kNormVel | Mild | **S** | Scale saturation if kNormVel wrong → dead features |
| sinθ, cosθ | Mild | **S** | Good; vs raw degrees |
| has_rotated + east placeholder | Mild | **S** | First-frame convention; matches CG sentinel handling |
| next_cp_local / 8 | Mild | **S** | Assumes max 8 CPs |
| next_global / norm | **Progress bias** | **S/H-adjacent** | Steers net toward “ahead is good” even without Φ in reward |
| shield_timer / 4 | **Risk** | **P** if from sim for opp | Own OK if agent-tracked; **opp shield from sim = privilege** |
| boost_available | Own: OK; opp: | **P** if true | KD20 zeros opp in Encode — good |
| opp_boost_known | Dead/confusing | **S** | If always 0 under cg_parity, wastes dim or leaks if not |

### 2.2 Track block

| Feature | Bias? | Code | Critique |
|---|---|---|---|
| CP x,y padded to 8 zeros | **Yes** | **S** | Zero-pad without validity mask → phantom CPs at origin |
| cp_count, laps, turn | Mild | **S** | |
| timeout_own / timeout_opp | **Risk** | **P** | Not on CG pod lines; if from `playerTimeout` = sim privilege |
| team_id 0/1 | Mild | **S** | Breaks pure team-swap symmetry unless both sides train |

### 2.3 Catalog / T0–T1 extras (brainstorm)

| Feature | Bias? | Code | Critique |
|---|---|---|---|
| Angle error to CP | **Mild H** | **S/H** | “Face CP” is human racing prior if over-weighted by architecture |
| Lateral error / closing speed | Mild | **S** | Geometry, not tactics |
| Pure-pursuit optimal heading | **Yes H** | **H** | Explicit human control prior — T1 only, never reward |
| “Will collide free-flight” TOI flag | Mild | **S** | Analytic geometry |
| Remaining race length | Progress | **S** | Similar to global_next |
| GNN / CP embeddings | **Map overfit** | **D** | Can memorize catalog-18 |
| History K positions | Mild | **S** | |
| Opp action inference from Δpos | Mild/noisy | **S** | |
| Teacher full state | **P** | **P** | Offline only |

### 2.4 Normalization & frame

| Choice | Bias? | Code | Critique |
|---|---|---|---|
| Team-relative coords | **Yes S** | **S** | Good inductive bias; hides absolute map side |
| Divide by 16000/9000 | Mild | **S** | |
| Clip outliers | Mild | **S** | Can hide extreme states |
| No rotation-to-track frame | Missing invariance | **S** | Net relearns orientation per map |

**Verdict:** v1 is **not “feature-free.”** Progress channels and timeouts are soft strategy. Privilege channels are the real landmine.

---

## 3. Action space

| Item | Bias? | Code | Critique |
|---|---|---|---|
| Discrete 7 angle bins | **Yes** | **S** | Cannot express fine aim; limits optimal |
| Discrete 5 thrust bins | **Yes** | **S** | Misses 1–199 continuum |
| Special NONE/BOOST/SHIELD | **Yes** | **S** | Matches CG tokens |
| R=5000 target decode | **Yes** | **S/H-light** | Far-point aiming heuristic baked in |
| Independent pod heads (not joint 105²) | **Yes** | **S** | Ignores joint coordination in π; search must fix |
| Residual continuous mutations online | Mixed | **S** | Can leave train support (OOD) |
| BOOST mask per-pod | **Anti-H** | **R/S** | Correct SSOT; *not* rules.md team-share |
| SHIELD always legal | Mild | **R** | |

**Verdict:** Structure bias is **large**. “Zero bias” cannot mean continuous full CG action without huge sample cost.

---

## 4. Reward & learning signal

| Item | Bias? | Code | Critique |
|---|---|---|---|
| +1/0/−1 terminal | **Minimal H** | **R** | Closest to true objective |
| Draw = 0 | Mild | **R** | Treats draw as neutral not loss |
| Φ = max global_next | **Yes** | **H-light** | Prefers one pod racing; KD23 sum/sorted fixes partially |
| Φ = sum/sorted pair | Milder | **S** | Still “progress good” |
| γ ≈ 0.997 long horizon | Mild | **S** | Discount bias toward late vs early contacts |
| No GA score as reward | Good | **Ø** | Avoids classic H |
| No −distance primary | Good | **Ø** | |
| BC log-likelihood | **Yes** | **D/T/H** | Imports LB meta |
| Search policy as target (Expert Iteration) | Mild | **S/D** | Biased toward *what search finds under time* |
| Value bootstrap from search | Mild | **S** | |

**Verdict:** Sparse win is cleanest. Φ and BC are **declared temporary** — still bias until ablated.

---

## 5. Multi-agent / credit / roles

| Item | Bias? | Code | Critique |
|---|---|---|---|
| Shared team reward | **Yes** | **S** | Free-rider without KD23 |
| Factorized π per pod | **Yes** | **S** | Coordination only via V/search |
| No forced runner/blocker | **Anti-H** | **Ø** | Good |
| Roles “emerge” | Hope | **D** | May not emerge without credit fix |
| Pod-swap augmentation | Mild | **S** | Symmetry prior |
| Centralized V | Mild | **S** | |

---

## 6. Training methodologies (each as bias source)

| Method | Bias? | Code | Main contamination |
|---|---|---|---|
| M1 GA + BotConfig | **Heavy H** | **H** | Human fitness |
| M2 BC from battles | **Heavy D** | **D/T** | Season meta, skill mix |
| M3 Self-play sparse | **Self-play D** | **D** | Cycles, non-transitive loops, exploit self only |
| M4 Search+V Path C | **Compute C** | **C/S** | Horizon H, pop, 75 ms → shallow tactics |
| M4 without EI | **Harmful S** | **S** | V wrong under search states |
| M5 Reactive only | **C** | **C** | No look-ahead |
| M6 ES on BotConfig | **H** | **H** | |
| M6 ES on net | Mild | **S** | |
| M7 PFSP / population | Mild | **D/E** | Pool composition bias |
| M8 Privileged teacher | **P** | **P** | OK if distill |
| M9 Offline RL on logs | **D** | **D** | |
| M10 Simultaneous MCTS | **C** | **C** | Branching / time → prior-dependent |
| Expert Iteration mix 50–70% search games | Mild | **S/C** | Search budget shapes policy |
| 10% vs GA baseline in train | **Yes** | **D/H** | Pulls toward beating *our* GA style |
| Random legal opp | Mild | **D** | Easy racing bias early |

---

## 7. Search / online deploy

| Item | Bias? | Code | Critique |
|---|---|---|---|
| Horizon = 6 | **Yes** | **C** | Myopic vs full race |
| Population = 50 | **Yes** | **C** | |
| Single-stage GA shell | **Yes** | **S** | vs full IBR stages |
| Leaf = V | Mild | **S** | Depends on V quality |
| π prior on mutations | **Yes** | **D/S** | Amplifies π modes |
| Emergency GA leaf on timeout | **Yes H** | **H/T** | Reintroduces human eval under stress |
| hard-ban Fast fragment | **Anti-bias** | **Ø** | Good |
| First turn 1000 ms vs 75 | **Yes** | **C/R** | Different policy at t=0 |
| int8 weights | Mild | **S** | Quantization noise |

**Emergency GA leaf (KD28 option)** is a **hidden human-strategy fallback** — document as last resort or use best-so-far only.

---

## 8. Evaluation / “best bot” definition

| Item | Bias? | Code | Critique |
|---|---|---|---|
| Win rate only | **Yes** | **E** | Ignores style diversity, timeout quality |
| Wilson LB ≥ 0.50 vs prev | **Yes** | **E** | Lineage progress ≠ global best |
| vs ga_baseline required | **Yes** | **E/H** | Anchored to our GA |
| Catalog-18 only | **Yes** | **E/D** | Map overfit |
| Same-side swap | Mild | **E** | Good |
| Seeded RNG | Mild | **E** | Reduces noise, not bias |
| No live CG ladder in train | **Yes** | **E** | Sim-best ≠ ladder-best |
| Cluster bootstrap | Anti-noise | **Ø** | Good |
| Holdout maps for ship | Anti-overfit | **Ø** | Good |
| Diversity veto | Mild | **E** | Can reject valid convergence |

**Verdict:** “Best of all bots” is **undefined** without external opponent panel or live ladder. Current design optimizes **sim league vs our anchors**.

---

## 9. Data sources

| Source | Bias? | Code | Critique |
|---|---|---|---|
| Self-play pool | **Yes** | **D** | Non-transitive cycles |
| Battle JSON LB | **Yes** | **D** | Survivor / meta bias |
| Golden/session battles | Mild | **D** | Physics-focused, not peak skill |
| GA as opponent 10% | **Yes** | **H/D** | |
| Random policy | Mild | **D** | |

---

## 10. Key Decisions (KD1–KD30) bias scorecard

| KD | Bias? | Code | One-line |
|---|---|---|---|
| KD1 EXACT train | Anti-H | **R** | Correct |
| KD2 outcome reward | Minimal | **R** | Correct |
| KD3 self-play main | D | **D** | Manage with league |
| KD4 BC optional | T/D | **T** | Ablate |
| KD5 V-search | C/S | **C** | OK |
| KD6 discrete actions | S | **S** | Large coverage hole |
| KD7 no forced roles | Anti-H | **Ø** | Correct |
| KD8 GA in league | E/H | **E** | Anchor bias |
| KD9 18 maps | E/D | **E** | Overfit risk |
| KD10 dual GA rollback | H under fail | **H/T** | Ship safety |
| KD11 physics orthogonal | Ø | **Ø** | |
| KD12 reject approx / GA labels | Anti-H | **Ø** | |
| KD13 per-pod boost | R | **R** | Correct SSOT |
| KD14 FP online | Anti-skew | **Ø** | Correct |
| KD15 V-trace async | S | **S** | |
| KD16 team V | S | **S** | |
| KD17 Wilson promote | E | **E** | Weak alone; KD29 helps |
| KD18 obs 70 freeze | S | **S** | Under-features racing |
| KD19 tiered DoD | Ø | **Ø** | |
| KD20 cg_parity | Anti-P | **Ø** | Must cover shield/timeout |
| KD21 sim. MCTS | R/S | **R** | Correct |
| KD22 Expert Iteration | S/C | **S** | Required for Path C honesty |
| KD23 pod advantage | S | **S** | Anti free-rider |
| KD24 mask obs not state | Anti-P | **Ø** | Critical |
| KD25 ban Fast | Anti-skew | **Ø** | |
| KD26 freeze 70 | C/S | **C** | Delays geometry |
| KD27 int8 paste | S | **S** | |
| KD28 timeout fail-closed | H if GA leaf | **H/C** | Prefer best-so-far |
| KD29 external panel | Anti-E | **Ø** | |
| KD30 apply soak | Anti-skew | **Ø** | |

---

## 11. “Is zero bias possible?” — honest answer

| Claim | Truth |
|---|---|
| Zero **human tactic** bias in reward/roles | **Approximately yes** if Φ off, BC off/ablated, no BotConfig, no force roles |
| Zero **structure** bias | **No** — actions, MLP, horizon, team-rel obs |
| Zero **data** bias | **No** — self-play + any BC |
| Zero **eval** bias | **No** — catalog-18 + our GA anchor |
| Zero **compute** bias | **No** — 75 ms |
| Fair vs CG stdin | **Only if** cg_parity audit passes (boost, shield, timeout) |

**Best achievable:**  
*Minimize H; declare S/C/D/E; eliminate P on serve; ablate T.*

That is a **professional** zero-bias program—not a blank slate.

---

## 12. Ranked bias risks for “best of all bots”

| Rank | Risk | Why it blocks “best” |
|---|---|---|
| 1 | Eval = catalog-18 + our GA | Local optimum in our league |
| 2 | Path C without EI (if ignored) | V lies under search |
| 3 | Privilege leak (shield/timeout) | Train ≠ CG |
| 4 | Discrete action grid | Cannot express optimal continuous aim |
| 5 | 75 ms shallow search | Meta of short horizon |
| 6 | Free-rider credit | No real blocking meta |
| 7 | BC residual after λ→0 | Architecture still BC-shaped |
| 8 | Φ max(global_next) | One-pod racer |
| 9 | Emergency GA leaf | Human eval in production |
| 10 | Zero-pad CPs | Spurious geometry |

---

## 13. What is *not* bias (stop worrying)

- Using Fidelity EXACT  
- sin/cos angles  
- Legal BOOST mask from own state  
- Simultaneous MCTS form  
- Versioned obs schema  
- Truth suite for physics  
- int8 if calibrated  

---

## 14. Design implications (if we take this audit seriously)

1. **Rename G5** in speech: “zero *strategy* bias,” never “zero bias.”  
2. **Require ablations** for: BC, Φ, discrete residual radius, GA emergency leaf.  
3. **Ship metrics** beyond WR: timeout rate, two-pod progress gap, holdout maps.  
4. **cg_parity test suite** as hard gate (channels list §2/§D).  
5. **EI mandatory** before Path C marketing.  
6. **External / live opponents** before “best of all bots” claim.

---

*End of full bias audit.*
