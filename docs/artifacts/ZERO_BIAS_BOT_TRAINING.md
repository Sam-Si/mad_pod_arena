# Zero-bias training for Mad Pod Racing — what you can actually do

**Artifact path:**  
`/Users/samsi/csb/mad_pod_arena/docs/artifacts/ZERO_BIAS_BOT_TRAINING.md`

**Scope:** Ideas and design only — **no code**.  
**Goal:** Train (or arrange training so) an agent that becomes **best-of-field** by **learning from outcomes**, not by baking in human racing myths.  
**Environment you already have:** CodinGame Mad Pod / CSB rules, Fidelity-class physics, battle JSON corpora, GA/search bots as baselines—not as teachers of “the truth.”

---

## 1. What the game really is (zero-bias framing)

Strip slogans. The environment is:

### 1.1 Objective (true win condition)

- **Team of 2 pods.** First team with **any one pod** finishing the required laps wins.
- Secondary failure: **both** pods fail the 100-turn checkpoint timeout → lose.
- Platform also ends on **invalid output**, **timeout**, dual elimination (tooltips).

A zero-bias agent optimizes **P(win | map, opp, rules)** — not “always full thrust,” not “racer + blocker,” unless those emerge from win rate.

### 1.2 Partial observability (critical)

Each turn you observe:

| You see | You do **not** see |
|---|---|
| All 4 pods: x,y,vx,vy,angle°, next CP id | Opponent **commands** (tx,ty,thrust/BOOST/SHIELD) |
| Laps, checkpoint list (init) | Future maps in multi-fight ladder (each fight new map) |
| | Exact server floating path mid-turn (only commit keyframes in replays) |

So **perfect world models of “opp next action” cannot come from stdin alone.** Learning must handle **unknown opp policy**, not just open-loop physics.

### 1.3 Action space (per pod, per turn)

- Continuous-ish target: integers `(tx, ty)` (in practice huge range).
- Discrete thrust channel: `0..200` | `BOOST` (once per **team**/race rules as implemented) | `SHIELD`.
- Physics: max **18°/turn** rotate (first rotate snap), friction 0.85 trunc, pos round, collisions elastic min impulse 120, shield mass×10, boost 650.

**Implication for learning:** raw `(tx,ty,thrust)` is high-dimensional and sparse. Zero-bias does **not** mean “output raw coords from a tiny net with no structure”—it means **no hand-coded strategy**. Structure (discretization, relative targets) is fine if **learned or searched**, not if you hardcode “always aim CP.”

### 1.4 Physics role (your repo)

- **Fidelity / `fast_physics`:** environment dynamics for **training simulators** and search. Must stay **EXACT** to CG so policies don’t overfit sim bugs.
- **Do not** train “the value network to match GA heuristic.” That reintroduces human bias.
- Use physics to: roll out, self-play, label returns—not to copy GA’s score function.

### 1.5 Battles / data you already have

| Corpus | Use for **zero-bias** learning | Bias risk |
|---|---|---|
| Leaderboard JSON (actions of top bots) | Offline imitation / offline RL **with care** | Strong bias toward **current** meta |
| Test_session / golden | Physics validation only | Not strategy truth |
| Your share-replays | Debug, not population of policies | Tiny, personal style |
| Self-play only | Lowest strategy bias | Needs diverse population / maps |

**Zero-bias default:** treat LB as **optional** warm-start or adversary library, not as the reward label of “good play.”

---

## 2. What “zero bias” can mean (honest)

### 2.1 Achievable definition

**Zero human strategy bias:** no racer/blocker roles, no fixed BOOST timing rules, no “always shield on contact,” no hand-tuned weights for distance-to-CP, no “expert features” that encode a racing theory.

**Allowed structure (not strategy bias):**

- Exact game rules and physics.
- Legal action encoding / discretization for tractability.
- Self-play Elo as ranking (not “looks cool”).
- Compute budgets matching CG (75 ms / turn).

### 2.2 Bias you cannot remove

| Unavoidable | Why |
|---|---|
| Rules of CSB | The game defines the objective |
| Physics fidelity | Wrong sim → wrong optimum |
| Finite compute | Optimal infinite search is impossible online |
| Random maps / matchmaking | Non-stationary opponents on CG |
| Neural architecture inductive bias | Convolutions vs MLP vs transformers |
| Exploration schedule | ε-greedy vs entropy is a choice |

**Zero-bias ≠ tabula rasa magic.** It means: **optimize win rate under rules with minimal strategy priors.**

### 2.3 What zero-bias is *not*

- Not “use GA with more generations.”
- Not “clone top1 LB bot from JSON.”
- Not “reward = −distance_to_cp” alone (that ignores blocking, collisions, timeouts).
- Not “train offline on LB winners only” (imitates 2024 meta, not theoretical best).

---

## 3. Interface design for self-learning (no strategy baked in)

### 3.1 Observation (symmetric, absolute-minimal encoding)

Prefer **raw physics state** over engineered features:

- Per pod (0–3): position, velocity, angle (sin/cos optional for continuity), next_cp index, shield timer if recoverable, boost remaining if known for you.
- Track: CP coordinates in order, laps remaining or global progress.
- Relative: optional **relative vectors to next CP** as *transforms of raw state*, not hand scores.

**Avoid as default inputs:** “is blocker,” “threat score,” “optimal angle error vs heuristic.”

### 3.2 Action (learnable, complete)

Options that still allow discovering any policy:

| Encoding | Pros | Cons |
|---|---|---|
| **A. Continuous:** offset from pod or from next CP + thrust logits | Flexible | Hard exploration |
| **B. Discrete grid:** angles × thrusts × {none,BOOST,SHIELD} | Easy RL | May miss fine aim |
| **C. Search residual:** policy proposes distribution; short MCTS/GA with **learned** prior only | Strong under 75 ms | Complexity |
| **D. Latent macro then decode** | Compresses long-horizon | Risk of collapsing macros |

Zero-bias recommendation: **start with B or A**, then **C** once a prior exists from self-play—not from GA expert labels.

### 3.3 Reward (the heart of “no bias”)

Ranked by purity:

| Reward | Bias | Notes |
|---|---|---|
| **+1 win / 0 loss / 0.5 draw** (sparse) | Minimal | Hard credit assignment over 100–400 turns |
| Terminal + **shaped only by rules** (timeout penalty = loss already) | Low | Prefer no mid-episode shaping |
| **Potential-based shaping** on true progress (checkpoint index + lap, potential Φ) | Low if Φ is progress index only | Ng et al.: keeps optimal policy if potential-based |
| Distance-to-CP shaping | **High bias** | Encourages dumb rush; ignores block/sacrifice |
| Imitation loss to LB | **Meta bias** | Use only as temporary regularizer |

**Zero-bias default:** sparse win/loss + optional **potential Φ = f(checkpoints_passed, best_pod)** only—never “style.”

### 3.4 Episode definition

- One full race (or truncated if time budget).
- Map sampled from: tournament catalog, random CG-like maps, or scraped map distributions.
- Opponent: **another policy from the population** (not a fixed script), with occasional historical LB bots as **challenge** only.

---

## 4. Training paradigms ranked for *this* game

### 4.1 Primary: **Self-play population / league** (best zero-bias path)

**Idea:** Many agents (or many snapshots) play each other; only **who wins** updates ranking and training weights.

Concrete CSB fits:

- **PFSP / league** (AlphaStar-style): beat a diverse set of past selves, not only the latest.
- **Population-based training (PBT):** evolve hyperparameters with fitness = Elo.
- **Nash / double oracle:** maintain a set of policies; train against mixture.

**Why it fits Mad Pod:**

- Non-transitive strategies (ram vs pure race, shield timing) → need a **league**, not a single opponent.
- Maps change → need **map distribution** in training.
- 2v2 with partial observability → self-play discovers blocking without labeling “blocker.”

**What you do:**

1. Build a **closed sim** (Fidelity/`fast_physics`) that runs full games headless, legal I/O, timeouts, boost/shield rules.
2. Maintain a **population** of policies + Elo.
3. Sample (policy A, policy B, map) → game → win signal → update.
4. Periodically **freeze** champions into the league (anti-forgetting).

### 4.2 Strong secondary: **Search distillation (zero expert strategy)**

**Idea:** At train time, use **heavy compute search** (deep GA / MCTS with **only win-rate or true progress** as objective—not human score) against a frozen opp model; distill the chosen actions into a fast net for 75 ms.

**Zero-bias condition:**

- Search objective = estimated **P(win)** or terminal reward, **not** GA’s hand score.
- Opp model = population / learned, not “opp aims CP @80” forever.

This is how you get “superhuman look-ahead” without coding strategy: **search invents**, **net copies search under budget**.

### 4.3 Optional warm-start: **Behavioral cloning from LB, then purge**

**Idea:** Imitate diverse LB agents briefly so the agent outputs legal competent actions, then **overwrite** with self-play and **decay BC weight to 0**.

**Risk:** meta lock-in. Mitigations:

- Multi-agent BC (many ranks, not only top1).
- Filter: only clone **winning** side actions in decisive games.
- Hard stop: BC off after N steps; only self-play Elo matters.

### 4.4 Offline RL on battles (secondary)

Use logged (s, a, r, s′) from:

- Self-play buffers (preferred).
- LB JSON (all 4 actions known → full trajectory).

Algorithms: CQL / IQL / Decision Transformer—**with** reward = reconstructed win, not “looked aggressive.”

**LB-only offline RL without self-play** will not invent strategies outside the data support.

### 4.5 What *not* to treat as primary

| Approach | Why weak for “best of all” zero-bias |
|---|---|
| Pure supervised clone of top LB | Ceiling = meta + no inventiveness |
| Pure GA forever | Human-shaped fitness; local optima |
| Reward = −dist_to_cp | Strong race bias, bad team play |
| Train vs random thrusters only | Exploitable garbage, not LB-level |

---

## 5. Physics as environment (your advantage)

You have something most CG players don’t: **replay-faithful sim** (when EXACT-validated).

| Use | Don’t use |
|---|---|
| Environment transition for RL | Reward = match GA score |
| Mass self-play without CG rate limits | Train on a broken Fast-only physics for strategy |
| Perfect information rollouts when **all actions known** (search) | Assume opp action = your model online without uncertainty |
| Domain randomization of maps | Domain randomization of **physics constants** (breaks CG) |

**Rule:** Training env physics **must** pass EXACT vs Fidelity on corpora. Strategy trained on wrong bounce is free Elo in sim, free loss on CG.

### 5.1 Partial observability in rollouts

For **online** play, opp actions unknown. Training options:

1. **Simultaneous move self-play:** both sides are policies; no need for opp action oracle.
2. **Belief / latent opp:** recurrent net over observed pod states.
3. **Search with sampled opp policies** from league mixture.
4. **Worst-case / robust:** maximize win vs adversarial opp from population.

Zero-bias prefers (1)+(3): never hardcode “opp always aims CP.”

---

## 6. Curriculum without strategy bias

Curriculum should be **environment difficulty**, not “teach blocking next week.”

| Stage | Environment | Still zero-bias? |
|---|---|---|
| 1 | Short races, 2–3 CP, weak random opponents | Yes if reward still win |
| 2 | Full maps from catalog, self-play peers | Yes |
| 3 | League includes diverse frozen policies | Yes |
| 4 | Inject **strong** historical LB bots as **opponents only** | Yes if not cloning them as self |
| 5 | Match CG time budget (75 ms) distillation | Yes |

Avoid curricula like “episode 1e6: force shield on collision.”

---

## 7. Evaluation that doesn’t lie

### 7.1 Metrics that matter

| Metric | Meaning |
|---|---|
| **Elo / TrueSkill vs league** | Relative strength under your sim |
| **Win rate vs frozen top checkpoints** | Progress |
| **Win rate vs diverse LB bot dumps** (as black-box opp) | Transfer toward real field |
| **CG ladder / arena** | Ground truth (slow, noisy) |
| Physics EXACT suite | Env still valid |

### 7.2 Metrics that mislead

- Average CP progress without wins.
- “Looks like top bot” imitation accuracy.
- Training loss alone.
- Only mirrors of yourself (rock-paper-scissors collapse).

### 7.3 Non-transitivity

Expect cycles: pure racer beats slow blocker; aggressive ram beats pure racer; controlled race beats ram. **League diversity** is mandatory for “best of all,” not a single scalar Elo vs one opp.

---

## 8. Concrete playbook — what **you** can do (no code here)

### Phase 0 — Foundations (1–2 weeks mental / infra plan)

1. Freeze **physics** as EXACT training env (already near-done in this repo).
2. Define **episode API** in design: reset(map) → step(actions_p0, actions_p1) → (obs, done, winner).
3. Define **legal action mask** (boost remaining, shield cooldown if tracked).
4. Decide observation = raw state only (document fields).
5. Decide reward = **win/loss (+ potential Φ on checkpoint index only)**.

### Phase 1 — Self-play MVP (minimum viable zero-bias)

1. Simplest policy class: discrete actions (relative angle bins × thrust bins × special).
2. Algorithm: **PPO or IMPALA-style** self-play **or** even evolutionary strategies on policy weights—**fitness = win rate vs current population**.
3. Maps: random from 18-catalog + random procedural CG-like maps.
4. Opponents: 50% current self, 50% past checkpoints (PFSP).
5. Stop criterion: Elo plateau vs league, not loss curve.

**Output:** a policy that invents *some* team coordination without roles coded in.

### Phase 2 — League + diversity

1. Keep N ≥ 20 frozen agents spanning training history.
2. Matchmaking: prioritize high regret / low win-rate matchups (harder opponents).
3. Add **asymmetric** init (spawn order) if CG does.
4. Track **non-transitive** matrix periodically.

### Phase 3 — Search distillation (still no human strategy)

1. Offline / train-time: deep search with objective **estimated win** (rollouts with league opp samples).
2. Distill to a fast net under 75 ms.
3. Online CG: net alone or net + shallow search with **learned prior**.

### Phase 4 — Controlled contact with human meta

1. Evaluate vs LB replays (bot as opponent by replaying opp actions, or open-loop).
2. Optional small BC fine-tune **then** more self-play (BC weight → 0).
3. Deploy to CG; only ladder decides.

### Phase 5 — Scaling (if compute exists)

1. Distributed actors generating self-play.
2. Larger models only if latency budget allows after distillation.
3. Population size up; never replace league with single champion training only.

---

## 9. Use of your existing assets (mapped to training)

| Asset | Zero-bias role |
|---|---|
| `physics.h` / `fast_physics.h` | **Simulator**; must stay EXACT |
| `validate_fast_physics_corpus` | Guardrail: env not drifting |
| Leaderboard battles | Opp library / offline data / evaluation |
| Golden fails (physics) | **Not** for strategy; fix env first |
| Current GA bot | **Baseline opponent** and latency reference—not teacher of reward |
| Rust CG paste / VERIFY | Deployment channel; measure live sim error |
| 75 ms limit | Hard constraint on distilled policy |

---

## 10. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Sim ≠ CG | EXACT physics gates; live VERIFY on share-replays |
| Policy collapses to one strategy | League + PFSP + entropy |
| Overfit to self-play junk | Eval vs LB bots and CG ladder |
| Sparse rewards → no learning | Potential-based Φ on checkpoint index only; longer training |
| Invalid/BOOST misuse | Legal masks; large penalty = loss equivalent |
| Timeout 100 turns | Include timeout losses in reward (already a loss) |
| “Zero bias” but human features creep in | Code review checklist: no strategy features in obs/reward |
| Compute waste | Start discrete actions + small net; scale later |

---

## 11. Decision table — pick a path

| If you have… | Do this first |
|---|---|
| Lots of CPU, limited ML ops | Self-play **ES/GA on neural weights** with win fitness (population) |
| GPU + RL stack | PPO/IMPALA self-play + league |
| Strong search already | Distill **win-based** search (not GA score) into net |
| Only LB data, no sim time | Offline multi-agent BC + offline RL, then **must** add self-play |
| Want absolute least bias | Self-play league only; LB only for eval |

---

## 12. One-sentence north star

**Train a population of policies in an EXACT physics environment so that the only signal that shapes behavior is winning races against a diverse set of other policies (and eventually the live ladder)—never a human racing manual or a hand-tuned score function.**

---

## 13. Suggested next step (still no code until you ask)

1. Agree Phase 1: **self-play + sparse win reward + discrete actions + EXACT sim**.  
2. Specify compute budget (CPU-only vs GPU).  
3. Then implement env loop + population Elo—**not** features.

---

*Document type: training strategy artifact. No implementation. Grounded in CSB rules, partial observability, and this repo’s physics/battle reality.*
