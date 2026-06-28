"""Shared compare helpers for gate and diagnostic Python verifiers."""

from __future__ import annotations

import math


def pos_close(a, b, tol) -> bool:
    return abs(a - b) <= tol


def vel_close(a, b, tol) -> bool:
    return abs(a - b) <= tol


def angle_close(a_rad, b_rad, tol_deg) -> bool:
    """Shortest arc on circle (pre-task verify_battles / compare_battle semantics)."""
    da = abs(a_rad - b_rad)
    da = min(da, 2 * math.pi - da)
    return da <= (tol_deg * math.pi / 180.0)


def is_invalid_thrust(thrust_str) -> bool:
    """SHIELD/BOOST => False; int not in [0, 200] => True; parse error => False."""
    if thrust_str in ("SHIELD", "BOOST"):
        return False
    try:
        val = int(thrust_str)
        return val < 0 or val > 200
    except (ValueError, TypeError):
        return False
