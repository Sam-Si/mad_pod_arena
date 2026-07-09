# Feature branch learning scores (bot strategy / infra)

Scored separately from battle **physics** corpora. Base ref: `main`.
Current integration branch: `feature/phase1-coevolution-ibr`.

Dimensions (0–10): domain depth (search/bot), iteration signal, transferability,
low noise vs `main`, completeness, recency/integration with physics monorepo.

| Branch | Commits vs `main` | cg | engine | physics | tournament | battles files | Total /60 | Notes |
|--------|------------------:|---:|-------:|--------:|-----------:|--------------:|----------:|-------|
| `feature/phase1-coevolution-ibr` | ~18 | 4 | 8 | 7 | 6 | ~23970 | **54** | IBR + MCTS budget + full physics/battles absorption; **best agent home** |
| `feature/phase2-hybrid-rm-mcts` | ~4 | 2 | 7 | 0 | 6 | 0 | **48** | Hybrid regret matching / MCTS phase 2; best *search-algorithm* delta |
| `feature/local-dev` | ~3 | 2 | 7 | 0 | 6 | 0 | **40** | DRY engine physics checkpoint |
| `feature/minimax-coevolution` | ~1 | 2 | 1 | 0 | 6 | 0 | **32** | Early unify bot + evasion weight |
| `feature/benchmark-tournament` | ~1 WIP | 2 | 1 | 0 | 2 | 0 | **28** | Tournament infra; incomplete |
| `main` | 0 | 0 | 0 | 0 | 0 | 0 | **12** | Pre-monorepo baseline |

## Notable commits (strategy)

| SHA | Branch lineage | Subject |
|-----|----------------|---------|
| `3cab7a4f` | phase1 | Phase 1 Co-evolutionary IBR search |
| `b582b5ae` | phase1 | Configurable time budget and MCTS optimizations phase 1 |
| `9c7bcf09` | phase2 | Phase 2 Hybrid Regret Matching search |
| `58e6bf1b` | minimax / shared base | Unify bot logic, runner_evasion_weight, parallel benchmark |
| `cbb417fa` | local-dev / phase2 | DRY single SSoT for engine physics |
| `1743f8fa` | phase1 | Golden ~200 battle corpus pass/fail tiers |

## Recommendation

- **Implement / extend bots:** work on `feature/phase1-coevolution-ibr` (or merge phase2 search into it).
- **Study search only:** diff `phase1`…`phase2-hybrid-rm-mcts` focusing on `src/cg` + `src/engine`.
- **Study referee physics:** use `docs/agent-battle-curriculum.md` Tier S/A — not this table.
