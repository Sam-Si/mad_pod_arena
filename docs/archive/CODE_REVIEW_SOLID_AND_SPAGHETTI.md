# DO NOT IMPLEMENT FROM THIS FILE

**Wrong mission if you only read the filename (SOLID / spaghetti).**  
**Active task:** verification governance only → **[`../VERIFICATION_TRUTH_POLICY.md`](../VERIFICATION_TRUTH_POLICY.md)**

## Negative scope (do **not** do under that task)

- Split / modularize `src/cg/cg_bot.cpp` or remove include-`.cpp` tournament glue  
- Quarantine/`csb_physics.h` as main work (unless zero-scope drive-by)  
- Merge `src/engine` physics with `src/physics/physics.h` or make GA ≈ referee  
- CG_STANDALONE codegen / standalone drift CI (F2)  
- Tighten `GATE_POS_TOL` / promote C++ 0.01 compare to merge gate (F3)  
- Sweep `docs/agent_pack/**` or `docs/research/**`  
- Delete C++ `verify_battles` “for purity”  
- Rename job id `physics-accuracy` without updating policy + checker  

## What the active task **does**

Honest **`MERGE_PHYSICS_OK`** (compound Python gate + golden pass tier + `test_physics`), diagnostics labeled, tolerances in `sim/tolerance_policy.py`, driver fail-closed under `MAD_POD_GATE_STRICT`, static `sim/check_verification_policy.py`.

## Historical SOLID / spaghetti autopsy

Not maintained here. Recover from **git history of this path** if you need the old multi-phase inventory (search commits touching `CODE_REVIEW_SOLID_AND_SPAGHETTI.md`). That text is **not** an implementation program for the active task.
