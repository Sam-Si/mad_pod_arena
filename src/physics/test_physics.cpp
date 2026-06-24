// Unit / smoke tests for csb physics (constants + one-step sanity).
#include "physics.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static int failures = 0;

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #cond << "\n"; failures++; } \
} while (0)

#define EXPECT_NEAR(a, b, tol) EXPECT_TRUE(std::fabs((a) - (b)) <= (tol))

static void test_diff_angle_go_style() {
    csb::Pod p;
    p.p = {0, 0};
    p.angle = 0.0;
    // target straight ahead
    double da = p.diffAngle({100, 0});
    EXPECT_NEAR(da, 0.0, 1e-9);
}

static void test_first_turn_and_thrust() {
    // Single-pod kinematics via applyMove + endTurn (avoid multi-pod collisions in this unit test)
    csb::Pod p;
    p.p = {1000.0, 1000.0};
    p.s = {0, 0};
    p.angle = -1.0;
    csb::PlayerMove m;
    m.target = {2000, 1000};
    m.thrust = 200;
    p.applyMove(m, true);  // first turn snaps toward east
    EXPECT_NEAR(p.s.x, 200.0, 0.5);  // cos(0)*200
    // World step moves position by velocity, then endTurn frictions/rounds
    p.p.x += p.s.x;
    p.p.y += p.s.y;
    p.endTurn();
    EXPECT_NEAR(p.s.x, 170.0, 0.01);  // trunc(200*0.85)
    EXPECT_NEAR(p.p.x, 1200.0, 0.01); // round(1000+200)
}

static void test_shield_blocks_thrust() {
    csb::Pod p;
    p.p = {0, 0};
    p.s = {0, 0};
    p.angle = 0;
    csb::PlayerMove m;
    m.target = {1000, 0};
    m.shield = true;
    p.applyMove(m, false);
    EXPECT_TRUE(p.shieldtimer == 4);
    EXPECT_NEAR(p.s.x, 0.0, 1e-9);
}

static void test_boost_once() {
    csb::Pod p;
    p.p = {0, 0};
    p.s = {0, 0};
    p.angle = 0;
    csb::PlayerMove m;
    m.target = {1000, 0};
    m.boost = true;
    p.applyMove(m, false);
    EXPECT_TRUE(p.boosted == 1);
    EXPECT_NEAR(p.s.x, 650.0, 0.01);

    csb::Pod p2 = p;
    p2.s = {0, 0};
    p2.applyMove(m, false);
    // Second boost should degrade to 200
    EXPECT_NEAR(p2.s.x, 200.0, 0.01);
}

static void test_dest_equals_pos_skips() {
    csb::Pod p;
    p.p = {100, 100};
    p.s = {50, 0};
    p.angle = 1.23;
    csb::PlayerMove m;
    m.target = {100, 100};
    m.thrust = 200;
    double ang_before = p.angle;
    p.applyMove(m, false);
    EXPECT_NEAR(p.angle, ang_before, 1e-12);
    EXPECT_NEAR(p.s.x, 50.0, 1e-12);
}

static void test_timeout_reset_100() {
    int timeouts[2] = {50, 50};
    csb::Pod p;
    p.next = 1;
    p.passCheckpoint(0, 10, timeouts);
    EXPECT_TRUE(timeouts[0] == 100);
}

static void test_parse_move() {
    auto m1 = csb::parseMove("100 200 SHIELD");
    EXPECT_TRUE(m1.shield);
    auto m2 = csb::parseMove("100 200 BOOST");
    EXPECT_TRUE(m2.boost);
    auto m3 = csb::parseMove("100 200 150");
    EXPECT_TRUE(m3.thrust == 150);
    auto m4 = csb::parseMove("100 200 32767");
    EXPECT_TRUE(m4.thrust == 32767);  // not clamped (matches CG passthrough)
    auto m5 = csb::parseMove("100 200");  // incomplete
    EXPECT_TRUE(!m5.valid);
    EXPECT_TRUE(m5.thrust == 0);
}

int main() {
    test_diff_angle_go_style();
    test_first_turn_and_thrust();
    test_shield_blocks_thrust();
    test_boost_once();
    test_dest_equals_pos_skips();
    test_timeout_reset_100();
    test_parse_move();

    if (failures == 0) {
        std::cout << "test_physics: all passed\n";
        return 0;
    }
    std::cout << "test_physics: " << failures << " failure(s)\n";
    return 1;
}
