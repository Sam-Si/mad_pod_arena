#!/usr/bin/env python3
"""Fowler safety net: static SSOT / smell guards. Run from repo root.

Enforces single ownership for:
- Numeric law (core/constants.h) vs mirrors (fast.h, ga_prelude) and aliases (fidelity_math)
- Fidelity world-step (fidelity_world_step.h only — no second collision loop in façades)
- Maps catalog (core/maps/catalog.h only — no physics/maps.h)
- BotConfig (cg/bot_config.h; not engine/bot.h)
- No resurrected PhysicsSimulator / GAPhysicsSimulator / experiments/cg_rust
- CG amalgam target still present in BUILD
"""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def fail(msg: str) -> None:
    print(f"SSOT FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def read(rel: str) -> str:
    path = os.path.join(ROOT, rel)
    if not os.path.isfile(path):
        fail(f"missing {rel}")
    with open(path, encoding="utf-8") as f:
        return f.read()


def parse_cpp_float_const(text: str, name: str) -> float:
    # inline constexpr double name = 0.85;
    m = re.search(
        rf"inline constexpr\s+(?:double|int)\s+{re.escape(name)}\s*=\s*([0-9eE+.\-]+)\s*;",
        text,
    )
    if not m:
        fail(f"could not find constexpr {name}")
    return float(m.group(1))


def parse_any_float_const(text: str, name: str) -> float:
    """Match static/inline/plain constexpr double|int name = value."""
    m = re.search(
        rf"(?:static\s+)?(?:inline\s+)?constexpr\s+(?:double|int)\s+{re.escape(name)}\s*=\s*([0-9eE+.\-]+)\s*;",
        text,
    )
    if not m:
        fail(f"could not find constexpr {name}")
    return float(m.group(1))


def main() -> None:
    constants = read("src/core/constants.h")
    fast = read("src/physics/fast.h")
    fidelity_math = read("src/physics/fidelity_math.h")
    build = read("src/cg/BUILD.bazel")
    gemini = read("GEMINI.md")
    ssot = read("docs/SSOT.md")
    prelude = read("src/cg/internal/ga_prelude_and_search.inc")

    # --- Constants SSOT: core owns values; mirrors must match ---
    core_friction = parse_cpp_float_const(constants, "kFriction")
    core_min_impulse = parse_cpp_float_const(constants, "kMinImpulse")
    # kPodCollisionRsq is often written as 800.0 * 800.0
    m_rsq = re.search(
        r"kPodCollisionRsq\s*=\s*([0-9eE+.\-]+)\s*\*\s*([0-9eE+.\-]+)",
        constants,
    )
    if m_rsq:
        core_pod_rsq = float(m_rsq.group(1)) * float(m_rsq.group(2))
    else:
        core_pod_rsq = parse_cpp_float_const(constants, "kPodCollisionRsq")

    m = re.search(
        r"inline constexpr double kFriction\s*=\s*([0-9eE+.\-]+)\s*;",
        fast,
    )
    if not m:
        fail("fast.h missing mirrored kFriction")
    if float(m.group(1)) != core_friction:
        fail(f"friction mismatch core={core_friction} fast.h={m.group(1)}")

    for name, core_val, pattern in (
        ("kMinImpulse", core_min_impulse, r"inline constexpr double kMinImpulse\s*=\s*([0-9eE+.\-]+)"),
        ("kPodCollisionRsq", core_pod_rsq, r"inline constexpr double kPodCollisionRsq\s*=\s*([0-9eE+.\-]+)"),
    ):
        mm = re.search(pattern, fast)
        if not mm:
            fail(f"fast.h missing mirrored {name}")
        if float(mm.group(1)) != core_val:
            fail(f"{name} mismatch core={core_val} fast.h={mm.group(1)}")

    if "csb_constants::kFriction" not in fidelity_math:
        fail("fidelity_math.h must alias csb_constants::kFriction")

    # ga_prelude: only restate friction for free-flight eval (collision law is fast.h in amalgam)
    if parse_any_float_const(prelude, "kCgFriction") != core_friction:
        fail("ga_prelude kCgFriction must match csb_constants::kFriction")

    # Forbidden deleted owners in active agent map
    if re.search(r"^\s*-\s+\*\*`?PhysicsSimulator`?\*\*:", gemini, re.M):
        fail("GEMINI.md still documents PhysicsSimulator as live API")
    if "GAPhysicsSimulator" in gemini and "Do not reintroduce" not in gemini and "deleted" not in gemini.lower():
        if "reintroduce" not in gemini:
            fail("GEMINI.md must not present GAPhysicsSimulator as live")

    if "cg_bot_amalgam" not in build:
        fail("src/cg/BUILD.bazel missing cg_bot_amalgam genrule")
    if "fast.h" not in build:
        fail("amalgam must list fast.h as source")

    if "src/core/constants.h" not in ssot:
        fail("docs/SSOT.md must list constants.h ownership")

    if os.path.isdir(os.path.join(ROOT, "experiments", "cg_rust")):
        fail("experiments/cg_rust must not return without SSOT update")

    # Maps: only catalog.h (physics/maps.h must stay deleted)
    if os.path.isfile(os.path.join(ROOT, "src", "physics", "maps.h")):
        fail("src/physics/maps.h must stay deleted; maps SSOT is src/core/maps/catalog.h")
    if not os.path.isfile(os.path.join(ROOT, "src", "core", "maps", "catalog.h")):
        fail("missing maps SSOT src/core/maps/catalog.h")

    # Single world-step owner; façades must not re-own bounce/forward loops
    phys = read("src/physics/physics.h")
    fp = read("src/physics/fast_physics.h")
    world = read("src/physics/fidelity_world_step.h")
    if "simulateFidelityWorld" not in world:
        fail("fidelity_world_step.h missing simulateFidelityWorld")
    if "simulateFidelityWorld" not in phys:
        fail("physics.h Game must call simulateFidelityWorld")
    if "simulateFidelityWorld" not in fp:
        fail("fast_physics.h Game must call simulateFidelityWorld")
    # Dead dual bounce on Game façades (worldBounce lives only in fidelity_world_step)
    if re.search(r"void\s+bounce\s*\(", phys):
        fail("physics.h must not redefine bounce; use fidelity_world_step only")
    if re.search(r"void\s+bounce\s*\(", fp):
        fail("fast_physics.h must not redefine bounce; use fidelity_world_step only")
    if "worldBounce" not in world:
        fail("fidelity_world_step.h must own worldBounce")

    # No second full collision while-loop in façades (heuristic: SimulateTurn-style local loops ok only in fast.h)
    if "GetCollisionTime" in phys or "GetCollisionTime" in fp:
        fail("Fidelity façades must not own GetCollisionTime (fast fragment only)")

    # Fidelity rotate/thrust/move SSOT: both façades must call shared entry points;
    # law lives in fidelity_math.h only (no second lattice / mid-band rotate).
    for name in (
        "applyFidelityRotate",
        "applyFidelityThrust",
        "applyFidelityMove",
    ):
        if name not in fidelity_math:
            fail(f"fidelity_math.h missing SSOT {name}")
        if name not in phys:
            fail(f"physics.h must call {name} (no local rotate/thrust/move law)")
        if name not in fp:
            fail(f"fast_physics.h must call {name} (no local rotate/thrust/move law)")
    # Façades must not re-inline nextafter thrust lattice (law is applyFidelityThrust).
    if re.search(r"std::nextafter\s*\(", phys):
        fail("physics.h must not own nextafter lattice; use applyFidelityThrust")
    if re.search(r"std::nextafter\s*\(", fp):
        fail("fast_physics.h must not own nextafter lattice; use applyFidelityThrust")
    # Fidelity applyRotate must only delegate (no local da / mid-band body).
    # GA helpers (applyRotateByClampedDelta) may still clamp angle deltas — not law.
    for label, text in (("physics.h", phys), ("fast_physics.h", fp)):
        m = re.search(
            r"(?:CSB_FP_INLINE\s+)?void\s+applyRotate\s*\([^)]*\)\s*\{([^}]*)\}",
            text,
            re.S,
        )
        if not m:
            fail(f"{label} missing applyRotate wrapper")
        body = m.group(1)
        if "applyFidelityRotate" not in body:
            fail(f"{label} applyRotate must call applyFidelityRotate")
        if "nextafter" in body or "kMaxRotate" in body:
            fail(f"{label} applyRotate must not re-own rotate law (delegate only)")

    # Bot modularization markers
    if not os.path.isfile(os.path.join(ROOT, "src/cg/internal/ga_prelude_and_search.inc")):
        fail("missing modular ga_prelude_and_search.inc")
    if not os.path.isfile(os.path.join(ROOT, "src/cg/bot_config.h")):
        fail("missing src/cg/bot_config.h")
    if not os.path.isfile(os.path.join(ROOT, "src/cg/ga_pure.h")):
        fail("missing src/cg/ga_pure.h (bot pure scoring SSOT)")
    pure = read("src/cg/ga_pure.h")
    if "namespace ga_pure" not in pure or "ClampAngleShiftDeg" not in pure:
        fail("ga_pure.h must own ClampAngleShiftDeg in namespace ga_pure")
    if "ga_pure.h" not in build:
        fail("amalgam BUILD must list ga_pure.h for CG paste")
    eng_bot = read("src/engine/bot.h")
    if "struct BotConfig" in eng_bot:
        fail("engine/bot.h must not own BotConfig")

    # engine must not hardcode friction law (use constants)
    eng_cpp = read("src/engine/engine.cpp")
    if re.search(r"\*\s*0\.85\b", eng_cpp) or re.search(r"0\.85\s*\*", eng_cpp):
        fail("engine.cpp must not hardcode 0.85; use csb_constants::kFriction")
    if "CheckpointCollide" in eng_cpp:
        fail("engine must not own CheckpointCollide; use csb::cpCollide")

    print("check_ssot_policy: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
