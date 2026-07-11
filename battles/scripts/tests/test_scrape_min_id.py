#!/usr/bin/env python3
"""Unit tests for scrape_leaderboard min-id / retention eligibility (real shipped helper)."""
import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_SCRIPTS = os.path.dirname(_HERE)
if _SCRIPTS not in sys.path:
    sys.path.insert(0, _SCRIPTS)

from scrape_leaderboard import MIN_BATTLE_ID, _eligible_game_id  # noqa: E402


class TestEligibleGameId(unittest.TestCase):
    def test_retention_floor_exclusive(self):
        self.assertFalse(_eligible_game_id(MIN_BATTLE_ID))
        self.assertFalse(_eligible_game_id(MIN_BATTLE_ID - 1))
        self.assertTrue(_eligible_game_id(MIN_BATTLE_ID + 1))

    def test_min_id_strictly_greater(self):
        # Local max id used in investigation
        max_id = 894832693
        self.assertFalse(_eligible_game_id(max_id, min_id=max_id))
        self.assertFalse(_eligible_game_id(max_id - 1, min_id=max_id))
        self.assertTrue(_eligible_game_id(max_id + 1, min_id=max_id))

    def test_invalid_ids(self):
        self.assertFalse(_eligible_game_id(None))
        self.assertFalse(_eligible_game_id("not-a-number"))


if __name__ == "__main__":
    unittest.main()
