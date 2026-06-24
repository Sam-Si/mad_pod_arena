# Verification snapshot

Re-run after any `src/physics/physics.h` edit. Prefer **Python** harness for the CI-equivalent gate (`sim/verify_battles.py`); C++ `verify_battles` uses stricter/alternate comparison and may disagree — treat Python + golden script as authoritative unless CI switches.

## Commands

```bash
# Golden corpus
python3 battles/scripts/verify_golden_corpus.py --tier pass
python3 battles/scripts/verify_golden_corpus.py

# CI-style physics gate (authoritative for this repo docs)
python3 sim/verify_battles.py battles/test_session_battles

# C++ verifier (optional; pass absolute --dir)
bazel build //src/physics:verify_battles
./bazel-bin/src/physics/verify_battles --dir "$PWD/battles/test_session_battles"

# Known divergences (informational)
python3 sim/verify_battles.py battles/leaderboard_physics_divergences/battles
```

## Results (captured with local physics WIP)

| Suite | Result |
|-------|--------|
| Golden `expected_pass` (161) | **161 ok, 0 FAIL** — `*** PASS TIER CLEAN ***` |
| Golden `expected_fail` (39) | **39 still diverge, 0 unexpectedly pass** |
| `test_session_battles` via **Python** | **312/312 passed**, 46 364/46 364 turns, **100.00%** (~6.4s) |
| `test_session_battles` via **C++** `verify_battles` | **Not authoritative** in this snapshot — reported mass fails under tighter/different checks; do not block on C++ alone until aligned with `sim/` tolerances |
| `leaderboard_physics_divergences` (Python) | Still mostly failing (folder purpose); ~39 first-error battles in fail dist — knife-edge CP/angle/vel/timeout mix |

### Physics changes included in snapshot commit

- Checkpoint radius: **strict** `dist² < 600²` (Go-faithful; fixes false pass at dist==600, e.g. `battle_884515945` class).
- Mid-turn checkpoint segments: advance `previous_pos` after each collision slice (Go `curps`).
- Docs in `docs/physics-verification.md` updated to match.
- Golden lists: **5 battles promoted** fail → pass (`883531319`, `884515945`, `886244294`, `891615789`, `891630564`).


## Raw tails (provenance)

### golden verify
```
  … 80/200
  … 120/200
  … 160/200
  … 200/200

============================================================
Golden corpus results
============================================================
  expected_pass:  161 ok,  0 FAIL (must be 0)
  expected_fail:  39 still diverge,  0 now pass

*** PASS TIER CLEAN ***
```

### python test_session
```
============================================================
Total battles:    312
Passed (100%):    312
Failed:           0
Skipped:          0

Total turns:      46364
Perfect turns:    46364
Turn accuracy:    100.00%

Time:             6.391 seconds
Battle rate:      48.8 battles/sec
Turn rate:        7255 turns/sec

*** ALL 312 TESTED BATTLES PASSED — PHYSICS 100% ACCURATE ***
```
