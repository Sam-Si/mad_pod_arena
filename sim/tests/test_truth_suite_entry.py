#!/usr/bin/env python3
"""Truth-suite entry must exist and policy modules must pass (Fowler safety net)."""
from __future__ import annotations

import os
import subprocess
import sys
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))


class TestTruthSuiteEntry(unittest.TestCase):
    def test_run_truth_suite_script_exists_and_executable(self):
        path = os.path.join(ROOT, "tools", "run_truth_suite.sh")
        self.assertTrue(os.path.isfile(path), "tools/run_truth_suite.sh missing")
        self.assertTrue(os.access(path, os.X_OK), "run_truth_suite.sh not executable")

    def test_ssot_policy_main_exits_zero(self):
        r = subprocess.run(
            [sys.executable, os.path.join(ROOT, "sim", "check_ssot_policy.py")],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        self.assertIn("OK", r.stdout)

    def test_verification_policy_main_exits_zero(self):
        r = subprocess.run(
            [sys.executable, os.path.join(ROOT, "sim", "check_verification_policy.py")],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)

    def test_shared_world_step_is_only_definition(self):
        def read(rel: str) -> str:
            with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
                return f.read()

        world = read("src/physics/fidelity_world_step.h")
        self.assertIn("inline void simulateFidelityWorld", world)
        defs = [ln for ln in world.splitlines() if "inline void simulateFidelityWorld" in ln]
        self.assertEqual(len(defs), 1)
        self.assertIn("simulateFidelityWorld", read("src/physics/physics.h"))
        self.assertIn("simulateFidelityWorld", read("src/physics/fast_physics.h"))


if __name__ == "__main__":
    unittest.main()
