// Phase 2 OQ2 harness: 18 maps × 50 turns in-process A/B exact on csb::Game Fidelity,
// plus ≥1 map vs replay_driver subprocess within GATE_*.
#include "src/physics/physics.h"
#include "src/core/maps/catalog.h"
#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <array>
#include <memory>

static int failures = 0;
#define EXPECT_TRUE(c) do { if (!(c)) { std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #c << "\n"; ++failures; } } while (0)

// GATE_* from sim/tolerance_policy.py (policy-owned; do not loosen here).
static constexpr double kGatePos = 5.0;
static constexpr double kGateVel = 3.0;
static constexpr double kGateAngRad = 1.0 * M_PI / 180.0;  // 1 degree in radians
static constexpr int kGateTimeout = 1;

static std::vector<csb::Point> TrackFromRaw(int map_idx) {
    const auto& raw = GetTournamentMapsRaw()[static_cast<size_t>(map_idx)];
    std::vector<csb::Point> t;
    t.reserve(raw.size());
    for (const auto& p : raw) t.push_back({p.x, p.y});
    return t;
}

static void ApplyScriptedTurn(csb::Game& g, int turn) {
    const auto& track = g.track;
    for (int p = 0; p < 4; ++p) {
        size_t idx = static_cast<size_t>((turn + p) % static_cast<int>(track.size()));
        int tx = static_cast<int>(track[idx].x);
        int ty = static_cast<int>(track[idx].y);
        g.applyAction(p, tx, ty, "200");
    }
    g.step(csb::StepOptions{csb::PhysicsProfile::Fidelity});
}

static void ExpectGamesEqual(const csb::Game& a, const csb::Game& b) {
    for (int i = 0; i < 4; ++i) {
        const auto& pa = a.pods[i];
        const auto& pb = b.pods[i];
        EXPECT_TRUE(pa.p.x == pb.p.x && pa.p.y == pb.p.y);
        EXPECT_TRUE(pa.s.x == pb.s.x && pa.s.y == pb.s.y);
        EXPECT_TRUE(pa.angle == pb.angle);
        EXPECT_TRUE(pa.next == pb.next);
        EXPECT_TRUE(pa.shieldtimer == pb.shieldtimer);
        EXPECT_TRUE(pa.boosted == pb.boosted);
        EXPECT_TRUE(pa.won == pb.won);
    }
    EXPECT_TRUE(a.playerTimeout[0] == b.playerTimeout[0]);
    EXPECT_TRUE(a.playerTimeout[1] == b.playerTimeout[1]);
}

static void test_ab_all_maps() {
    int nmaps = GetTournamentMapCount();
    EXPECT_TRUE(nmaps >= 18);
    for (int m = 0; m < 18; ++m) {
        auto track = TrackFromRaw(m);
        csb::Game A, B;
        A.initialize(track, 3);
        B.initialize(track, 3);
        for (int t = 1; t <= 50; ++t) {
            ApplyScriptedTurn(A, t);
            ApplyScriptedTurn(B, t);
            ExpectGamesEqual(A, B);
        }
    }
    std::cout << "arena_fidelity_trace: 18 maps × 50 turns A/B exact ok\n";
}

// In-process: step(Fidelity) vs nextTurn() (driver uses nextTurn after applyAction).
static void test_one_map_step_vs_nextturn() {
    auto track = TrackFromRaw(0);
    csb::Game via_step, via_next;
    via_step.initialize(track, 3);
    via_next.initialize(track, 3);
    for (int t = 1; t <= 20; ++t) {
        for (int p = 0; p < 4; ++p) {
            size_t idx = static_cast<size_t>((t + p) % track.size());
            int tx = static_cast<int>(track[idx].x);
            int ty = static_cast<int>(track[idx].y);
            via_step.applyAction(p, tx, ty, "200");
            via_next.applyAction(p, tx, ty, "200");
        }
        via_step.step(csb::StepOptions{csb::PhysicsProfile::Fidelity});
        via_next.nextTurn();
        for (int i = 0; i < 4; ++i) {
            EXPECT_TRUE(via_step.pods[i].p.x == via_next.pods[i].p.x);
            EXPECT_TRUE(via_step.pods[i].p.y == via_next.pods[i].p.y);
            EXPECT_TRUE(via_step.pods[i].s.x == via_next.pods[i].s.x);
            EXPECT_TRUE(via_step.pods[i].s.y == via_next.pods[i].s.y);
        }
        EXPECT_TRUE(via_step.playerTimeout[0] == via_next.playerTimeout[0]);
    }
    std::cout << "arena_fidelity_trace: map0 step vs nextTurn ok\n";
}

// Find replay_driver binary for subprocess GATE case.
static std::string FindReplayDriver() {
    const char* env = std::getenv("REPLAY_DRIVER");
    if (env && env[0]) {
        if (std::FILE* f = std::fopen(env, "r")) { std::fclose(f); return env; }
    }
    const char* candidates[] = {
        "sim/replay_driver",
        "./sim/replay_driver",
        "bazel-bin/src/physics/replay_driver",
        "../physics/replay_driver",
    };
    for (const char* c : candidates) {
        if (std::FILE* f = std::fopen(c, "r")) { std::fclose(f); return c; }
    }
    // Bazel runfiles
    const char* rf = std::getenv("TEST_SRCDIR");
    const char* wn = std::getenv("TEST_WORKSPACE");
    if (rf && wn) {
        std::string p = std::string(rf) + "/" + wn + "/src/physics/replay_driver";
        if (std::FILE* f = std::fopen(p.c_str(), "r")) { std::fclose(f); return p; }
        // Also under test's runfiles for data dependency
        p = std::string(rf) + "/" + wn + "/bazel-out";
    }
    // Runfile from data dep //src/physics:replay_driver
    if (rf) {
        // Common patterns: TEST_SRCDIR/_main/src/physics/replay_driver or external
        for (const char* ws : {"_main", "mad_pod_arena", wn ? wn : "_main"}) {
            if (!ws) continue;
            std::string p = std::string(rf) + "/" + ws + "/src/physics/replay_driver";
            if (std::FILE* f = std::fopen(p.c_str(), "r")) { std::fclose(f); return p; }
        }
    }
    return {};
}

// Map 0 / 20 turns: in-process Game vs replay_driver text protocol within GATE_*.
static void test_map0_vs_replay_driver_gate() {
    std::string driver = FindReplayDriver();
    if (driver.empty()) {
        std::cerr << "FAIL: replay_driver not found (set REPLAY_DRIVER or build //src/physics:replay_driver)\n";
        ++failures;
        return;
    }
    auto track = TrackFromRaw(0);
    csb::Game ref;
    ref.initialize(track, 3);

    // Build command script for driver via popen write — use temp file for input.
    std::string script_path = "/tmp/arena_fidelity_driver_script.txt";
    {
        std::FILE* sf = std::fopen(script_path.c_str(), "w");
        EXPECT_TRUE(sf != nullptr);
        if (!sf) return;
        fprintf(sf, "INIT %zu", track.size());
        for (const auto& cp : track) fprintf(sf, " %.0f %.0f", cp.x, cp.y);
        fprintf(sf, " 3\n");
        for (int t = 1; t <= 20; ++t) {
            for (int p = 0; p < 4; ++p) {
                size_t idx = static_cast<size_t>((t + p) % track.size());
                int tx = static_cast<int>(track[idx].x);
                int ty = static_cast<int>(track[idx].y);
                fprintf(sf, "ACTION %d %d %d 200\n", p, tx, ty);
            }
            fprintf(sf, "STEP\n");
        }
        fprintf(sf, "QUIT\n");
        std::fclose(sf);
    }

    // Run driver, capture full stdout
    std::string cmd = driver + " < " + script_path + " 2>/dev/null";
    std::array<char, 4096> buf{};
    std::string out;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    EXPECT_TRUE(pipe != nullptr);
    if (!pipe) return;
    while (fgets(buf.data(), (int)buf.size(), pipe.get())) out += buf.data();

    // Parse STEP_DONE blocks: each has 4 pod lines + TIMEOUTS + STEP_DONE
    std::istringstream iss(out);
    std::string line;
    int turns_compared = 0;
    for (int t = 1; t <= 20; ++t) {
        for (int p = 0; p < 4; ++p) {
            size_t idx = static_cast<size_t>((t + p) % track.size());
            ref.applyAction(p, (int)track[idx].x, (int)track[idx].y, "200");
        }
        ref.nextTurn();

        // Advance to next set of pod lines after STEP responses
        // Protocol emits OK ACTION / READY noise; look for lines starting with pod index digit
        // Better: find TIMEOUTS lines and back up — collect all lines matching pod state format
    }

    // Re-parse: after each STEP_DONE we have just printed 4 pods
    // Walk line by line; when we see a line starting with "0 " that looks like pod state after READY,
    // collect 4 consecutive pod lines.
    std::vector<std::string> lines;
    {
        std::istringstream is2(out);
        std::string ln;
        while (std::getline(is2, ln)) lines.push_back(ln);
    }

    // Reference game re-sim
    csb::Game gref;
    gref.initialize(track, 3);
    size_t li = 0;
    for (int t = 1; t <= 20; ++t) {
        for (int p = 0; p < 4; ++p) {
            size_t idx = static_cast<size_t>((t + p) % track.size());
            gref.applyAction(p, (int)track[idx].x, (int)track[idx].y, "200");
        }
        gref.nextTurn();

        // Find next block of 4 pod lines followed by TIMEOUTS
        int pods_found = 0;
        double dx[4], dy[4], dvx[4], dvy[4], dang[4];
        int dnext[4], dsh[4], dbo[4];
        int dt0 = 0, dt1 = 0;
        while (li < lines.size()) {
            const std::string& ln = lines[li++];
            if (ln.rfind("TIMEOUTS ", 0) == 0) {
                std::sscanf(ln.c_str(), "TIMEOUTS %d %d", &dt0, &dt1);
                // Expect STEP_DONE next
                continue;
            }
            if (ln == "STEP_DONE") {
                if (pods_found == 4) break;
                continue;
            }
            // Pod line: idx x y vx vy angle next shield boosted [won]
            int idx = -1;
            double x, y, ang;
            int vx, vy, nxt, sh, bo, won = 0;
            int n = std::sscanf(ln.c_str(), "%d %lf %lf %d %d %lf %d %d %d %d",
                                &idx, &x, &y, &vx, &vy, &ang, &nxt, &sh, &bo, &won);
            if (n >= 9 && idx >= 0 && idx < 4) {
                dx[idx] = x; dy[idx] = y; dvx[idx] = vx; dvy[idx] = vy;
                dang[idx] = ang; dnext[idx] = nxt; dsh[idx] = sh; dbo[idx] = bo;
                pods_found++;
            }
        }
        EXPECT_TRUE(pods_found == 4);
        if (pods_found != 4) {
            std::cerr << "driver parse failed at turn " << t << "\n";
            break;
        }
        for (int i = 0; i < 4; ++i) {
            EXPECT_TRUE(std::fabs(gref.pods[i].p.x - dx[i]) <= kGatePos);
            EXPECT_TRUE(std::fabs(gref.pods[i].p.y - dy[i]) <= kGatePos);
            EXPECT_TRUE(std::fabs(gref.pods[i].s.x - dvx[i]) <= kGateVel);
            EXPECT_TRUE(std::fabs(gref.pods[i].s.y - dvy[i]) <= kGateVel);
            EXPECT_TRUE(std::fabs(gref.pods[i].angle - dang[i]) <= kGateAngRad);
        }
        EXPECT_TRUE(std::abs(gref.playerTimeout[0] - dt0) <= kGateTimeout);
        EXPECT_TRUE(std::abs(gref.playerTimeout[1] - dt1) <= kGateTimeout);
        ++turns_compared;
    }
    EXPECT_TRUE(turns_compared == 20);
    std::cout << "arena_fidelity_trace: map0 vs replay_driver within GATE_* ("
              << turns_compared << " turns) ok\n";
}

int main() {
    test_ab_all_maps();
    test_one_map_step_vs_nextturn();
    test_map0_vs_replay_driver_gate();
    if (failures) {
        std::cout << "arena_fidelity_trace_test: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "arena_fidelity_trace_test: all passed\n";
    return 0;
}
