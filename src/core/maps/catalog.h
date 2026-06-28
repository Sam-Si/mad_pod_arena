#pragma once
// SSOT: tournament / arena map catalog (18 CG-captured maps).
// Coordinates byte-identical to former arena.cpp ALL_MAPS.
// Does NOT include engine headers (avoids Bazel cycles). Convert to Vec2 at use site.
// Go 13-map list is an ALTERNATE catalog, not a subset — see docs/SSOT.md.

#include <vector>
#include <utility>

struct TournamentMapPoint {
    double x;
    double y;
};

inline const std::vector<std::vector<TournamentMapPoint>>& GetTournamentMapsRaw() {
    static const std::vector<std::vector<TournamentMapPoint>> kMaps = {
        // Map 0
        { { 12929, 7191 }, { 5614, 2557 }, { 4114, 7440 }, { 13515, 2340 } },
        // Map 1
        { { 13584, 7626 }, { 12449, 1355 }, { 10519, 6003 }, { 3593, 5174 } },
        // Map 2
        { { 14075, 7765 }, { 13888, 1202 }, { 10257, 4931 }, { 6104, 2204 }, { 3049, 5211 }, { 6260, 7743 } },
        // Map 3
        { { 9426, 7247 }, { 5962, 4254 }, { 14674, 1436 }, { 3442, 7215 } },
        // Map 4
        { { 5033, 5264 }, { 11472, 6064 }, { 9081, 1865 } },
        // Map 5
        { { 13095, 2313 }, { 4579, 2152 }, { 7377, 4920 }, { 3303, 7243 }, { 14551, 7688 }, { 10577, 5043 } },
        // Map 6
        { { 2645, 7027 }, { 10065, 5940 }, { 13925, 1916 }, { 8004, 3244 } },
        // Map 7
        { { 14633, 1420 }, { 3428, 7230 }, { 9449, 7224 }, { 5962, 4253 } },
        // Map 8
        { { 3031, 5179 }, { 6271, 7752 }, { 14096, 7753 }, { 13873, 1231 }, { 10258, 4890 }, { 6128, 2203 } },
        // Map 9
        { { 11202, 5412 }, { 7244, 6630 }, { 5403, 2840 }, { 10293, 3376 } },
        // Map 10
        { { 6000, 5375 }, { 11322, 2825 }, { 7508, 6916 } },
        // Map 11
        { { 5406, 2811 }, { 10302, 3339 }, { 11231, 5436 }, { 7267, 6667 } },
        // Map 12
        { { 9547, 1383 }, { 3654, 4439 }, { 7977, 7904 }, { 13322, 5535 } },
        // Map 13
        { { 13310, 5555 }, { 9561, 1374 }, { 3636, 4433 }, { 7981, 7891 } },
        // Map 14
        { { 13283, 5513 }, { 9560, 1394 }, { 3652, 4444 }, { 7997, 7872 } },
        // Map 15
        { { 6306, 7766 }, { 14117, 7743 }, { 13885, 1197 }, { 10229, 4926 }, { 6102, 2199 }, { 2991, 5197 } },
        // Map 16
        { { 13311, 5519 }, { 9585, 1426 }, { 3615, 4419 }, { 7974, 7919 } },
        // Map 17
        { { 12435, 1353 }, { 10563, 5965 }, { 3558, 5170 }, { 13579, 7616 } },
    };
    return kMaps;
}

inline int GetTournamentMapCount() {
    return static_cast<int>(GetTournamentMapsRaw().size());
}
