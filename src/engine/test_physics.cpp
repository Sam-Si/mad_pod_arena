#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include "src/engine/csb_physics.h"

// Define the old simulator logic as a local copy in this file to benchmark against it
struct OldPod {
    int id;
    int team;
    csb::Vec2 pos, vel;
    double angle;
    int next_cp_id;
    bool boost_available;
    int shield_cd;
    int timeout;
    int laps_completed;

    double Mass() const { return (shield_cd == 4) ? 10.0 : 1.0; }
    void Move(double t) { pos.x += vel.x * t; pos.y += vel.y * t; }
    void EndTurn() {
        pos.x = std::round(pos.x);
        pos.y = std::round(pos.y);
        vel.x = std::trunc(vel.x * 0.85);
        vel.y = std::trunc(vel.y * 0.85);
        if (shield_cd > 0) shield_cd--;
    }
};

double OldGetCollisionTime(const OldPod& p1, const OldPod& p2) {
    double x = p1.pos.x - p2.pos.x;
    double y = p1.pos.y - p2.pos.y;
    double c = x * x + y * y - 640000.0; 
    if (c > 3360000.0) return -1.0;
    double vx = p1.vel.x - p2.vel.x;
    double vy = p1.vel.y - p2.vel.y;
    double a = vx * vx + vy * vy;
    if (a < 0.00001) return -1.0;
    double b = 2.0 * (x * vx + y * vy);
    if (c >= 0.0 && b >= 0.0) return -1.0;
    double delta = b * b - 4.0 * a * c;
    if (delta < 0.0) return -1.0;
    double t = (-b - std::sqrt(delta)) / (2.0 * a);
    if (t < 0.0) return -1.0;
    return t;
}

void OldResolveCollision(OldPod& p1, OldPod& p2) {
    double m1 = p1.Mass();
    double m2 = p2.Mass();
    double mcoeff = (m1 + m2) / (m1 * m2);
    double nx = p1.pos.x - p2.pos.x;
    double ny = p1.pos.y - p2.pos.y;
    double nxnysquare = nx * nx + ny * ny;
    double dvx = p1.vel.x - p2.vel.x;
    double dvy = p1.vel.y - p2.vel.y;
    double product = nx * dvx + ny * dvy;
    double fx = (nx * product) / (nxnysquare * mcoeff);
    double fy = (ny * product) / (nxnysquare * mcoeff);
    p1.vel.x -= fx / m1;
    p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2;
    p2.vel.y += fy / m2;
    double impulse = std::sqrt(fx * fx + fy * fy);
    if (impulse < 120.0) {
        fx = fx * 120.0 / impulse;
        fy = fy * 120.0 / impulse;
    }
    p1.vel.x -= fx / m1;
    p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2;
    p2.vel.y += fy / m2;
}

void OldSimulateTurn(OldPod* p) {
    double t_current = 0.0;
    int col_count = 0;
    while (t_current < 1.0 && col_count < 10) {
        double first_col_t = 2.0;
        OldPod* col_p1 = nullptr;
        OldPod* col_p2 = nullptr;
        double t;
        t = OldGetCollisionTime(p[0], p[1]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[0]; col_p2 = &p[1]; }
        t = OldGetCollisionTime(p[0], p[2]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[0]; col_p2 = &p[2]; }
        t = OldGetCollisionTime(p[0], p[3]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[0]; col_p2 = &p[3]; }
        t = OldGetCollisionTime(p[1], p[2]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[1]; col_p2 = &p[2]; }
        t = OldGetCollisionTime(p[1], p[3]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[1]; col_p2 = &p[3]; }
        t = OldGetCollisionTime(p[2], p[3]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[2]; col_p2 = &p[3]; }
        if (first_col_t > 1.0 - t_current) {
            p[0].Move(1.0 - t_current); p[1].Move(1.0 - t_current); p[2].Move(1.0 - t_current); p[3].Move(1.0 - t_current);
            t_current = 1.0;
            break;
        }
        if (first_col_t < 0.0001) first_col_t = 0.0001;
        p[0].Move(first_col_t); p[1].Move(first_col_t); p[2].Move(first_col_t); p[3].Move(first_col_t);
        if (col_p1 && col_p2) OldResolveCollision(*col_p1, *col_p2);
        t_current += first_col_t;
        col_count++;
    }
    if (t_current < 1.0) {
        p[0].Move(1.0 - t_current); p[1].Move(1.0 - t_current); p[2].Move(1.0 - t_current); p[3].Move(1.0 - t_current);
    }
    p[0].EndTurn(); p[1].EndTurn(); p[2].EndTurn(); p[3].EndTurn();
}

// Simple xorshift generator for fast benchmark data
uint32_t benchmark_rand_state = 123456789;
uint32_t BenchmarkRand() {
    benchmark_rand_state ^= benchmark_rand_state << 13;
    benchmark_rand_state ^= benchmark_rand_state >> 17;
    benchmark_rand_state ^= benchmark_rand_state << 5;
    return benchmark_rand_state;
}
double BenchmarkRandRange(double min, double max) {
    return min + (max - min) * (static_cast<double>(BenchmarkRand()) / static_cast<double>(UINT32_MAX));
}

void RunBenchmark() {
    std::cout << "====================================================\n";
    std::cout << "    CSB PHYSICS SIMULATOR PERFORMANCE BENCHMARK     \n";
    std::cout << "====================================================\n\n";

    constexpr int ITERATIONS = 10000000; // 10 Million iterations
    std::cout << "Pre-generating " << ITERATIONS << " random test scenarios...\n";

    std::vector<std::array<csb::Pod, 4>> new_pods_list(ITERATIONS);
    std::vector<std::array<OldPod, 4>> old_pods_list(ITERATIONS);

    for (int it = 0; it < ITERATIONS; ++it) {
        for (int i = 0; i < 4; ++i) {
            // Generate random positions clustered around a target to trigger collisions
            double px = BenchmarkRandRange(5000, 10000);
            double py = BenchmarkRandRange(3000, 6000);
            double vx = BenchmarkRandRange(-400, 400);
            double vy = BenchmarkRandRange(-400, 400);
            double angle = BenchmarkRandRange(0, 360);
            int shield = (BenchmarkRand() % 10 == 0) ? 4 : 0; // 10% chance of shield

            new_pods_list[it][i].id = i;
            new_pods_list[it][i].team = i / 2;
            new_pods_list[it][i].pos = csb::Vec2(px, py);
            new_pods_list[it][i].vel = csb::Vec2(vx, vy);
            new_pods_list[it][i].angle = angle;
            new_pods_list[it][i].shield_cd = shield;
            new_pods_list[it][i].boost_available = true;

            old_pods_list[it][i].id = i;
            old_pods_list[it][i].team = i / 2;
            old_pods_list[it][i].pos = csb::Vec2(px, py);
            old_pods_list[it][i].vel = csb::Vec2(vx, vy);
            old_pods_list[it][i].angle = angle;
            old_pods_list[it][i].shield_cd = shield;
            old_pods_list[it][i].boost_available = true;
        }
    }

    std::cout << "Scenarios pre-generated successfully.\n\n";

    // 1. Benchmark legacy simulator
    std::cout << "Benchmarking Legacy GA Physics Simulator..." << std::endl;
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < ITERATIONS; ++it) {
        OldSimulateTurn(old_pods_list[it].data());
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    double time_old_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    double old_sps = (ITERATIONS / (time_old_ms / 1000.0));
    std::cout << "Legacy simulator took: " << time_old_ms << " ms (" << old_sps / 1000000.0 << " million states/sec)\n\n";

    // 2. Benchmark new simulator
    std::cout << "Benchmarking New csb_physics::SimulateTurnFast..." << std::endl;
    auto t3 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < ITERATIONS; ++it) {
        csb::PhysicsEngine::SimulateTurnFast(new_pods_list[it].data());
    }
    auto t4 = std::chrono::high_resolution_clock::now();
    double time_new_ms = std::chrono::duration<double, std::milli>(t4 - t3).count();
    double new_sps = (ITERATIONS / (time_new_ms / 1000.0));
    std::cout << "New simulator took: " << time_new_ms << " ms (" << new_sps / 1000000.0 << " million states/sec)\n\n";

    // Verify correctness of new simulator against old simulator
    int error_count = 0;
    for (int it = 0; it < ITERATIONS; ++it) {
        for (int i = 0; i < 4; ++i) {
            double diff_x = std::abs(new_pods_list[it][i].pos.x - old_pods_list[it][i].pos.x);
            double diff_y = std::abs(new_pods_list[it][i].pos.y - old_pods_list[it][i].pos.y);
            double diff_vx = std::abs(new_pods_list[it][i].vel.x - old_pods_list[it][i].vel.x);
            double diff_vy = std::abs(new_pods_list[it][i].vel.y - old_pods_list[it][i].vel.y);

            if (diff_x > 1e-3 || diff_y > 1e-3 || diff_vx > 1e-3 || diff_vy > 1e-3) {
                error_count++;
                if (error_count <= 5) {
                    std::cout << "Correctness mismatch at it " << it << ", pod " << i << ":\n"
                              << "Old pos: (" << old_pods_list[it][i].pos.x << ", " << old_pods_list[it][i].pos.y << "), vel: (" << old_pods_list[it][i].vel.x << ", " << old_pods_list[it][i].vel.y << ")\n"
                              << "New pos: (" << new_pods_list[it][i].pos.x << ", " << new_pods_list[it][i].pos.y << "), vel: (" << new_pods_list[it][i].vel.x << ", " << new_pods_list[it][i].vel.y << ")\n\n";
                }
            }
        }
    }

    std::cout << "----------------------------------------------------\n";
    if (error_count == 0) {
        std::cout << "✅ CORRECTNESS VERIFIED: 100% identical outcomes!\n";
    } else {
        std::cout << "❌ CORRECTNESS MISMATCH: Found " << error_count << " discrepancies!\n";
    }
    std::cout << "🚀 SPEEDUP FACTOR: " << std::fixed << std::setprecision(2) << (time_old_ms / time_new_ms) << "x faster!\n";
    std::cout << "====================================================\n";
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--benchmark") {
        RunBenchmark();
        return 0;
    }

    csb::GetTrigLUT();

    int globalNumCp;
    if (!(std::cin >> globalNumCp)) {
        return 0;
    }

    std::vector<csb::Vec2> cps(globalNumCp);
    for (int i = 0; i < globalNumCp; ++i) {
        double x, y;
        std::cin >> x >> y;
        cps[i] = csb::Vec2(x, y);
    }

    int nTest;
    if (!(std::cin >> nTest)) {
        return 0;
    }

    int cp_count = (globalNumCp - 1) / 3;

    std::vector<csb::Vec2> base_cps(cp_count);
    for (int i = 0; i < cp_count; ++i) {
        base_cps[i] = cps[i];
    }

    std::array<csb::Pod, 4> pods;
    double dx = base_cps[1].x - base_cps[0].x;
    double dy = base_cps[1].y - base_cps[0].y;
    double dd = std::sqrt(dx * dx + dy * dy);
    double ux = dx / dd;
    double uy = dy / dd;

    static const csb::Vec2 start_mults[4] = {
        csb::Vec2(500.0, -500.0),
        csb::Vec2(-500.0, 500.0),
        csb::Vec2(1500.0, -1500.0),
        csb::Vec2(-1500.0, 1500.0)
    };

    for (int i = 0; i < 4; ++i) {
        pods[i].id = i;
        pods[i].team = i / 2;
        pods[i].pos.x = csb::Round(base_cps[0].x + uy * start_mults[i].x);
        pods[i].pos.y = csb::Round(base_cps[0].y + ux * start_mults[i].y);
        pods[i].angle = -1.0;
        pods[i].next_cp_id = 1;
        pods[i].boost_available = true;
        pods[i].shield_cd = 0;
        pods[i].timeout = 0;
        pods[i].laps_completed = 0;
    }

    for (int tn = 0; tn < nTest; ++tn) {
        for (int i = 0; i < 4; ++i) {
            std::string ignore;
            std::cin >> std::ws;
            std::getline(std::cin, ignore);
        }

        for (int i = 0; i < 4; ++i) {
            double px, py;
            std::string thrust_str;
            std::cin >> px >> py >> thrust_str;

            int t = 0;
            bool shield_act = false;
            bool boost_act = false;

            if (thrust_str == "SHIELD") {
                shield_act = true;
            } else if (thrust_str == "BOOST") {
                boost_act = true;
            } else {
                t = std::stoi(thrust_str);
            }

            pods[i].ApplyActionReferee(px, py, t, shield_act, boost_act);
        }

        csb::PhysicsEngine::SimulateTurnReferee(pods, base_cps, cp_count);

        for (int i = 0; i < 4; ++i) {
            int absolute_next_cp = pods[i].laps_completed * cp_count + pods[i].next_cp_id;
            if (absolute_next_cp >= globalNumCp) {
                absolute_next_cp = globalNumCp - 1;
            }

            int boosted_val = pods[i].boost_available ? 0 : 1;

            double print_angle = pods[i].angle;
            if (print_angle > 180.0) {
                print_angle -= 360.0;
            }
            if (print_angle < -180.0) {
                print_angle += 360.0;
            }

            std::cout << (int)pods[i].pos.x << " "
                      << (int)pods[i].pos.y << " "
                      << (int)pods[i].vel.x << " "
                      << (int)pods[i].vel.y << " "
                      << std::fixed << std::setprecision(6) << print_angle << " "
                      << absolute_next_cp << " "
                      << pods[i].shield_cd << " "
                      << boosted_val << "\n";
        }
    }

    return 0;
}
