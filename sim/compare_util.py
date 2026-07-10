"""Shared compare helpers for gate and diagnostic Python verifiers."""

from __future__ import annotations

import math
from typing import Any, Optional


def pos_close(a, b, tol) -> bool:
    return abs(a - b) <= tol


def vel_close(a, b, tol) -> bool:
    return abs(a - b) <= tol


def angle_close(a_rad, b_rad, tol_deg) -> bool:
    """Shortest arc on circle (pre-task verify_battles / compare_battle semantics)."""
    da = abs(a_rad - b_rad)
    da = min(da, 2 * math.pi - da)
    return da <= (tol_deg * math.pi / 180.0)


def angle_delta_rad(a_rad: float, b_rad: float) -> float:
    """Shortest absolute angle delta in radians."""
    da = abs(a_rad - b_rad)
    return min(da, 2 * math.pi - da)


def angle_close_eps(a_rad: float, b_rad: float, eps_rad: float) -> bool:
    return angle_delta_rad(a_rad, b_rad) <= eps_rad


def is_invalid_thrust(thrust_str) -> bool:
    """SHIELD/BOOST => False; int not in [0, 200] => True; parse error => False."""
    if thrust_str in ("SHIELD", "BOOST"):
        return False
    try:
        val = int(thrust_str)
        return val < 0 or val > 200
    except (ValueError, TypeError):
        return False


def compare_pods_exact(
    sim_pods: list,
    gt_pods: list,
    sim_timeouts,
    gt_timeouts,
    n_checkpoints: int = 0,
    *,
    ang_eps_rad: float,
) -> tuple[list[str], dict[str, float]]:
    """
    Zero-tolerance pos/vel/timeout/next_cp; angle within ang_eps_rad (float/text noise).

    Returns (error strings, max abs deltas dict).
    """
    errs: list[str] = []
    maxd = {
        "dx": 0.0,
        "dy": 0.0,
        "dvx": 0.0,
        "dvy": 0.0,
        "dang_rad": 0.0,
        "dang_deg": 0.0,
        "dtimeout": 0.0,
    }
    for i in range(min(4, len(sim_pods), len(gt_pods))):
        sp = sim_pods[i]
        gp = gt_pods[i]
        gx = float(gp.x if hasattr(gp, "x") else gp["x"])
        gy = float(gp.y if hasattr(gp, "y") else gp["y"])
        gvx = int(gp.vx if hasattr(gp, "vx") else gp["vx"])
        gvy = int(gp.vy if hasattr(gp, "vy") else gp["vy"])
        ga = gp.angle if hasattr(gp, "angle") else gp.get("angle")
        gn = int(gp.next_cp if hasattr(gp, "next_cp") else gp.get("next_cp", gp.get("next")))

        sx, sy = float(sp["x"]), float(sp["y"])
        svx, svy = int(sp["vx"]), int(sp["vy"])
        sa = float(sp["angle"])
        sn = int(sp["next"])

        dx, dy = abs(sx - gx), abs(sy - gy)
        dvx, dvy = abs(svx - gvx), abs(svy - gvy)
        maxd["dx"] = max(maxd["dx"], dx)
        maxd["dy"] = max(maxd["dy"], dy)
        maxd["dvx"] = max(maxd["dvx"], float(dvx))
        maxd["dvy"] = max(maxd["dvy"], float(dvy))

        if dx != 0.0 or dy != 0.0:
            errs.append(
                f"pod{i} pos EXACT miss: sim=({sx!r},{sy!r}) gt=({gx!r},{gy!r}) "
                f"Δ=({dx},{dy})"
            )
        if dvx != 0 or dvy != 0:
            errs.append(
                f"pod{i} vel EXACT miss: sim=({svx},{svy}) gt=({gvx},{gvy}) "
                f"Δ=({dvx},{dvy})"
            )

        if ga is not None:
            dang = angle_delta_rad(sa, float(ga))
            maxd["dang_rad"] = max(maxd["dang_rad"], dang)
            maxd["dang_deg"] = max(maxd["dang_deg"], math.degrees(dang))
            if dang > ang_eps_rad:
                errs.append(
                    f"pod{i} ang EXACT miss: sim={sa!r} gt={float(ga)!r} "
                    f"Δrad={dang:.3e} Δdeg={math.degrees(dang):.3e} (eps={ang_eps_rad})"
                )

        sim_cp = sn % n_checkpoints if n_checkpoints > 0 else sn
        gt_cp = gn % n_checkpoints if n_checkpoints > 0 else gn
        if sim_cp != gt_cp:
            errs.append(f"pod{i} next_cp EXACT miss: sim={sn}(mod={sim_cp}) gt={gn}(mod={gt_cp})")

    dt0 = abs(int(sim_timeouts[0]) - int(gt_timeouts[0]))
    dt1 = abs(int(sim_timeouts[1]) - int(gt_timeouts[1]))
    maxd["dtimeout"] = float(max(dt0, dt1))
    if dt0 != 0 or dt1 != 0:
        errs.append(f"timeouts EXACT miss: sim={sim_timeouts} gt={gt_timeouts}")

    return errs, maxd


def merge_maxd(acc: dict[str, float], inc: dict[str, float]) -> None:
    for k, v in inc.items():
        acc[k] = max(acc.get(k, 0.0), v)
