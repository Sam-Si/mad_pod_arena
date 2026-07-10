// Phase 4 minimum: (1) amalgam genrule output is on the test data graph (builds),
// (2) csb::fast (same fragment amalgam embeds) runs a smoke integrate.
// Full P0c against amalgam TU is optional; shared rollout goldens cover Fast semantics.
#include "fast.h"
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>

thread_local bool g_friendly_collision = false;

static bool AmalgamArtifactPresent() {
    // Bazel data runfile for :cg_bot_amalgam output
    const char* rf = std::getenv("TEST_SRCDIR");
    const char* wn = std::getenv("TEST_WORKSPACE");
    if (rf && wn) {
        for (const char* ws : {wn, "_main", "mad_pod_arena"}) {
            if (!ws) continue;
            std::string p = std::string(rf) + "/" + ws + "/src/cg/cg_bot_amalgam.cpp";
            std::ifstream in(p);
            if (in.good()) return true;
        }
    }
    std::ifstream in("src/cg/cg_bot_amalgam.cpp");
    return in.good();
}

int main() {
    // Require amalgam genrule product when running under Bazel test with data dep.
    if (!AmalgamArtifactPresent()) {
        // Still OK if developer runs binary without runfiles — warn only when TEST_SRCDIR set
        if (std::getenv("TEST_SRCDIR")) {
            std::cerr << "amalgam_fast_smoke_test: cg_bot_amalgam.cpp runfile missing\n";
            return 1;
        }
    } else {
        std::cout << "amalgam artifact present\n";
    }

    csb::fast::Pod pods[4];
    for (int i = 0; i < 4; ++i) {
        pods[i].id = i;
        pods[i].team = i < 2 ? 0 : 1;
        pods[i].pos.x = 8000 + i * 100;
        pods[i].pos.y = 4500;
        pods[i].vel.x = (i % 2 == 0) ? 300 : -300;
        pods[i].angle = 0;
    }
    g_friendly_collision = false;
    csb::fast::SimulateTurn(pods);
    if (pods[0].pos.x == 8000 && pods[0].vel.x == 300) {
        std::cerr << "amalgam_fast_smoke: pods did not integrate\n";
        return 1;
    }
    std::cout << "amalgam_fast_smoke_test: ok\n";
    return 0;
}
