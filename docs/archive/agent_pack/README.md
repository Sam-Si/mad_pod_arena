# Agent pack — machine-readable indices

Companion to [`../agent-battle-curriculum.md`](../agent-battle-curriculum.md).

| File | Contents |
|------|----------|
| `golden_expected_pass.txt` | Golden battles that must verify clean |
| `golden_expected_fail.txt` | Golden known-divergence filenames |
| `test_session_battles.txt` | Full CI gate battle list (~312) |
| `divergence_battles.txt` | JSON under `leaderboard_physics_divergences/battles/` |
| `divergence_sidecars.txt` | `*.divergence.json` metadata sidecars |
| `VERIFICATION_SNAPSHOT.md` | Last recorded verify results + commands |
| `FEATURE_BRANCH_SCORES.md` | Bot-strategy branch learning scores |

Resolve golden JSON paths as:

`battles/golden_physics_battles/battles/<name from golden_expected_*.txt>`

Resolve divergence paths as:

`battles/leaderboard_physics_divergences/battles/<name>`
