# Leaderboard physics divergences

Battles where `src/physics/physics.h` does **not** match the CodinGame referee
keyframe-for-keyframe within verifier tolerances.

Generated: 2026-06-23 18:46 UTC
Source log: `/tmp/lb_final.log`
Count: **44** battles (copied from `leaderboard_battles/`, originals kept in place)

## How failures were measured (formula)

1. Parse battle JSON: frame 0 = init state; each game turn = player stdout actions + keyframe `view` ground truth.
2. Drive `src/physics/physics.h` via `sim/replay_driver` with **exact** init state and **exact** actions every turn.
3. After each turn, compare sim vs referee keyframe using tolerances in `sim/verify_battles.py`:

| Field | Pass condition |
|-------|----------------|
| position `x,y` | `abs(sim - gt) <= 5.0` |
| velocity `vx,vy` | `abs(sim - gt) <= 3` |
| angle (radians) | shortest angle distance `<= 1.0°` |
| `next_cp` | `sim.next % n_checkpoints == gt.next_cp` |
| player timeouts | `abs(sim - gt) <= 1` |

4. **First failing turn** stops the battle. That turn index + error string are recorded below / in sidecars.

Run yourself:
```bash
python3 sim/verify_battles.py battles/leaderboard_battles
# or only this subset:
python3 sim/verify_battles.py battles/leaderboard_physics_divergences/battles
```

## Layout

```
leaderboard_physics_divergences/
├── README.md              ← this file
├── manifest.csv           ← one row per battle: turn + error types + summary
└── battles/
    ├── battle_XXXX.json           ← copy of replay
    └── battle_XXXX.divergence.json  ← first_fail_turn, error, tolerances
```

## Error type breakdown

- **checkpoint**: 19
- **timeout**: 19
- **position**: 14
- **angle**: 11
- **velocity**: 10

## Index (sorted by first_fail_turn)

| Battle | Fail turn | / total | Types | Summary |
|--------|-----------|---------|-------|---------|
| `battle_884515945.json` | **9** | 322 | checkpoint, timeout | pod0 next_cp sim=2(mod=2) gt=1; timeouts sim=(100, 90) gt=(90, 90) |
| `battle_884486784.json` | **20** | 491 | angle, velocity | pod2 vel sim=(79,-126) gt=(80,-121); pod2 angle sim=162.7° gt=160.8° |
| `battle_885988689.json` | **21** | 251 | angle | pod3 angle sim=-15.5° gt=-16.8° |
| `battle_891617954.json` | **33** | 153 | checkpoint, timeout | pod2 next_cp sim=6(mod=2) gt=1; timeouts sim=(91, 100) gt=(91, 92) |
| `battle_891370461.json` | **38** | 383 | position | pod1 pos sim=(5281,2542) gt=(5273,2547) Δ=(8.0,-5.0) |
| `battle_884278521.json` | **39** | 256 | checkpoint, timeout | pod1 next_cp sim=5(mod=5) gt=4; timeouts sim=(100, 92) gt=(91, 92) |
| `battle_883531319.json` | **43** | 251 | checkpoint, timeout | pod2 next_cp sim=3(mod=3) gt=2; timeouts sim=(89, 100) gt=(89, 98) |
| `battle_872334774.json` | **59** | 168 | checkpoint, timeout | pod2 next_cp sim=4(mod=0) gt=1; timeouts sim=(92, 92) gt=(92, 100) |
| `battle_886369830.json` | **59** | 148 | checkpoint, timeout | pod1 next_cp sim=6(mod=0) gt=2; timeouts sim=(100, 94) gt=(88, 94) |
| `battle_872238192.json` | **60** | 199 | checkpoint, timeout | pod1 next_cp sim=4(mod=0) gt=3; timeouts sim=(100, 95) gt=(88, 95) |
| `battle_885987701.json` | **61** | 184 | angle | pod1 angle sim=-89.4° gt=-88.1° |
| `battle_886469116.json` | **64** | 273 | position | pod2 pos sim=(4837,6096) gt=(4835,6103) Δ=(2.0,-7.0) |
| `battle_885912413.json` | **69** | 70 | position, velocity | pod1 pos sim=(5939,2211) gt=(6013,2073) Δ=(-74.0,138.0); pod1 vel sim=(-34,110) gt=(52,-52 |
| `battle_891630564.json` | **75** | 163 | checkpoint, timeout | pod3 next_cp sim=8(mod=0) gt=3; timeouts sim=(97, 100) gt=(97, 87) |
| `battle_888294828.json` | **77** | 233 | checkpoint, timeout | pod0 next_cp sim=4(mod=0) gt=1; timeouts sim=(89, 90) gt=(100, 90) |
| `battle_885647025.json` | **84** | 101 | checkpoint, timeout | pod3 next_cp sim=6(mod=2) gt=1; timeouts sim=(98, 100) gt=(98, 96) |
| `battle_885990456.json` | **84** | 342 | angle | pod1 angle sim=90.0° gt=88.3° |
| `battle_885827873.json` | **86** | 187 | angle | pod1 angle sim=-87.7° gt=-88.8° |
| `battle_885928301.json` | **86** | 126 | angle | pod1 angle sim=-51.6° gt=-52.8° |
| `battle_891616683.json` | **88** | 227 | checkpoint, timeout | pod0 next_cp sim=10(mod=4) gt=3; timeouts sim=(100, 98) gt=(89, 98) |
| `battle_885922662.json` | **98** | 109 | position | pod3 pos sim=(12742,2955) gt=(12742,2949) Δ=(0.0,6.0) |
| `battle_875046794.json` | **109** | 254 | position | pod1 pos sim=(7515,-205) gt=(7509,-205) Δ=(6.0,0.0) |
| `battle_886244294.json` | **112** | 148 | checkpoint, timeout | pod2 next_cp sim=12(mod=0) gt=3; timeouts sim=(56, 100) gt=(56, 96) |
| `battle_890666841.json` | **120** | 448 | position, velocity | pod3 pos sim=(5746,7086) gt=(5750,7075) Δ=(-4.0,11.0); pod3 vel sim=(913,384) gt=(915,375) |
| `battle_885647126.json` | **122** | 249 | position, velocity | pod3 pos sim=(4107,1993) gt=(4113,1991) Δ=(-6.0,2.0); pod3 vel sim=(2,565) gt=(7,564) |
| `battle_882151685.json` | **137** | 225 | position, velocity | pod2 pos sim=(3962,5128) gt=(3962,5135) Δ=(0.0,-7.0); pod2 vel sim=(-502,-77) gt=(-501,-72 |
| `battle_891617303.json` | **144** | 336 | checkpoint, timeout | pod0 next_cp sim=10(mod=2) gt=1; timeouts sim=(100, 87) gt=(85, 87) |
| `battle_887820683.json` | **162** | 277 | velocity | pod0 vel sim=(557,193) gt=(552,200) |
| `battle_886734512.json` | **166** | 227 | angle | pod1 angle sim=-53.9° gt=-54.9° |
| `battle_886274562.json` | **170** | 338 | angle | pod3 angle sim=89.4° gt=88.3° |
| `battle_886455391.json` | **170** | 299 | checkpoint, timeout | pod2 next_cp sim=14(mod=2) gt=1; timeouts sim=(42, 100) gt=(42, 90) |
| `battle_883532591.json` | **177** | 210 | checkpoint, timeout | pod3 next_cp sim=12(mod=2) gt=1; timeouts sim=(94, 100) gt=(94, 95) |
| `battle_888655750.json` | **188** | 272 | checkpoint, timeout | pod1 next_cp sim=9(mod=1) gt=2; timeouts sim=(90, 82) gt=(100, 82) |
| `battle_891626075.json` | **193** | 196 | checkpoint, timeout | pod2 next_cp sim=14(mod=2) gt=1; timeouts sim=(97, 100) gt=(97, 95) |
| `battle_891628176.json` | **198** | 336 | checkpoint, timeout | pod0 next_cp sim=10(mod=0) gt=4; timeouts sim=(100, 69) gt=(88, 69) |
| `battle_886449550.json` | **210** | 381 | position, angle, velocity | pod0 pos sim=(-457,4982) gt=(-450,4987) Δ=(-7.0,-5.0); pod0 vel sim=(-111,-94) gt=(-109,-8 |
| `battle_885624120.json` | **216** | 239 | position, velocity | pod1 pos sim=(8556,6938) gt=(8556,7014) Δ=(0.0,-76.0); pod1 vel sim=(64,39) gt=(64,141); p |
| `battle_890670385.json` | **234** | 304 | position, velocity | pod3 pos sim=(12145,2213) gt=(12150,2206) Δ=(-5.0,7.0); pod3 vel sim=(-25,154) gt=(-21,153 |
| `battle_888427968.json` | **250** | 272 | position | pod0 pos sim=(8926,5152) gt=(8927,5158) Δ=(-1.0,-6.0) |
| `battle_888529621.json` | **260** | 335 | position, angle | pod3 pos sim=(8476,20073) gt=(8470,20071) Δ=(6.0,2.0); pod3 angle sim=-155.3° gt=-153.9° |
| `battle_885928141.json` | **303** | 482 | velocity | pod2 vel sim=(43,121) gt=(47,119) |
| `battle_885930561.json` | **309** | 320 | position | pod3 pos sim=(4196,6845) gt=(4197,6851) Δ=(-1.0,-6.0) |
| `battle_891615789.json` | **352** | 359 | checkpoint, timeout | pod3 next_cp sim=12(mod=2) gt=1; timeouts sim=(89, 100) gt=(89, 97) |
| `battle_887715689.json` | **422** | 476 | angle | pod3 angle sim=112.9° gt=111.7° |
