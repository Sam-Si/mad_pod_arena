#!/usr/bin/env python3
"""SHIELD keyframe recovery: stdout 200 vs keyframe shield+null targets."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from battle_parser import load_battle
from physics_driver import CppPhysics
from verify_battles import run_battle

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
BATTLE = os.path.join(ROOT, "battles", "golden_physics_battles", "battles", "battle_885912413.json")


def test_885912413_turn_perfect_after_shield_recovery():
    assert os.path.isfile(BATTLE), BATTLE
    log = load_battle(BATTLE)
    # Recovered SHIELD on the fail turn
    ta = log.turns[69]
    assert ta.p1_pod0.thrust == "SHIELD" or ta.p1_pod1.thrust == "SHIELD", (
        ta.p1_pod0.thrust,
        ta.p1_pod1.thrust,
    )
    phys = CppPhysics()
    phys.init_battle(list(log.checkpoints), log.laps)
    fail, errs, total, perfect = run_battle(phys, log)
    assert fail is None, (fail, errs)
    assert perfect == total and total > 0


if __name__ == "__main__":
    test_885912413_turn_perfect_after_shield_recovery()
    print("test_shield_recovery: ok")
