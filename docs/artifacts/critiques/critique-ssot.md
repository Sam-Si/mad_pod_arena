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
   - **`fast_physics::step(Move[4])` is correct joint:** four `applyMove` then one `simulateWorld`.  
   - **KD21** correctly forbids alternating turn-based MCTS.  
   Residual skew risks:
   - Search that steps **own pods only** then re-encodes before opp (sequential ply).  
   - GA-style **role-ordered** apply (runner then blocker) is fine **if** all four actions land before world step; not fine if interleaved with `SimulateTurn` twice.  
   - Opp model that freezes opp actions from previous root while own actions update is intentional imperfect info, not sequential physics — document so it is not “fixed” into alternate-move MCTS.  
   - `applyGAActionDegrees` path inside FP (angle-shift + thrust) is **not** the same as target-point `applyMove` used by Arena/CG. ValueSearch must emit **target-point Moves** (or prove degree-shift ≡ target decode) or train/serve action semantics skew.

4. **Path C can reintroduce Fast fragment (concrete vectors)**  
   | Vector | How it sneaks back | Mitigation |
   |---|---|---|
   | Copy `SimulateAndEvaluate` | Leaf swap only; keep `FastSimulateTurn` | PR-7a new file; ban symbol on champion TU |
   | `CSB_SEARCH_HYBRID_FAST` | Budget panic | Default off; ship build `-U` / assert false |
   | Free-flight `ga_pure` friction eval | Score without full FP step | Forbidden on champion leaf when V on |
   | Microbench “Fast baseline” | Becomes CI default path | Bench both; gate only FP champion |
   | Amalgam size | Drop world-step to ship | Distill π (A4), don’t ship Fast+V |
   | “Train V on Fast states for consistency” | A8 revived | Explicit reject; tests on EXACT only |
   | Shared IBR scaffolding | Stages call old sim | Separate rollout interface `IRolloutPhysics` with only FP impl linked |

5. **KD13 (per-pod boost) vs `docs/rules.md` folklore**  
   - Code SSOT: `boosted` per pod in `physics.h` / FP; correct.  
   - `docs/rules.md` still says team-shared boost — must not drive masks, BC labels, or Encode.  
   - Arena correctly maps `boost_available = (boosted==0)` per pod.  
   - Residual: team-shared BOOST folklore in comments, BC parsers, or agent bookkeeping that clears **both** pods on one BOOST.

6. **Spawn SSOT**  
   - Design correctly points at `initializeFromTrack` + `roundHalfUp`, not `Arena::GenerateMap` (uses engine `Round` then overwritten).  
   - PR-1 spawn equality 18 maps is mandatory; any RL Reset that reimplements mults without calling the same law will poison BC and self-play.

7. **SSOT register lag for RL**  
   - `docs/SSOT.md` ownership table does not yet list `src/rl/terminal.h`, Encode, Bridge, or “champion rollouts = fast_physics”. Until updated, implementers will treat GA Fast as “the” search physics (as SSOT still lists Fast fragment for GA).  
   - Policy checker enforces FP calls `simulateFidelityWorld` but **not** applyThrust identity.

---

### Minor

1. **Friendly collision / `g_friendly_collision`**  
   Fast fragment sets global flag for teammate pairs; Fidelity world-step does not. Harmless if ValueSearch never reads it; dangerous if any leaf/heuristic still does.

2. **FP optimization knobs (`CSB_FP_OPT_*`)**  
   SINCOS / trig cache / free-flight epilogue can create platform-dependent float paths. Apple defaults SINCOS off. Champion train and ship must pin the same defines; document in weight header or refuse mismatched builds.

3. **Mass dual formulation is intentional Fast vs Fidelity, not a bug**  
   `kShieldMassFast=10` vs Fidelity `m=0.1` in `worldBounce` — different kernels. Only a problem if Path C mixes them. Keep out of champion rollouts.

4. **Design terminal steps 3–5 redundancy**  
   Cleanup-only: single `checkTerminal(fields)` matching Arena’s first decisive returns + max turns; drop duplicated prose that invites off-by-one ports.

5. **Obs v1.1 catalog expansion**  
   History/map graph is fine for T0 **if** reconstructed; do not expand privilege surface (true opp shield, true timeouts from sim) under `cg_parity=true`.

6. **SSOT-refactor KD13/KD14 (BotConfig / kCgFriction discovery)**  
   Orthogonal naming collision with Zero-Bias KD13/KD14. Friction mirror policy remains relevant for amalgam; does not authorize Fast rollouts for V.

---

### Must-pass physics gates for ML

Before any “best bot”, “beats GA”, or promotion claim, **all** of the following must be green. Research curves without these are non-transferable Elo.

| # | Gate | Pass criterion |
|---|---|---|
| G0 | **Truth suite** | `./tools/run_truth_suite.sh` green (SSOT policy + Gate A culture + existing physics gates). Physics PRs orthogonal (KD11). |
| G1 | **FP ↔ Fidelity EXACT (shared apply)** | After unifying `applyMove`/`applyThrust` (or proving identity): `validate_fast_physics` corpus EXACT + long random joint-move soak (all 18 maps, random legal moves, N≥1e5 steps) with **bit-identical** pos/vel/next/shield/boosted/timeouts/won/angle. |
| G2 | **Spawn parity** | `EpisodeRunner::Reset` vs `csb::Game::initializeFromTrack` on maps 0–17: positions, angles, next, timeouts equal. |
| G3 | **Terminal parity** | Shared `terminal.h` vs `Arena::PlayGame` / `checkWinner` on fixtures: win, loss, dual-finish draw, dual-elim, single timeout, max-turns=500, ongoing=−2. |
| G4 | **Bridge + Encode bitmatch (KD20)** | Mid-race Fidelity frame → `SyncViewFromGame` → `BridgeViewToFastPhysics` → `Encode(cg_parity=true)` **bitmatches** training Encode on same underlying state under cg_parity (own boost bookkeeping path included). |
| G5 | **cg_parity privilege audit** | Golden vectors: under `cg_parity=true`, opp boost + `opp_boost_known` = 0; **and** no channel is filled from sim-only truth for opp shield / timeouts unless reconstruction code is the same online. Prefer tests that run Encode from **CG-visible fields + agent memory only**. |
| G6 | **KD13 masks** | BOOST legal mask per-pod `boosted==0`; never team-shared; BC labels match. |
| G7 | **Champion rollouts = FP only (KD14)** | ValueSearchBot TU: zero references to `FastSimulateTurn` / `csb::fast::SimulateTurn` on default path; `use_hybrid_fast==false`; CI grep / link guard. |
| G8 | **Joint simultaneous step** | Property test: permuting apply order of four Moves before one world step does not change state (moves are independent pre-collision); forbidding two world steps per decision ply. |
| G9 | **Search root re-root every turn** | Online: each `GetActions` rebuilds FP root from view+bookkeeping; no sticky FP state across CG turns without bridge. |
| G10 | **Latency without cheating dynamics** | p99 ≤75 ms (warm+cold) with **FP rollouts + real V**, hybrid off. If fail → A4 distill, **not** Fast fragment. |
| G11 | **League promotion @75 ms (KD17)** | Wilson LB protocol on 18×2×20; `ga_baseline_v1` pin; no 7.5 ms promotion. |
| G12 | **Train/serve action decode** | Discrete grid decode R=5000 + int target cast matches Arena/CG output path used in league. |

**Explicit non-gates for “best bot”:** L0 WR vs random alone; BC accuracy; GA heuristic score regression; Fast-fragment self-play Elo; any result with `cg_parity=false` weights.

---

### Concrete design patches

1. **Make move application SSOT (Critical)**  
   - Add `fidelity_apply.h` (name flexible) with `applyMove`/`applyThrust`/`applyRotate` shared by `physics.h` and `fast_physics.h`.  
   - Extend `sim/check_ssot_policy.py`: fail if `fast_physics.h` defines a local thrust knife-edge loop or skips `thrustCosSin`.  
   - Update `docs/SSOT.md` ownership table: “Fidelity pre-step move apply” → shared header; façades must not re-own.

2. **Split obs privilege from dynamics belief (Critical / KD20 amend)**  
   - `EncodeOptions`: `cg_parity` continues to zero opp boost fields.  
   - Add `TimeoutSource` / `ShieldSource`: `AgentReconstructed` vs `SimOracle` (oracle only for TR teacher).  
   - Document **opp boost belief for rollouts** as a separate enum (`AssumeAvailable` / `AssumeSpent` / `Sample` / `Oracle`); champion default must be chosen and golden-tested — not silently `boosted=0` in FP root without comment.

3. **Bridge contract table (PR-7a normative)**  
   ```text
   view.pos/vel          → fp.px/py/vx/vy
   view.angle degrees    → sentinel? kInitAngleSentinel + hasRotated=false
                           : deg*kDegToRad + hasRotated=true
   view.next_cp_id+laps  → GlobalNext; forbid won→laps=3 overload
   own_boosted[2]        → fp.boosted for own pair
   opp                   → fp.boosted per belief policy (not Encode zeros alone)
   view.shield_cd        → fp.shieldtimer  // own OK; opp only if CG-visible policy allows
   playerTimeout         → invert Arena view timeout or track from CP events
   setTrack(xy,n,laps)   → identical global_n to Game::buildGlobalCp
   Encode(..., cg_parity=true) after bridge
   ```
   Golden: Arena frame bitmatch as designed.

4. **Terminal single owner**  
   - `src/rl/terminal.h`: `teamWon`, `teamAlive`, `checkWinner`, `checkTerminalArenaOrder` over POD fields.  
   - Arena (future) and EpisodeRunner both call it — stop dual prose lists that diverge.  
   - `fast_physics::Game` remains method-free for terminals (design already correct).

5. **Path C / PR-7a hard constraints**  
   - ValueSearchBot: `using RolloutGame = csb::fast_physics::Game` only.  
   - Build rule: champion library **does not** depend on calling `csb::fast::SimulateTurn` (engine may still link Fast for GA baseline).  
   - Feature flags: `use_fast_physics_rollouts` default true; `use_hybrid_fast` default false; **export_cg_submission / CreateValueSearchBot** refuse hybrid.  
   - Latency fail → PolicyBot distill path, not Fast.

6. **Simultaneous API in design**  
   - Normative: `Step(TeamAction a0, TeamAction a1)` builds `Move[4]` then **one** `game.step(moves)`.  
   - Ban any public `StepPod` that world-steps.  
   - MCTS section: restate “one `fast_physics::step` per ply; expand joint/factored simultaneous actions only (KD21).”

7. **cg_parity completeness checklist (catalog + Zero-Bias)**  
   Extend KD20 text:  
   - Champion train feed = **CG-visible + own bookkeeping reconstruction**, not Arena-privileged `SyncViewFromGame` raw.  
   - Add tests for: opp shield channels zero or reconstructed-only; timeouts reconstructed from next-edge history; no `playerTimeout` oracle under `cg_parity`.  
   - Arena league for IBots may keep full view **only if** ValueSearchBot re-encodes via CG-parity bridge (ignore privileged fields).

8. **Apply-target integerization**  
   - Spec: all champion actions cast targets to int before apply (match `arena.cpp`).  
   - Train decoder emits int-equivalent moves so V sees the same next-state family.

9. **SSOT.md living update (when RL lands)**  
   | Concern | Authority | Must not re-own |
   |---|---|---|
   | Champion search rollouts | `fast_physics::Game` + shared apply | `csb::fast::SimulateTurn` |
   | Terminal | `rl/terminal.h` field helpers | Ad-hoc won/timeout checks in bots |
   | Observation | `Encode` + `cg_parity` | Privileged sim fields on ship path |
   | View→FP root | `BridgeViewToFastPhysics` | Ad-hoc setPod in bot |

10. **Must-pass gate wiring**  
    - PR-1: G2, G3, G5 (partial Encode), G6.  
    - PR-7a: G1 (post-apply unify), G4, G7, G8, G9, G10 dummy V.  
    - PR-7/9: G10 real V, G11, G12.  
    - Document in Zero-Bias “DoD” that Wilson promotion without G1/G4/G7 is invalid.

11. **rules.md / folklore quarantine**  
    - Annotate team-shared boost sentence as **non-SSOT / superseded by code KD13** so BC and agents stop reading it.

12. **Optional but recommended:** adversarial FP soak binary in CI (non-blocking first, then blocking) — random joint moves + shield/boost spam — separate from battle JSON EXACT, to catch applyThrust drift self-play will hit.

---

### Bottom line

World-step SSOT is in good shape; **move-apply dual implementation**, **incomplete cg_parity**, **opp-boost dynamics**, and **Path C’s gravitational pull back to `FastSimulateTurn`** are the remaining train/serve kill shots. Treat KD14/KD20 as incomplete until shared apply + CG-visible Encode + explicit opp-boost belief + hard no-Fast ship gates are written into the design and PR acceptance criteria. No best-bot claim before the must-pass physics gate table is green.
