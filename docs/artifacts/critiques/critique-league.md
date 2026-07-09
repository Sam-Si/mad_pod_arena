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

Design logs BOOST/SHIELD/timeout histograms as descriptive; promotion Boolean ignores them. Catalog ladder L0–L6 also pass mostly on WR.

Missing ship-relevant metrics:

| Metric | Why it matters |
|---|---|
| Timeout loss rate (own team `playerTimeout`) | Common CSB failure mode; high WR via opponent timeouts ≠ racing skill |
| Dual-elim / max-turns draw rate | Stalling / mutual death metas |
| Mean / median CP progress at end (global_next) | Separates “close losses” from “never leaves start” when WR similar |
| Time-to-first-CP / laps completed distribution | Sparse-reward hang detection beyond L0 |
| Boost use rate & timing | Collapse to never-boost or panic-boost |
| Collision / shield rate | Pure race vs pure ram regimes |
| Per-map WR variance / worst-map WR | Specialization / catalog overfit |
| Policy entropy / action diversity (already collapse detector offline) | Should be a **soft promotion veto**, not train-only |

A bot can promote on Wilson while regressing timeout rate and worst-map WR—exactly the live CG failure pattern.

**M3 — No formal exploitability or “best of all bots” definition**

- Marketing language: outcome-defined best bot, league-competitive, beat GA under promotion rule.
- Actual definition: **lineage non-regression vs prev + soft non-regression vs pinned GA on catalog-18**.
- Missing:
  - Round-robin or Swiss among **frozen** historical champions + GA + diverse specialists.
  - **Nash / empirical payoff matrix** over a fixed probe set; report cycle length and support size.
  - Best-response stress: train a short adversarial fine-tune or pick PFSP hardest *held-out* checkpoints **not** used in training of this subject.
  - Definition of “beats the field”: e.g. Copeland score, Bradley–Terry rank with uncertainty, or “min WR vs panel ≥ τ” (maximin), not only mean vs prev.

Without this, non-transitive metas (design’s raison d’être) are acknowledged in training (PFSP) and **ignored in promotion**.

**M4 — Ladder L0–L6 conflates different scientific questions**

| Stage | Issue |
|---|---|
| L0 WR≥0.70 vs random, 1 map | Fine smoke; not comparable to promotion N/Wilson |
| L1 vs GA on map 0 only | Map-0 specialization can pass then die on 18 |
| L2 “WR≥0.55 vs random pool median” | “Random pool median” underspecified; not Wilson; not 75 ms panel |
| L3 full promotion | Only real gate—but see Critical |
| L4 ablations “same league protocol” | Catalog doc claims 18×2×R Wilson; expensive; risk of under-powered ablations with smaller R and false keep/drop |
| L5 GA shell vs sim MCTS | Needs **paired equal-time** and equal-dynamics; easy to mis-attribute |
| L6 CG canary | No N, no Wilson, no panel—policy vacuum |

Catalog and system design **disagree slightly** on ladder numbering/content (system L0–L3 numeric thresholds vs catalog L0–L6 pathfinder). Two ladders without a single SSOT invite metric shopping.

**M5 — Endogenous ckpt selection + PFSP training double-counts the same opponents**

Training already upweights hard historical agents (40% hard, 20% GA). Evaluation then reuses median/hardest **from the same population**. That is fine for “did training work on its curriculum,” not for external validity. External probes (pinned GA, frozen old champions, optional ReplayBot / BC clones) are underweighted in the **decision** rule.

**M6 — Compute allocation is inverted**

- 2880 games for promotion (720×4 pairings), hours of wall time, while primary Boolean uses only 720 vs prev_champion.
- 2160 games vs median/hard/GA (if all four pairings always run) largely inform dashboards.
- Better: fewer repeats on more **conditions** (holdout maps, more fixed opponents, fewer R) once clustered variance is modeled—or spend the budget on a proper multi-opponent matrix.

---

### Minor

**m1 — Wilson at fixed α=0.05 is arbitrary for ship risk.** For “replace production amalgam,” many orgs want higher confidence or sequential testing with α spend. Not wrong—just unargued.

**m2 — Elo with draw=0.5 is dashboard-only.** Good, but then dashboards will still drive human decisions. Specify that Elo is never used for automated promote/rollback.

**m3 — First prev_champion = random-init** makes first promotion LB≥0.50 nearly automatic for any semi-competent bot; first real bar collapses to secondary GA LB≥0.45 (weak).

**m4 — CI smoke @7.5 ms** is a different agent (weaker GA, different search depth). Calling it “league smoke” risks false confidence; rename to “crash/hygiene smoke.”

**m5 — No blocked factorial design** for map difficulty tiers (open hairpin vs straight). Uniform map sampling + aggregate WR masks structured failure.

**m6 — Side balance:** reporting only pooled WR can hide “wins as team0, loses as team1” encoding bugs; design has team-relative obs but no mandatory side-stratified pass condition.

**m7 — Catalog success criteria** (“V-search beats ga_baseline_v1 under promotion rule”) still equates research success with one secondary/primary mashup; align wording with multi-gate definition below.

**m8 — Population Elo for ckpt_median** is estimated in the last train epoch under non-stationary matchmaking—high variance label for “median.”

---

### Improved promotion protocol (write the full subsection text)

```text
### League evaluation protocol (closed) — revised

#### Design goals

1. **Reproducible decision:** same inputs → same promote/reject on one reference machine class; seeds stored.
2. **External validity:** fixed probes not chosen as a function of the subject.
3. **Non-transitivity awareness:** report full payoff structure; do not promote on a single pairwise WR alone.
4. **Honest uncertainty:** inference accounts for clustering (map × side), not naive i.i.d. over R repeats.
5. **Multi-metric health:** WR is primary but not sole; timeout/progress/diversity vetoes block “pathological winners.”
6. **Transfer:** catalog-18 is necessary; holdout maps (or procedural tracks) are required before “ship champion” label.

#### Fixed external panel v2 (immutable IDs, versioned)

| Slot | Agent id | Update rule |
|---|---|---|
| E0 | `subject` | candidate blob + SearchConfig |
| E1 | `ga_baseline_v1` | git-pinned BotConfig @75 ms, 1 thread (unchanged) |
| E2 | `champ_current` | last **shipped** amalgam weights (not last research promote if those differ) |
| E3 | `champ_minus_1` | previous shipped champion (frozen) |
| E4 | `specialist_race` | frozen ckpt maximizing pure race score / WR vs weak blockers (updated only on calendar schedule, e.g. monthly) |
| E5 | `specialist_block` | frozen ckpt maximizing disruption / timeout inflicted (same schedule) |
| E6 | `pool_anchor` | fixed hash of population member at pool freeze time T_train (chosen **before** subject training run ends; not min-WR vs subject) |

Optional eval-only (never training opponents by default): `ReplayBot` slices; BC-cloned nets.

**Forbidden in the decision panel:** ckpt_hard / ckpt_median selected by WR vs subject after the run. Those remain **training diagnostics** and PFSP inputs only.

#### Grids

| Grid | Maps | Sides | R | Budget | Role |
|---|---|---|---|---|---|
| **Catalog promotion** | 0–17 | 2 | **8** (not 20) | 75 ms | In-distribution estimate; fewer pseudo-replicates |
| **Holdout transfer** | H holdout maps (procedural or reserved live-like set), H≥6 | 2 | 8 | 75 ms | Required for ship tag |
| **Pairing matrix** | same | 2 | 4 | 75 ms | Full subject × {E1…E6} (or Swiss if cost-bound) |
| **CI hygiene** | 0–17 | 2 | 1 | 7.5 ms | Crash/timeout only; never Elo |

**Primary pairings (decision):**
- subject vs champ_current
- subject vs ga_baseline_v1
- subject vs pool_anchor
- subject vs specialist_race
- subject vs specialist_block
(champ_minus_1 required for lineage audit; can be secondary)

**Games per pairing (catalog):** 18 × 2 × 8 = 288 (cluster-aware analysis; do not treat as 288 i.i.d.).

#### Seeding and pairing (normative)

1. **Condition key:** `c = (map_idx, side, holdout_flag)`.
2. **Repeat index** `r = 0..R-1` resamples **shared** search noise where possible.
3. **Environment seed:** `seed_env = hash64("env", c, r, protocol_version)`.
4. **Shared bot noise seed:** `seed_bot = hash64("bot", c, r, protocol_version)` — **same seed material for both players’ search RNG streams**, mixed with a per-seat salt only for seat-local tie-breaks, not for GA population init if the API allows a single shared stream. If bots cannot share streams, document residual unpaired noise and use **more maps/sides**, not more R, to reduce variance.
5. **Time-capped search:** record `N_cand`, wall ms, and CPU model. Promotion comparisons are valid only on the **reference hardware class** listed in the report. Cross-machine Wilson is informational.
6. **Deterministic mode (optional research):** fixed candidate budget (N_cand cap) instead of wall clock for offline league only—**not** for ship claims (ship remains wall-clock 75 ms).

#### Scoring

Per game outcomes for subject seat:

- `W` win, `L` loss, `D` draw (race dual-finish, dual-elim, max_turns).
- Primary win indicator for CI: `Y = 1[W]` (draw ≠ win), same as v1.
- Also store ordinal score `S ∈ {1, 0.5, 0}` for Bradley–Terry / Elo dashboards only.

**Cluster unit for inference:** `(map_idx, side)` [and holdout id]. Repeats within cluster estimate within-cell noise only.

**Primary WR estimator:** mean of per-cluster win rates `Ȳ_c`, then macro-average over clusters (equal weight per map×side). Report both micro (game-level) and macro (cluster-level).

**Confidence:** use **cluster bootstrap** (resample maps×sides, keep all repeats inside) or Wilson **only after** collapsing to one Bernoulli per cluster via majority/mean—do **not** feed N=720 raw games into Wilson as i.i.d.

#### Multi-gate promotion rule (all must pass)

**Gate A — Lineage (catalog, cluster-bootstrap):**  
Macro WR vs `champ_current` has **cluster-bootstrap lower 95% bound ≥ 0.50**, **or** (non-inferiority) lower bound on WR_subject − WR_champ_mirror ≥ −δ with δ=0.00 for strict replace, δ=0.02 only for “shadow promote” research tags.  
If no champ_current (cold start): skip A; require Gate B hard mode.

**Gate B — External anchor (catalog):**  
Macro WR vs `ga_baseline_v1`:  
- Cold start: cluster-bootstrap LB ≥ **0.50** (not 0.45).  
- Else: non-inferiority vs champ_current’s frozen GA report on the **same seeds**: LB(WR_subj_GA − WR_champ_GA) ≥ −0.02, **and** point macro WR_subj_GA ≥ 0.48.

**Gate C — Maximin / exploit panel (catalog):**  
min_{opp ∈ {pool_anchor, specialist_race, specialist_block}} macro WR ≥ **0.40**, and mean macro WR across those three ≥ **0.50**.  
(Prevents pure rock-paper-scissors “beat only the last self.”)

**Gate D — Health vetoes (catalog + holdout):** fail promotion if any hold:  
1. Own timeout-loss rate > champ_current + 5 pp (absolute).  
2. Worst-map macro WR (catalog) < 0.35.  
3. Draw rate > 15% of games (investigate stalling).  
4. Mean terminal global_next (losing games) collapses >20% vs champ_current.  
5. Collapse detector: action entropy below train threshold on league rollouts.  
6. Side gap: |WR_side0 − WR_side1| > 0.15 (encoding / seat bug).

**Gate E — Transfer (required for `ship_champion` tag only):**  
Same Gates A–D on **holdout** grid with possibly relaxed maximin (min WR ≥ 0.35) but **hard fail** if holdout macro WR vs ga_baseline_v1 drops >5 pp vs catalog gap. Research promote without E gets tag `catalog_champion` only—not amalgam default.

**Gate F — Cost / latency:** p99 ≤ 75 ms on reference machine (existing PR-7 smoke); else distill path A4, no ship.

#### Payoff matrix & exploitability report (mandatory artifact, not optional dashboard)

For frozen set F = {subject, champ_current, ga_baseline_v1, specialists…} run a reduced round-robin (R=4, all maps×sides or stratified map sample). Emit:

- Empirical payoff matrix M_ij = macro WR(i vs j)
- Cycles of length 3+
- Copeland score / BT ranking with bootstrap ranks
- **Exploitability proxy:** 1 − min_j M_subject,j (higher = more exploitable)

**Ship language:** “best bot” means **highest Copeland among ship candidates that pass Gates A–F**, not “passed Wilson vs parent.”

#### Relation to training PFSP

PFSP may continue to use hard/median subject-relative opponents. **Promotion panel stays external.** Once per K promotes, refresh specialists on a calendar, not on the candidate under test.

#### Ladder alignment (single SSOT)

| Stage | Question | Metric |
|---|---|---|
| L0 | Learns anything? | WR vs random map0 + progress histograms |
| L1 | Not map0-only? | Macro WR vs GA on 6-map subset |
| L2 | Curriculum works? | Pool matrix diagonal not 1; entropy OK |
| L3 | Catalog promote? | Gates A–D |
| L4 | Ablations | Gates A–D with fixed seed file; smaller R allowed if power analysis pre-registered |
| L5 | Planner A/B | Paired equal-time, same weights if possible |
| L6 | Ship | Gates A–F + CG canary N≥30 with pre-registered GA comparison |

Catalog doc and system design must point at this subsection as the sole promotion SSOT (remove duplicate L3 “Wilson only” wording).
```

---

### Concrete design patches

1. **KD17 rewrite (system design § League + Key Decisions)**  
   - Replace “sole primary rule Wilson LB@95% ≥ 0.50 on N=720 i.i.d. games” with **cluster-bootstrap / macro-WR Gates A–F** above.  
   - Explicitly state: **repeats are not independent maps**; Wilson on raw games is forbidden for promote/reject.  
   - Demote point-estimate folklore; keep WR̂ only in reports.

2. **Panel v1 → v2 (system design fixed panel table)**  
   - Remove `ckpt_median` / `ckpt_hard` from promotion panel.  
   - Add frozen `champ_minus_1`, `pool_anchor` (pre-registered), `specialist_race`, `specialist_block`.  
   - Document calendar refresh; forbid subject-conditional selection.

3. **Seed policy patch**  
   - Change `hash(map, side, repeat, agent_id)` → shared `seed_bot` for both seats + env seed; document residual unpaired noise if APIs force split.  
   - Add reference-hardware clause and optional fixed-N_cand offline mode.  
   - Prefer **R=8, more opponents/holdouts** over R=20 pseudo-replicates.

4. **Secondary GA gate patch**  
   - Cold start: LB/macro ≥ **0.50** vs GA (raise from 0.45).  
   - Ongoing: proper non-inferiority on **difference of macro WRs** with shared seeds, not LB-to-LB − ε.  
   - Store champ’s GA grid report immutably at promotion time (already half-specified—make mandatory artifact).

5. **Metrics beyond WR (Observability + promote.py)**  
   - Promote report JSON must include: timeout-loss rate, draw rate, worst-map WR, side gap, mean terminal progress on losses, BOOST/SHIELD rates, entropy.  
   - Implement Gate D vetoes as hard fails in `train/promote.py`.  
   - Collapse detector applies to league rollouts, not only learner windows.

6. **Catalog-18 / holdout (KD9, OQ-RL6, catalog L3/L6)**  
   - Promote OQ-RL6 from optional to **required for ship tag**.  
   - Add `maps/holdout` or procedural generator with fixed seeds for H≥6.  
   - Split tags: `catalog_champion` vs `ship_champion`.  
   - Ablation matrix must run on the **same protocol version** and cannot claim transfer without holdout.

7. **Exploitability artifact (new mandatory league output)**  
   - Payoff matrix + Copeland/BT + min-WR exploitability proxy.  
   - Success criteria tables in both docs: replace “beats GA under Wilson” with “passes Gates A–F and ranks first by Copeland among candidates.”

8. **Ladder SSOT merge**  
   - Single ladder table in system design; catalog §4.3 becomes a pointer.  
   - L2 criterion rewritten with concrete pool matrix stats (not “random pool median”).  
   - L1 cannot pass on map 0 only for any path that skips multi-map smoke.

9. **Budget reallocation**  
   - Drop total games from 2880@R=20×4 endogenous pairings to ~288×5–6 external pairings + holdout + small round-robin; document wall-clock on reference machine (PR-9).

10. **Language audit**  
    - Replace “best of all bots / outcome-defined best” with: *best under protocol P on panel F at 75 ms with tags catalog|ship*.  
    - Keep zero-bias reward philosophy; do not pretend the eval panel is strategy-bias-free if specialists are hand-labeled—label them as **coverage probes**, not reward.

11. **Catalog doc cross-links**  
    - §4.3 L2/L3/L6 and §4.4 “same league protocol” → cite revised protocol subsection.  
    - §6 Research criterion: multi-gate + matrix, not single Wilson line.

12. **First-champion special case**  
    - Do not allow random-init as prev_champion for Gate A storytelling; cold-start uses Gate B hard + Gate C only, tag `bootstrap_champion`.

---

*End of League/Eval Critic Report.*
