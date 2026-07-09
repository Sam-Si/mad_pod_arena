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

**Normalization (v1.1):**

| Constant | Value | Notes |
|---|---|---|
| `kNormPosX/Y` | 16000 / 9000 | keep |
| `kNormVel` | **2200** | was 1000; re-validate on speed hist |
| `kNormDist` | 16000 | map diagonal optional alt: 18357 |
| `kNormTimeout` | 100 | reconstructed clocks |
| `kNormTurn` | 500 | |
| `kNormNextGlobal` | 40 | |
| `kNormSeg` | 16000 | segment lengths |
| `kNormRaceLen` | 16000 × 8 × laps scale → use **cumulative path × laps** precomputed per map, normalize by that | remaining ∈ [0,1] preferred |
| Angles | sin/cos | never raw degrees |

**CG-parity encode rules (normative for v1.1):**

| Field | Own | Opp |
|---|---|---|
| boost_available | agent/sim bookkeeping (per-pod) | **0** |
| shield_timer | agent-tracked after SHIELD | **0** (no sim leak) |
| timeout | reconstructed turns-since-team-CP | reconstructed |
| opp_boost_known / opp_shield_known | omit from ship schema | — |

---

#### Block table (definitive ship v1.1, **104 dims**)

| ID | Feature | Dim | Tier | Notes |
|---|---|---|---|---|
| **A. Pods core (team-relative order)** | | **36** | T0 | 4 pods × 9 |
| A1 | `x/Nx, y/Ny` | 2×4 | T0 | absolute map frame (kept for global structure) |
| A2 | `vx/Nv, vy/Nv` | 2×4 | T0 | |
| A3 | `sinθ, cosθ` | 2×4 | T0 | if `!has_rotated`: sin=0,cos=1 |
| A4 | `has_rotated` | 1×4 | T0 | first-turn sentinel |
| A5 | `next_global/40` | 1×4 | T0 | drop local index |
| A6 | `shield_timer/4` | 1×4 | T0 | **own true / opp 0** under cg_parity |
| A7 | `boost_available` | 1×4 | T0 | **own true / opp 0** under cg_parity |
| | *(removed vs v1)* | −8 | — | drop `next_cp_local`, `opp_boost_known` per pod; drop free slots reallocated |
| **B. Own ego–CP geometry** | | **16** | T0 | 2 own pods × 8 |
| B1 | `dist_next / Nd` | 1×2 | T0 | to next CP center |
| B2 | `sin_err, cos_err` | 2×2 | T0 | shortest-angle facing vs CP direction |
| B3 | `closing = (v·û)/Nv` | 1×2 | T0 | |
| B4 | `lateral = (v×û)/Nv` | 1×2 | T0 | signed |
| B5 | `dist_cp1 / Nd` | 1×2 | T0 | to CP+1 |
| B6 | `turn_at_next` as sin(φ), cos(φ) | 2×2 | T0 | exterior angle CP−1→CP→CP+1 (wrap track) |
| **C. Pairwise combat (selected pairs)** | | **12** | T0 | 6 pairs × 2 |
| C1 | pairs: (o0,o1),(o0,p0),(o0,p1),(o1,p0),(o1,p1),(p0,p1) | | T0 | full 2v2 structure |
| C2 | `dist_ij / Nd` | 6 | T0 | |
| C3 | `approach_ij = ((vi−vj)·û_ij)/Nv` | 6 | T0 | >0 separating? sign convention fixed in golden |
| | *(TTCA scalar optional)* | +6 T1 | T1 | add only if C helps but combat still weak |
| **D. Race / team scalars** | | **6** | T0 | |
| D1 | `lead_next = (max_own_next − max_opp_next)/40` | 1 | T0 | |
| D2 | `remaining_race_own / L` | 1 | T0 | along-track from best own pod |
| D3 | `remaining_race_opp / L` | 1 | T0 | best opp pod |
| D4 | `ally_Δnext / 40` | 1 | T0 | own0−own1 global |
| D5 | `timeout_own/100, timeout_opp/100` | 2 | T0 | **reconstructed** |
| **E. Map list + path stats** | | **26** | T0 | |
| E1 | CP xy pad 8 × (`x/Nx,y/Ny`) | 16 | T0 | |
| E2 | `cp_valid[i]` | 8 | T0 | 1 if i < cp_count else 0 — **fixes zero-pad leak** |
| E3 | `cp_count/8, laps/5` | 2 | T0 | |
| | *(segment lengths all 8 deferred)* | | T1 | covered partly by B6 + remaining length; full seg table is first map ablation add-on (+8) |
| **F. Global phase** | | **2** | T0 | |
| F1 | `turn/500` | 1 | T0 | |
| F2 | `race_frac = 1 − remaining_best_own` | 1 | T0 | or omit if collinear with D2—keep one of {D2,F2}; if drop F2, free 1 dim |
| **G. History K=2 (compact)** | | **12** | T0 | |
| G1 | per pod `Δx/Nx, Δy/Ny` last step | 8 | T0 | 4×2; enables boost spike detect |
| G2 | own last special: boost_used, shield_used (each own pod) | 4 | T0 | from action history |
| | *(own last angle bin / thrust)* | +4 T1 | T1 | if policy needs more memory |
| | | | | |
| **TOTAL v1.1 ship** | | **104** | T0 | 36+16+12+6+26+2+12 = **110** → drop F2 and shrink G2 to 2 (team-level last special) **or** set G=10, F=2 → **104** |

**Dim arithmetic lock (use this):**

```text
A pods core ............. 36
B own ego-CP ............ 16
C pairs ................. 12
D race scalars ..........  6
E map list+valid+meta ... 26
F turn ..................  1   (drop race_frac; remaining covers it)
G history ...............  7   (4 pods × Δspeed_proxy? → see below)

Recount preferred:

A=36, B=16, C=12, D=6, E=26, F=1 (turn only), G=12
→ 36+16+12+6+26+1+12 = 109

Trim to 104:
- C: drop (p0,p1) pair → 5 pairs × 2 = 10  (−2)
- G: 4×(Δx,Δy)=8 + own boost/shield last (2 pods ×1 “special_any”)=2 → 10  (−2)
- D: merge remaining_own/opp into lead_remaining only? keep both (value needs both)
Final: 36+16+10+6+26+1+10 = 105 ≈ 104 with D ally_Δnext moved into optional

LOCKED v1.1-104:
  A=36, B=16, C=10 (5 pairs), D=6, E=26, F=1, G=9
  G detail: 4 pods × |Δv|/Nv (collision/boost proxy) = 4
           + 4 pods × dist_delta to their next CP (progress rate) = 4
           + team_own_boost_edge (1) 
  36+16+10+6+26+1+9 = 104
```

| ID | Feature | Dim | Tier |
|---|---|---|---|
| A | Pods core (4×9): xy, v, sin/cos, has_rot, next_g, shield, boost | 36 | T0 |
| B | Own ego–CP (2×8): dist, sin/cos err, closing, lateral, dist_cp1, sin/cos turn | 16 | T0 |
| C | Pairs (5×2): (o0o1)(o0p0)(o0p1)(o1p0)(o1p1) dist + approach | 10 | T0 |
| D | lead_next, rem_own, rem_opp, ally_Δnext, timeout_own, timeout_opp | 6 | T0 |
| E | 8×CP xy (16) + cp_valid(8) + cp_count + laps (2) | 26 | T0 |
| F | turn/500 | 1 | T0 |
| G | hist: 4×‖Δv‖_n + 4×Δdist_to_next_n + own_team_boost_edge | 9 | T0 |
| | **TOTAL** | **104** | |

**T1 expansion pack (+24 max, post-L2 win only):**

| Feature | Dim | Tier |
|---|---|---|
| Full segment lengths[8] | 8 | T1 |
| Ballistic TTCA per C pairs | 5 | T1 |
| Own last action (angle_bin/7, thrust/200) ×2 | 4 | T1 |
| Opp inferred boost proxy (vel spike & no CP) | 2 | T1 |
| Body-frame vel own (fwd, side) ×2 | 4 | T1 |
| Attention/GNN map encoder (replace E flat) | arch | T1 |

**TR (never ship):** true opp boost/shield/timeout from sim; GA evaluate_state; future joint actions.

---

### Ablation order

Each step: **same league protocol** (catalog L4 / Zero-Bias promotion spirit)—Wilson or fixed R on 18 maps × 2 sides; one block at a time; keep only if WR↑ or sample-efficiency↑ at fixed steps.

| Order | Ablation | Question | Keep if |
|---|---|---|---|
| **0** | v1-70 baseline (frozen) | Plumbing sanity | goldens pass |
| **1** | **Parity fix only** (v1 layout but opp shield→0; timeouts reconstructed; drop dead opp_boost_known→zeros already) | Was train/serve leaking? | ship any champion must pass this |
| **2** | **+B ego–CP geometry** (add B, can temporarily >70) | Does relative racing geometry pay? | yes almost certainly |
| **3** | **+D race scalars** (lead, remaining lengths, timeouts) | Does continuous progress beat next_global alone? | keep if V loss ↓ / WR↑ |
| **4** | **+C pair combat** | Needed for block/ram meta? | keep if loses to blocker baselines without it |
| **5** | **+E cp_valid + (optional) drop raw CP if B+D strong** | Is flat CP list still needed given B? | try “next-3 only” vs full 8 |
| **6** | **+G history K compact** | Memory for boost/shield inference? | keep if loses to force-boost GA styles |
| **7** | Full **v1.1-104** vs best subset | Joint pack | default research obs |
| **8** | T1 segment lengths[8] | Topology beyond turn_at_next | keep only if multi-map WR↑ |
| **9** | T1 TTCA / body-frame / last action | Combat & control polish | optional |
| **10** | T1 attention map vs flat E | Encoder form | only after flat wins |
| **11** | TR teacher distill (privileged → student v1.1) | Distill gap | never ship teacher obs |

**Explicit “remove” ablations (harmful feature checks):**

- Remove `team_id` (already removed in v1.1).  
- Remove absolute xy (B-only coords)—expect failure; confirms abs+rel mix.  
- Train with sim-truth opp shield vs forced 0—measure **serve gap** on CG-parity bridge; expect inflated sim WR with truth shield.

**Do not** ablate one scalar at a time forever; block ablations only.

---

### Concrete design patches

#### Patch 1 — Close CG-parity holes in Encode (do before any league claim)

```text
EncodeOptions {
  bool cg_parity = true;  // default champion
}

// When cg_parity:
//  - opp boost_available = 0
//  - opp shield_timer = 0          // NEW (v1 missed this)
//  - timeouts = reconstructed_clocks, NOT playerTimeout sim fields
//  - omit opp_boost_known from schema v2 (or force 0 and stop counting as capacity)
//
// Reconstruct timeout:
//  on each turn, for each team:
//    if any team pod next_cp_id advanced (or next_global↑): clock = 100
//    else clock = max(0, clock - 1)
//  Init clocks = 100 at race start (matches kTimeoutLimit spawn).
// Own shield:
//  on output SHIELD → timer = 4; each turn if timer>0 → timer-- (mirror Fidelity).
// Own boost:
//  on output BOOST → boosted=1 permanently for that pod (KD13).
```

Golden tests:  
1) `cg_parity=true` ⇒ all opp boost/shield channels identically 0.  
2) Bridge(CG-like view without shield/timeout fields) + agent bookkeeping **bitmatches** training Encode on same trajectory.  
3) Arena SyncViewFromGame must **not** be the authority for champion Encode—decode through the same reconstructors.

#### Patch 2 — Replace v1 dead dims with geometry (schema v2)

- Freeze v1=70 for PR-1 only (`obs_schema_version=1`).  
- Implement `obs_schema_version=2`, `obs_dim=104`, layout order **A,B,C,D,E,F,G** as locked table.  
- Weight header: magic, version, obs_dim, schema, sha256 (already planned).  
- C++/Python golden vector on one mid-race fixture all 18 maps.

#### Patch 3 — CP padding mask

```text
for i in 0..7:
  if i < cp_count: write xy_n, valid=1
  else: write 0,0, valid=0
```

Never rely on “zeros mean absent” without `valid`.

#### Patch 4 — Normalization bump

- `kNormVel = 2200.f` (or measure p99 speed under boost games and set 1.1× p99).  
- Prefer `remaining_race / map_total_race_length` ∈ [0,1] over another absolute scale.  
- Clip only after norm at ±5 for safety; log clip rate.

#### Patch 5 — Invariance & augmentation (training, not extra dims)

Already in Zero-Bias KD16: pod-swap p=0.5. Add:

| Augment | Prob | Effect |
|---|---|---|
| Swap own0/own1 | 0.5 | role symmetry |
| *(optional research)* reflect track through midline | off by default | only if maps allow; dangerous with absolute CP list |

**Team swap** is already handled by team-relative Encode—do not also feed `team_id`.

Translation invariance: **B and C supply it for control/combat**; keep A/E absolute so V knows where the circuit sits in the box (walls are soft but geometry of CP constellation matters).

#### Patch 6 — Search leaf vs reactive obs

Same Encode for π and V. Search terminal override:

```text
if team won/lost in leaf: return ±1 (or huge), do not trust V
```

Do not put search-only privileged features into V inputs.

#### Patch 7 — Catalog v1.1 sketch corrections

Update catalog §3.8 defaults to:

| Item | Old catalog | Critic lock |
|---|---|---|
| Dim | ~130 | **104 ship** |
| History | K=3 full stacks | **K=1Δ compact (G=9)** first |
| Map | 8 CP + segs + turns ~40 | **CP+valid+meta (26)**; segs T1 |
| Pods | ~48 with dead flags | **36** parity-clean |
| Missing | under-specified combat | **B+C mandatory** |
| Parity | boost only | **boost + shield + timeout** |

#### Patch 8 — What not to implement yet

- Graph Laplacian / GNN / CP embeddings  
- Open-space rays  
- RNN replacing G  
- Privileged teacher obs on ship path  
- Feeding GA scores or role ids  

#### Patch 9 — Acceptance criteria for “v1.1 done”

1. Schema v2 goldens C++/Python bitmatch.  
2. cg_parity stress: random trajectories, opp shield/boost channels always 0; timeouts match reconstructor vs a blind agent buffer (not vs raw `playerTimeout` unless reconstructor proven equal after CP events).  
3. Block ablation 2–6 run at least to smoke league (not full 2880) before freezing ship weights on 104-d.  
4. Amalgam: int8 weights still ≤256 KB with 104→128→128 torso (params still ~O(1e4–5e4)).  
5. Encode p99 ≪ 0.1 ms (budget already 0.05 ms class).

---

### Bottom line

- **v1/70** is a correct *protocol freeze*, not a competitive feature set.  
- **Biggest wins:** CP-relative geometry (B), remaining path + lead (D), pair approach (C), and **honest CG-parity on shield/timeout**.  
- **Biggest harms:** sim-leaked shield/timeout, dead opp_boost_known, absolute-only features, zero-pad CP ambiguity, vel scale saturation.  
- **Ship target:** `obs_dim=104`, schema v2, T0-only, block-ablate in the order above; expand map segs/history only after L2-style evidence.

---

*End of Observation Critic Report.*
