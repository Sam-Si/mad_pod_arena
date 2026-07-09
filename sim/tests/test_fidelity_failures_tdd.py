#!/usr/bin/env python3
"""
TDD suite for physics fidelity failures (physics/max-fidelity).

RED → GREEN: each case encodes exact INPUT (init + per-turn actions) and
EXPECTED OUTPUT (CG keyframe pods + timeouts) from battle corpora.

Acceptance: every turn must match within GATE tolerances (pos≤5, vel≤3, ang≤1°,
timeout≤1, exact local next_cp). Target ≥99.9% of (cases × turns) passing,
and 100% of cases fully turn-perfect; also no regression on gate corpus.

Run:
  python3 sim/tests/test_fidelity_failures_tdd.py
  python3 -m pytest sim/tests/test_fidelity_failures_tdd.py -q
"""
from __future__ import annotations

import json
import os
import sys
import unittest

_SIM = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
_ROOT = os.path.normpath(os.path.join(_SIM, ".."))
sys.path.insert(0, _SIM)

from compare_util import angle_close, is_invalid_thrust, pos_close, vel_close  # noqa: E402
from physics_driver import CppPhysics  # noqa: E402
from tolerance_policy import (  # noqa: E402
    GATE_ANG_TOL_DEG,
    GATE_POS_TOL,
    GATE_TIMEOUT_TOL,
    GATE_VEL_TOL,
)

FIXTURE_DIR = os.path.join(os.path.dirname(__file__), "fixtures", "fidelity_failures")
MANIFEST_PATH = os.path.join(FIXTURE_DIR, "MANIFEST.json")


def _load_manifest():
    with open(MANIFEST_PATH) as f:
        return json.load(f)


def _load_fixture(battle_id: str) -> dict:
    path = os.path.join(FIXTURE_DIR, f"battle_{battle_id}.json")
    with open(path) as f:
        return json.load(f)


def _apply_actions(phys: CppPhysics, actions: list) -> dict:
    """Mirror verify_quick_accuracy invalid-thrust propagation."""
    # actions: 4 dicts tx,ty,thrust for pods 0..3
    thrusts = [a["thrust"] for a in actions]
    # player0 pods 0,1 — if pod0 invalid, both get invalid thrust token
    p0 = [(actions[0]["tx"], actions[0]["ty"], thrusts[0]),
          (actions[1]["tx"], actions[1]["ty"], thrusts[1])]
    p1 = [(actions[2]["tx"], actions[2]["ty"], thrusts[2]),
          (actions[3]["tx"], actions[3]["ty"], thrusts[3])]
    if is_invalid_thrust(p0[0][2]):
        inv = p0[0][2]
        p0 = [(p0[0][0], p0[0][1], inv), (p0[1][0], p0[1][1], inv)]
    if is_invalid_thrust(p1[0][2]):
        inv = p1[0][2]
        p1 = [(p1[0][0], p1[0][1], inv), (p1[1][0], p1[1][1], inv)]
    phys.apply(0, *p0[0])
    phys.apply(1, *p0[1])
    phys.apply(2, *p1[0])
    phys.apply(3, *p1[1])
    return phys.step()


def _compare_turn(sim: dict, expected: dict, n_cp: int, turn: int) -> list:
    errs = []
    epods = expected["pods"]
    for i in range(4):
        sp = sim["pods"][i]
        gp = epods[i]
        if not (pos_close(sp["x"], gp["x"], GATE_POS_TOL) and pos_close(sp["y"], gp["y"], GATE_POS_TOL)):
            errs.append(f"t{turn} pod{i} pos sim=({sp['x']},{sp['y']}) exp=({gp['x']},{gp['y']})")
        if not (vel_close(sp["vx"], gp["vx"], GATE_VEL_TOL) and vel_close(sp["vy"], gp["vy"], GATE_VEL_TOL)):
            errs.append(f"t{turn} pod{i} vel sim=({sp['vx']},{sp['vy']}) exp=({gp['vx']},{gp['vy']})")
        ga = gp.get("angle")
        if ga is not None and not angle_close(sp["angle"], ga, GATE_ANG_TOL_DEG):
            errs.append(f"t{turn} pod{i} ang sim={sp['angle']} exp={ga}")
        nloc = sp["next"] % n_cp if n_cp else sp["next"]
        if nloc != gp["next_cp"]:
            errs.append(f"t{turn} pod{i} next_cp sim={nloc} (g={sp['next']}) exp={gp['next_cp']}")
    if (abs(sim["timeouts"][0] - expected["timeout_p0"]) > GATE_TIMEOUT_TOL or
            abs(sim["timeouts"][1] - expected["timeout_p1"]) > GATE_TIMEOUT_TOL):
        errs.append(
            f"t{turn} timeouts sim={sim['timeouts']} "
            f"exp=({expected['timeout_p0']},{expected['timeout_p1']})"
        )
    return errs


def run_fixture_case(fx: dict) -> tuple[bool, int, int, list]:
    """Returns (fully_perfect, turns_ok, turns_total, error_lines)."""
    phys = CppPhysics()
    try:
        cps = [tuple(c) for c in fx["checkpoints"]]
        phys.init_battle(cps, fx["laps"])
        init = fx["initial"]
        for i, p in enumerate(init["pods"]):
            phys.set_pod(
                i, p["x"], p["y"], p["vx"], p["vy"], p["angle_rad"],
                p["next_cp"], p["shield"], p["boosted"],
            )
        phys.set_timeouts(init["timeout_p0"], init["timeout_p1"])
        n_cp = fx["n_checkpoints"]
        turns_ok = 0
        errors = []
        for rec in fx["turns"]:
            sim = _apply_actions(phys, rec["actions"])
            errs = _compare_turn(sim, rec["expected"], n_cp, rec["turn"])
            if errs:
                if len(errors) < 12:
                    errors.extend(errs[:4])
            else:
                turns_ok += 1
        total = len(fx["turns"])
        return turns_ok == total, turns_ok, total, errors
    finally:
        phys.close()


class TestFidelityFailureFixtures(unittest.TestCase):
    """One test method per failing battle: full turn-perfect under GATE."""

    @classmethod
    def setUpClass(cls):
        cls.manifest = _load_manifest()
        cls.case_ids = [c["battle_id"] for c in cls.manifest["cases"]]


def _make_test(battle_id: str):
    def test_fn(self):
        fx = _load_fixture(battle_id)
        # Document contract in assertion message
        self.assertEqual(fx["battle_id"], battle_id)
        self.assertIn("initial", fx)
        self.assertIn("turns", fx)
        self.assertGreater(len(fx["turns"]), 0)
        # INPUT: fx['initial'] + fx['turns'][t]['actions']
        # EXPECTED: fx['turns'][t]['expected'] keyframe state
        perfect, ok, total, errors = run_fixture_case(fx)
        msg = (
            f"battle_{battle_id}: turn-perfect {ok}/{total} "
            f"(first_fail_recorded={fx.get('first_fail_turn_0based')})\n"
            + "\n".join(errors[:8])
        )
        self.assertTrue(perfect, msg)

    test_fn.__name__ = f"test_battle_{battle_id}_turn_perfect_GATE"
    test_fn.__doc__ = (
        f"INPUT: init pods/timeouts + {battle_id} actions each turn. "
        f"EXPECTED: CG keyframe pos/vel/ang/next_cp/timeouts within GATE."
    )
    return test_fn


# Dynamically attach tests (TDD: these fail until physics matches CG)
for _bid in [
    "885827873", "885912413", "885928301", "886449550", "886469116",
    "887715689", "887820683", "890666841", "890670385", "891370461",
]:
    setattr(
        TestFidelityFailureFixtures,
        f"test_battle_{_bid}_turn_perfect_GATE",
        _make_test(_bid),
    )


class TestFidelityAcceptanceAggregate(unittest.TestCase):
    """≥99.9% of all fixture turns must pass (aggregate acceptance)."""

    def test_aggregate_turn_acceptance_99_9_percent(self):
        manifest = _load_manifest()
        ok = tot = 0
        failed_cases = []
        for c in manifest["cases"]:
            fx = _load_fixture(c["battle_id"])
            perfect, turns_ok, turns_tot, _ = run_fixture_case(fx)
            ok += turns_ok
            tot += turns_tot
            if not perfect:
                failed_cases.append((c["battle_id"], turns_ok, turns_tot))
        rate = 100.0 * ok / max(1, tot)
        # Also require gate corpus still 100% if env set (optional slow)
        msg = f"turn acceptance {ok}/{tot} = {rate:.4f}% failed_cases={failed_cases}"
        self.assertGreaterEqual(rate, 99.9, msg)


class TestGateCorpusNoRegression(unittest.TestCase):
    """Gate A must stay 100% (regression guard) — samples via verify_battles path optional.

    Fast check: ensure tolerance constants unchanged (policy lock).
    Full gate is run in CI; here we lock GATE_* numbers used by fixtures.
    """

    def test_gate_tolerances_locked(self):
        self.assertEqual(GATE_POS_TOL, 5.0)
        self.assertEqual(GATE_VEL_TOL, 3.0)
        self.assertEqual(GATE_ANG_TOL_DEG, 1.0)
        self.assertEqual(GATE_TIMEOUT_TOL, 1)


def main():
    # Print RED/GREEN summary for agent workflows
    manifest = _load_manifest()
    ok = tot = cases_pass = 0
    print("TDD fidelity failure suite")
    print("Fixtures:", FIXTURE_DIR)
    for c in manifest["cases"]:
        fx = _load_fixture(c["battle_id"])
        perfect, turns_ok, turns_tot, errors = run_fixture_case(fx)
        ok += turns_ok
        tot += turns_tot
        status = "PASS" if perfect else "FAIL"
        if perfect:
            cases_pass += 1
        print(f"  [{status}] battle_{c['battle_id']} turns {turns_ok}/{turns_tot}")
        if not perfect and errors:
            print("       ", errors[0][:120])
    rate = 100.0 * ok / max(1, tot)
    print(f"Cases perfect: {cases_pass}/{len(manifest['cases'])}")
    print(f"Turn acceptance: {ok}/{tot} = {rate:.4f}% (need ≥99.9%)")
    # unittest
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    return 0 if result.wasSuccessful() and rate >= 99.9 else 1


if __name__ == "__main__":
    sys.exit(main())
