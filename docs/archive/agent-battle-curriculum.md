# Agent battle curriculum

Structured learning pack for AI agents working on **Mad Pod Arena** referee physics
and bot strategy. Prefer this over dumping the entire `battles/leaderboard_battles/` tree.

Also see: [GEMINI.md](../GEMINI.md) · [battles/README.md](../battles/README.md) ·
[physics-verification.md](physics-verification.md) · machine lists in [`agent_pack/`](agent_pack/).

---

## 0. Hard rules (read first)

1. **Referee physics SSoT:** only [`src/physics/physics.h`](../src/physics/physics.h).
2. **Bot / arena physics SSoT:** only [`src/engine/engine.h`](../src/engine/engine.h) + `engine.cpp`.
3. **Battle retention:** only ids **> 870230019** ([`battles/RETENTION.md`](../battles/RETENTION.md)).
4. **CI gate:** `python3 sim/verify_battles.py battles/test_session_battles` must stay **100%** (authoritative harness).
5. **Timeouts ≠ physics fails:** never train checkpoint logic on `*_timeouts/` folders alone.
6. **Do not squash-sync git history** as a substitute for real upstream commits.

---

## 1. Curriculum tiers (physics)

Study in this order. Each tier has a purpose; skipping labels loses most of the signal.

### Tier S — primary supervised set (best single folder)

**Path:** [`battles/golden_physics_battles/`](../battles/golden_physics_battles/)

| Asset | Role |
|-------|------|
| `battles/*.json` | ~200 replay copies |
| `manifest.csv` | `expected_result`, `first_fail_turn`, `error_types`, source |
| `expected_pass.txt` | Must verify clean (`verify_golden_corpus.py --tier pass`) |
| `expected_fail.txt` | Known divergences — tracked, not gated at 100% |

**Why best for AI:** pass/fail labels, stratified test_session + leaderboard + divergences,
small enough for context windows, built for `physics.h` regression.

**Agent task examples:**

- Replay one `expected_pass` battle; assert all turns within tolerances.
- Pick one `expected_fail` row; explain first failing field using divergence notes.
- After editing `cpCollide` / movement sweep, re-run pass tier only.

**Index copies:** [`agent_pack/golden_expected_pass.txt`](agent_pack/golden_expected_pass.txt),
[`agent_pack/golden_expected_fail.txt`](agent_pack/golden_expected_fail.txt).

### Tier A — CI anchor (never regress)

**Path:** [`battles/test_session_battles/`](../battles/test_session_battles/) (~312 JSON)

Positive-only supervision. Definition of done for referee fidelity (~46k turns at 100% via Python harness).

**Index:** [`agent_pack/test_session_battles.txt`](agent_pack/test_session_battles.txt).

```bash
python3 sim/verify_battles.py battles/test_session_battles
```

### Tier A− — failure curriculum (knife edges)

**Path:** [`battles/leaderboard_physics_divergences/`](../battles/leaderboard_physics_divergences/)

| Asset | Role |
|-------|------|
| `battles/battle_*.json` | Replays that diverged under verifier tolerances |
| `battles/*.divergence.json` | `first_fail_turn`, `error_types`, tolerance formula |
| `manifest.csv` | Tabular index |

Highest information density per file for *debug physics*. Pair with Tier S fail list
(some battles may have been **promoted to pass** after physics fixes — trust golden manifests).

**Index:** [`agent_pack/divergence_battles.txt`](agent_pack/divergence_battles.txt),
[`agent_pack/divergence_sidecars.txt`](agent_pack/divergence_sidecars.txt).

### Tier B — diversity sampler only

**Path:** [`battles/leaderboard_battles/`](../battles/leaderboard_battles/) (~22k) +
[`leaderboard_battles_categorized/manifest.csv`](../battles/leaderboard_battles_categorized/manifest.csv)

Use manifest categories (`06_end_reached_marathon`, `09_end_reached_shield_heavy`, …) to
**sample** playstyles. Do not fine-tune on all 22k unlabeled.

### Tier C — exclude from physics training

| Path | Reason |
|------|--------|
| `battles/test_session_timeouts/`, `leaderboard_timeouts/` | Agent/runtime timeouts |
| `battles/copy_pasted_battles/*.md` | Unstructured console paste |
| `third_party/referees/` | Read-only reference, not training targets |

---

## 2. Recommended training / retrieval pack (files)

```text
POLICY
  GEMINI.md
  docs/agent-battle-curriculum.md          ← this file
  docs/physics-verification.md
  battles/README.md

SUPERVISED PHYSICS (primary)
  battles/golden_physics_battles/manifest.csv
  battles/golden_physics_battles/expected_pass.txt
  battles/golden_physics_battles/expected_fail.txt
  battles/golden_physics_battles/battles/*.json

FAILURE DEEP-DIVE
  battles/leaderboard_physics_divergences/manifest.csv
  battles/leaderboard_physics_divergences/battles/*.json
  battles/leaderboard_physics_divergences/battles/*.divergence.json

CI ANCHOR
  battles/test_session_battles/*.json

OPTIONAL SAMPLER
  battles/leaderboard_battles_categorized/manifest.csv
  → resolve filenames under battles/leaderboard_battles/

CODE SSoT
  src/physics/physics.h
  sim/verify_battles.py
  battles/scripts/verify_golden_corpus.py

SNAPSHOT / INDICES
  docs/agent_pack/
```

---

## 3. Physics learning objectives (from this corpus)

Agents should be able to implement / preserve:

1. **Turn loop order:** rotate → accelerate → move+collisions → friction+round → timers.
2. **Checkpoint pass:** segment **previous → current** vs CP disk; Go-style projection;
   radius test **`dist² < 600²` (strict)** — inclusive `<=` false-passes on dist==600
   (`battle_884515945` turn 9 style cases).
3. **Mid-turn CP bookkeeping:** advance previous pose after each collision slice (`curps` / `previous_pos`);
   end-of-turn-only segment regresses many pass-tier battles.
4. **Shield:** `shield_cd = 4` on activate; mass 10 only while `shield_cd == 4`; decrement in end-turn.
5. **Timeouts on CP pass:** set so post-decrement frame shows 100 (implementation uses 101 pre-decrement).
6. **Tolerances (Python verifier):** pos ≤5, vel ≤3, angle ≤1°, `next_cp` exact mod n_cp, timeouts ≤1.

---

## 4. Bot strategy branches (separate from battle physics data)

Physics corpora teach **referee fidelity**. Bot skill lives on feature branches under `src/cg/`,
`src/engine/`, `src/tournament/`. See full table: [`agent_pack/FEATURE_BRANCH_SCORES.md`](agent_pack/FEATURE_BRANCH_SCORES.md).

| Branch | Best for | Score /60 |
|--------|----------|----------:|
| **`feature/phase1-coevolution-ibr`** | Full stack: IBR + physics monorepo + golden corpus — **current integration** | **54** |
| **`feature/phase2-hybrid-rm-mcts`** | Hybrid regret-matching / MCTS search delta | **48** |
| **`feature/local-dev`** | DRY engine physics mid-history | **40** |
| **`feature/minimax-coevolution`** | Early unified bot + evasion weight | **32** |
| **`feature/benchmark-tournament`** | Tournament harness (WIP) | **28** |
| **`main`** | Pre-monorepo baseline | **12** |

**Best branch for agents implementing bots today:** `feature/phase1-coevolution-ibr`
(contains Phase 1 IBR, MCTS budget work, and battle-verified physics). Pair with Tier S battles.

**Best branch for search-algorithm diffs only:** `feature/phase2-hybrid-rm-mcts` (`9c7bcf09`).

Leaderboard JSON volume on phase1 is **data absorption**, not proof the bot policy is proportionally better.

### Bot curriculum (code order)

1. `src/engine/bot.h` — interfaces / config
2. `src/engine/engine.h` / `engine.cpp` — `PhysicsSimulator` vs `GAPhysicsSimulator`
3. `src/cg/cg_bot.cpp` — GA / search entry
4. Commits: `3cab7a4f` (IBR), `b582b5ae` (MCTS budget), `9c7bcf09` (hybrid RM)
5. `src/tournament/benchmark_tournament.cpp` — measure, don't guess

---

## 5. Verification ritual (after any `physics.h` edit)

```bash
python3 battles/scripts/verify_golden_corpus.py --tier pass   # must be clean
python3 sim/verify_battles.py battles/test_session_battles     # must be 100%
# optional:
python3 battles/scripts/verify_golden_corpus.py
python3 sim/verify_battles.py battles/leaderboard_physics_divergences/battles
```

Update golden `expected_pass` / `expected_fail` only when divergences are **fixed for real**
(re-verify, then move filenames between lists and refresh `manifest.csv`).

Latest snapshot: [`agent_pack/VERIFICATION_SNAPSHOT.md`](agent_pack/VERIFICATION_SNAPSHOT.md).

---

## 6. Anti-patterns

| Anti-pattern | Prefer |
|--------------|--------|
| Train on all 22k leaderboard JSON unlabeled | Golden 200 + divergence sidecars |
| Treat agent timeout battles as CP bugs | Keep `*_timeouts/` segregated |
| Implement physics in `sim/` or bots | Call `physics.h` / `replay_driver` only |
| Inclusive CP radius `<= 600` "to be safe" | Match Go strict `<`; test dist==600 cases |
| End-of-turn-only CP segment | Mid-turn `previous_pos` advancement |
| Trust C++ `verify_battles` alone while it disagrees with `sim/` | Align tools or gate on Python |

---

## 7. Quick links

| Want | Open |
|------|------|
| Build / conventions | [GEMINI.md](../GEMINI.md) |
| Human overview | [README.md](../README.md) |
| Turn loop & tolerances | [physics-verification.md](physics-verification.md) |
| Corpus map | [battles/README.md](../battles/README.md) |
| Golden set | [battles/golden_physics_battles/README.md](../battles/golden_physics_battles/README.md) |
| Divergences | [battles/leaderboard_physics_divergences/README.md](../battles/leaderboard_physics_divergences/README.md) |
| File lists for scripts | [`docs/agent_pack/`](agent_pack/) |
