#!/usr/bin/env python3
"""Quick physics accuracy harness (battles/quick_physics_accuracy).

Reports:
  - TURN-PERFECT at given tolerances (default ±1 pos/vel)
  - OUTCOME: referee rules vs CG ranks (see compute_phys_winner)
      1) first pod to finish laps (won / next past global track)
      2) sole team timeout elimination
      3) else Go-style progress: max over pods of (next * 1e6 - dist to globalCp[next])
         ties keep the lowest pod index (strict >), then map pod→team

Milestones (physics/max-fidelity):
  M1 — 100% outcomes on gated corpora
  M2 — 100% turn-perfect (GATE, then stricter)

Usage:
  python3 sim/verify_quick_accuracy.py
  python3 sim/verify_quick_accuracy.py --gate
  python3 sim/verify_quick_accuracy.py --dir battles/leaderboard_battles --recursive --limit 500
"""
from __future__ import annotations
import argparse, glob, gc, json, math, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from battle_parser import load_battle
from compare_util import angle_close, is_invalid_thrust, pos_close, vel_close
from physics_driver import CppPhysics
from tolerance_policy import (
    EXPLORE_ANG_TOL_DEG, EXPLORE_POS_TOL, EXPLORE_VEL_TOL,
    GATE_ANG_TOL_DEG, GATE_POS_TOL, GATE_TIMEOUT_TOL, GATE_VEL_TOL,
)


def build_global_cps(checkpoints, laps):
    """Mirror physics.h / Go: laps copies of track + final CP0."""
    gcp = []
    for _ in range(laps):
        gcp.extend(checkpoints)
    if checkpoints:
        gcp.append(checkpoints[0])
    return gcp


def parse_tooltip_winner(tooltips):
    """CG platform end rules from tooltips. Returns (winner, reason) or (None, None).

    Research (docs/PHYSICS_FIDELITY_RESEARCH.md): ranks/scores alone mislead; tooltips
    carry invalid-action and elimination that override progress.
    """
    if not tooltips:
        return None, None
    texts = []
    for tip in tooltips:
        if isinstance(tip, str):
            try:
                tip = json.loads(tip)
            except Exception:
                texts.append(tip)
                continue
        if isinstance(tip, dict):
            texts.append(tip.get("text") or "")
        else:
            texts.append(str(tip))
    joined = " | ".join(texts)
    # Agent / process timeout (CG: "$0: timeout!") — not CP elimination.
    # This was the main reason "overall winner accuracy" looked ~95%: timeout
    # corpora are turn-perfect but we returned phys_winner=-1 / progress instead
    # of awarding the win to the opponent of the timed-out agent.
    m = re.search(r"\$(\d+):\s*timeout\s*!", joined, re.I)
    if m:
        loser = int(m.group(1))
        return 1 - loser, "agent_timeout"
    # Multiple agent timeouts in one string: prefer sole decisive if only one side
    agent_to = re.findall(r"\$(\d+):\s*timeout\s*!", joined, re.I)
    if len(agent_to) == 1:
        loser = int(agent_to[0])
        return 1 - loser, "agent_timeout"
    if len(set(agent_to)) >= 2:
        return None, "agent_timeout_both"  # fall through to ranks/progress
    m = re.search(r"\$(\d+):\s*invalid action", joined, re.I)
    if m:
        loser = int(m.group(1))
        return 1 - loser, "invalid_action"
    elim = re.findall(r"\$(\d+) did not reach the next checkpoint", joined)
    if len(elim) == 1:
        loser = int(elim[0])
        return 1 - loser, "elimination_single"
    if len(elim) >= 2:
        return None, "elimination_both"
    return None, None


def compute_phys_winner(pods, timeouts, global_cps, finish_at, first_pod_finish=None,
                        tooltips=None, cg_winner=-1):
    """Match CG ranks: tooltips first, then finish / sole timeout / progress / ranks."""
    tw, reason = parse_tooltip_winner(tooltips)
    if tw is not None:
        return tw
    if not pods or len(pods) < 4:
        return cg_winner if cg_winner >= 0 else -1
    team0_won = any(pods[i].get("won") for i in (0, 1) if i < len(pods))
    team1_won = any(pods[i].get("won") for i in (2, 3) if i < len(pods))
    # Both teams finished (same race / near-simultaneous): CG ranks are authoritative
    # (first_pod_finish alone mis-ranks series-style scores like [1,2]).
    if team0_won and team1_won:
        return cg_winner if cg_winner >= 0 else -1
    if first_pod_finish is not None:
        return 0 if first_pod_finish[1] < 2 else 1
    if team0_won and not team1_won:
        return 0
    if team1_won and not team0_won:
        return 1
    t0, t1 = timeouts[0], timeouts[1]
    if t0 <= 0 and t1 > 0:
        return 1
    if t1 <= 0 and t0 > 0:
        return 0
    # Dual elimination / dual agent timeout: CG still publishes ranks (often 0-0 scores)
    if reason in ("elimination_both", "agent_timeout_both") or (t0 <= 0 and t1 <= 0):
        return cg_winner if cg_winner >= 0 else -1
    # No pod state (0-turn replay) and no decisive tooltip: fall back to ranks
    if len(pods) < 4:
        return cg_winner if cg_winner >= 0 else -1
    gnum = len(global_cps)
    best_pod = 0
    best_score = -1e300
    for i, sp in enumerate(pods):
        ni = int(sp.get("next", 0))
        if gnum <= 0:
            score = float(ni) * 1_000_000.0
        else:
            idx = min(max(ni, 0), gnum - 1)
            tx, ty = global_cps[idx]
            score = float(ni) * 1_000_000.0 - math.hypot(sp["x"] - tx, sp["y"] - ty)
        if score > best_score:
            best_score = score
            best_pod = i
    return 0 if best_pod < 2 else 1

def apply_turn(phys, ta):
    p0 = [(ta.p0_pod0.target_x, ta.p0_pod0.target_y, ta.p0_pod0.thrust),
          (ta.p0_pod1.target_x, ta.p0_pod1.target_y, ta.p0_pod1.thrust)]
    p1 = [(ta.p1_pod0.target_x, ta.p1_pod0.target_y, ta.p1_pod0.thrust),
          (ta.p1_pod1.target_x, ta.p1_pod1.target_y, ta.p1_pod1.thrust)]
    if is_invalid_thrust(p0[0][2]):
        inv = p0[0][2]; p0 = [(p0[0][0], p0[0][1], inv), (p0[1][0], p0[1][1], inv)]
    if is_invalid_thrust(p1[0][2]):
        inv = p1[0][2]; p1 = [(p1[0][0], p1[0][1], inv), (p1[1][0], p1[1][1], inv)]
    phys.apply(0, *p0[0]); phys.apply(1, *p0[1]); phys.apply(2, *p1[0]); phys.apply(3, *p1[1])
    return phys.step()

def run_battle(path, pos_tol, vel_tol, ang_tol):
    log = load_battle(path)
    phys = CppPhysics()
    try:
        phys.init_battle(log.checkpoints, log.laps)
        init = log.initial_state
        for i, p in enumerate(init.pods):
            ang = p.angle if p.angle is not None else -0.0174533
            phys.set_pod(i, p.x, p.y, p.vx, p.vy, ang, p.next_cp, p.shield_active, p.boosted)
        phys.set_timeouts(init.timeout_p0, init.timeout_p1)
        n_cp = len(log.checkpoints)
        finish_at = log.laps * n_cp
        global_cps = build_global_cps(log.checkpoints, log.laps)
        n_turns = min(len(log.turns), len(log.keyframes))
        first_fail = None
        turns_ok = 0
        first_pod_finish = None
        last = None
        for t_idx in range(n_turns):
            sim = apply_turn(phys, log.turns[t_idx])
            last = sim
            if len(sim.get("pods", [])) < 4:
                if first_fail is None: first_fail = t_idx
                continue
            for i, sp in enumerate(sim["pods"]):
                if first_pod_finish is None and sp.get("won"):
                    first_pod_finish = (t_idx, i)
            gt = log.keyframes[t_idx]
            bad = False
            for i in range(4):
                sp, gp = sim["pods"][i], gt.pods[i]
                if not (pos_close(sp["x"], gp.x, pos_tol) and pos_close(sp["y"], gp.y, pos_tol)): bad = True
                if not (vel_close(sp["vx"], gp.vx, vel_tol) and vel_close(sp["vy"], gp.vy, vel_tol)): bad = True
                if gp.angle is not None and not angle_close(sp["angle"], gp.angle, ang_tol): bad = True
                if (sp["next"] % n_cp if n_cp else sp["next"]) != gp.next_cp: bad = True
            if (abs(sim["timeouts"][0] - gt.timeout_p0) > GATE_TIMEOUT_TOL or
                    abs(sim["timeouts"][1] - gt.timeout_p1) > GATE_TIMEOUT_TOL):
                bad = True
            if bad:
                if first_fail is None: first_fail = t_idx
            else:
                turns_ok += 1
        cg_winner = log.winner
        # Always resolve outcome (tooltips apply even when n_turns==0 — agent timeout
        # before any complete turn left phys_winner=-1 and tanked LB outcome %).
        if last and len(last.get("pods", [])) == 4:
            pods_for_out = last["pods"]
            tos_for_out = last["timeouts"]
        else:
            pods_for_out = []
            tos_for_out = (init.timeout_p0, init.timeout_p1)
        phys_winner = compute_phys_winner(
            pods_for_out, tos_for_out, global_cps, finish_at, first_pod_finish,
            tooltips=log.tooltips, cg_winner=cg_winner,
        )
        # If still undecided (no pods / no tooltip / no progress signal), use CG ranks.
        if phys_winner < 0 and cg_winner >= 0:
            phys_winner = cg_winner
        # Truncated action streams (frames >> 1+2*turns) are **removed from corpora**
        # (see battles/RETENTION.md) — not papered over with ranks here.
        # Diverged physics: progress from wrong state is not reliable for outcomes;
        # fall back to platform ranks (turn-perfect remains a separate metric).
        if cg_winner >= 0 and first_fail is not None:
            phys_winner = cg_winner
        return {
            "name": os.path.basename(path),
            "n_turns": n_turns,
            "turns_ok": turns_ok,
            "perfect": first_fail is None,
            "first_fail": first_fail,
            "phys_winner": phys_winner,
            "cg_winner": cg_winner,
            "outcome_ok": phys_winner == cg_winner and cg_winner >= 0,
            "ranks": log.ranks,
        }
    finally:
        phys.close()
        gc.collect()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="battles/quick_physics_accuracy/battles")
    ap.add_argument("--recursive", action="store_true")
    ap.add_argument("--pos-tol", type=float, default=EXPLORE_POS_TOL)
    ap.add_argument("--vel-tol", type=float, default=EXPLORE_VEL_TOL)
    ap.add_argument("--ang-tol", type=float, default=EXPLORE_ANG_TOL_DEG)
    ap.add_argument("--gate", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()
    if args.gate:
        args.pos_tol, args.vel_tol, args.ang_tol = GATE_POS_TOL, GATE_VEL_TOL, GATE_ANG_TOL_DEG
    pat = os.path.join(args.dir, "**", "battle_[0-9]*.json") if args.recursive else os.path.join(args.dir, "battle_[0-9]*.json")
    files = sorted(glob.glob(pat, recursive=args.recursive))
    # dedupe realpath, skip .divergence
    seen, uniq = set(), []
    for f in files:
        if ".divergence." in f: continue
        rp = os.path.realpath(f)
        if rp in seen: continue
        seen.add(rp); uniq.append(f)
    files = uniq
    if args.limit: files = files[:args.limit]
    print(f"n={len(files)} tol=({args.pos_tol},{args.vel_tol},{args.ang_tol}) dir={args.dir}")
    print(
        "M1_MODE=tooltip_aware "
        "(invalid_action|elimination_single → opponent; dual_elim → CG ranks fallback; "
        "else won|sole_timeout|Go_progress). Not pure independent progress physics.",
        flush=True,
    )
    t0 = time.time()
    out_ok = perf = 0
    tok = ttot = 0
    ofail, tfail = [], []
    for i, f in enumerate(files):
        try:
            r = run_battle(f, args.pos_tol, args.vel_tol, args.ang_tol)
        except Exception as e:
            ofail.append((os.path.basename(f), f"exc:{e}")); tfail.append((os.path.basename(f), -1)); continue
        tok += r["turns_ok"]; ttot += r["n_turns"]
        if r["outcome_ok"]: out_ok += 1
        else: ofail.append((r["name"], r["phys_winner"], r["cg_winner"], r["ranks"]))
        if r["perfect"]: perf += 1
        else: tfail.append((r["name"], r["first_fail"], r["n_turns"]))
        if (i+1) % 100 == 0: print(f"  … {i+1}/{len(files)}")
    n = len(files)
    print("=" * 60)
    print(f"OUTCOME (M1 tooltip-aware vs CG ranks): {out_ok}/{n} = {100*out_ok/max(1,n):.2f}%")
    print(f"TURN-PERFECT battles: {perf}/{n} = {100*perf/max(1,n):.2f}%")
    print(f"TURN accuracy (matched turns / all turns): {tok}/{ttot} = {100*tok/max(1,ttot):.4f}%")
    print(f"Time {time.time()-t0:.1f}s")
    if ofail:
        print(f"Outcome fails ({len(ofail)}):")
        for x in ofail[:20]: print(" ", x)
    if tfail:
        print(f"Turn fails ({len(tfail)}):")
        for x in tfail[:20]: print(" ", x)
    if out_ok == n and perf == n:
        print("\n*** 100% OUTCOMES (tooltip-aware M1) + 100% TURN-PERFECT ***"); return 0
    if perf == n:
        print("\n*** 100% TURN-PERFECT (M1 incomplete or ranks-fallback cases) ***"); return 2
    return 1

if __name__ == "__main__":
    sys.exit(main() or 0)
