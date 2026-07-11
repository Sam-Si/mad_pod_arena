#pragma once
// =============================================================================
// Single Fidelity world-step implementation (Fowler: Form Template Method /
// Extract Class for Duplicated Code between physics.h and fast_physics.h).
// Both csb::Game and csb::fast_physics::Game MUST call simulateFidelityWorld
// after moves are applied — no second collision/CP/commit loop elsewhere.
// Behavior matches historical Game::simulateWorld (gate / CG keyframe oracle).
// =============================================================================

#include "fidelity_math.h"

namespace csb {

// Minimal pod state required for the world step (post-applyMove).
struct WorldPod {
    double px = 0, py = 0;
    double vx = 0, vy = 0;
    int next = 1;
    int shieldtimer = 0;
    bool won = false;
};

inline void worldForwardTime(WorldPod* pods, double t) {
    for (int i = 0; i < kPodCount; ++i) {
        pods[i].px += pods[i].vx * t;
        pods[i].py += pods[i].vy * t;
    }
}

inline void worldBounce(WorldPod* pods, int p1, int p2) {
    WorldPod& oa = pods[p1];
    WorldPod& ob = pods[p2];
    double nx = ob.px - oa.px;
    double ny = ob.py - oa.py;
    const double dd = std::sqrt(nx * nx + ny * ny);
    if (dd == 0.0) return;
    nx /= dd;
    ny /= dd;
    const double rvx = oa.vx - ob.vx;
    const double rvy = oa.vy - ob.vy;
    double m1 = 1.0, m2 = 1.0;
    if (oa.shieldtimer == 4) m1 = 0.1;
    if (ob.shieldtimer == 4) m2 = 0.1;
    double force = (nx * rvx + ny * rvy) / (m1 + m2);
    if (force < kMinImpulse) force += kMinImpulse;
    else force += force;
    const double ix = nx * -force;
    const double iy = ny * -force;
    oa.vx += ix * m1;
    oa.vy += iy * m1;
    ob.vx += -ix * m2;
    ob.vy += -iy * m2;
    // Overlap separation: only when contact is *materially* inside radius.
    // After double TOI, dd can land 1 ULP under 800 (e.g. 799.99999999999977).
    // Historical `dd <= 800` then applies kEpsilon (~1e-5) along the normal and
    // can flip roundHalfUp across a half-integer (battle 895131867 turn 42:
    // pod0.y 6325 vs CG 6326). CG lands the non-separation side of that knife-edge.
    // Threshold 1e-9 ≫ double ULP@800 (~2e-13) and ≪ kEpsilon / half-integer
    // resolution, so real multi-hit interpenetration still separates.
    if (dd < 800.0 - 1e-9) {
        const double ddiff = dd - 800.0;
        oa.px += nx * -(-ddiff / 2.0 + kEpsilon);
        oa.py += ny * -(-ddiff / 2.0 + kEpsilon);
        ob.px += nx * (-ddiff / 2.0 + kEpsilon);
        ob.py += ny * (-ddiff / 2.0 + kEpsilon);
    }
}

inline void worldEndTurnPod(WorldPod& p) {
    p.vx = frictionTrunc(p.vx);
    p.vy = frictionTrunc(p.vy);
    p.px = roundHalfUp(p.px);
    p.py = roundHalfUp(p.py);
    if (p.shieldtimer > 0) --p.shieldtimer;
}

inline void worldPassCheckpoint(WorldPod* pods, int podn, int global_n, int* playerTimeout) {
    WorldPod& p = pods[podn];
    p.next = p.next + 1;
    if (p.next >= global_n) {
        p.next = global_n - 1;
        p.won = true;
    }
    if (podn < 2) playerTimeout[0] = kTimeoutLimit + 1;
    else playerTimeout[1] = kTimeoutLimit + 1;
}

inline void worldTryPassCpFrom(WorldPod* pods, int i, double from_x, double from_y,
                               const double* gcx, const double* gcy, int global_n,
                               int* playerTimeout) {
    const int ni = pods[i].next;
    if (ni >= 0 && ni < global_n &&
        cpCollide(from_x, from_y, pods[i].px, pods[i].py, gcx[ni], gcy[ni])) {
        worldPassCheckpoint(pods, i, global_n, playerTimeout);
    }
}

// Authoritative world step after all 4 pod moves for the turn are applied.
inline void simulateFidelityWorld(WorldPod pods[kPodCount],
                                  const double* gcx, const double* gcy, int global_n,
                                  int playerTimeout[2], int* turn) {
    const int globalNumCp = global_n;
    double t = 1.0;
    double prev_x[kPodCount], prev_y[kPodCount];
    double start_x[kPodCount], start_y[kPodCount];
    bool bounced[kPodCount] = {false, false, false, false};
    for (int i = 0; i < kPodCount; ++i) {
        prev_x[i] = start_x[i] = pods[i].px;
        prev_y[i] = start_y[i] = pods[i].py;
    }

    int safety = 0;
    while (t > 0.0 && safety++ < 200) {
        double first = t;
        int cli = 0, clj = 0;
        // Go scan order i = 3..1, j = i-1..0; earliest tx wins ties.
        for (int i = kPodCount - 1; i > 0; --i) {
            for (int j = i - 1; j >= 0; --j) {
                const double tx = newCollideTime(
                    pods[i].px, pods[i].py, pods[i].vx, pods[i].vy,
                    pods[j].px, pods[j].py, pods[j].vx, pods[j].vy,
                    kPodCollisionRsq);
                if (tx <= first) {
                    first = tx;
                    cli = i;
                    clj = j;
                }
            }
        }
        worldForwardTime(pods, first);
        t -= first;
        if (cli != clj) {
            worldBounce(pods, cli, clj);
            bounced[cli] = bounced[clj] = true;
            if (t > 0.0) {
                worldTryPassCpFrom(pods, cli, prev_x[cli], prev_y[cli], gcx, gcy, globalNumCp,
                                   playerTimeout);
                worldTryPassCpFrom(pods, clj, prev_x[clj], prev_y[clj], gcx, gcy, globalNumCp,
                                   playerTimeout);
                prev_x[cli] = pods[cli].px;
                prev_y[cli] = pods[cli].py;
                prev_x[clj] = pods[clj].px;
                prev_y[clj] = pods[clj].py;
            }
        }
    }

    for (int i = 0; i < kPodCount; ++i) {
        worldEndTurnPod(pods[i]);
        const double from_x = bounced[i] ? prev_x[i] : start_x[i];
        const double from_y = bounced[i] ? prev_y[i] : start_y[i];
        worldTryPassCpFrom(pods, i, from_x, from_y, gcx, gcy, globalNumCp, playerTimeout);
    }

    for (int i = 0; i < kPodCount; ++i) {
        if (pods[i].won) continue;
        const int ni = pods[i].next;
        if (ni < 0 || ni >= globalNumCp) continue;
        const double dx = pods[i].px - gcx[ni];
        const double dy = pods[i].py - gcy[ni];
        if (dx * dx + dy * dy == kCpRsq) {
            worldPassCheckpoint(pods, i, globalNumCp, playerTimeout);
        }
    }

    playerTimeout[0]--;
    playerTimeout[1]--;
    (*turn)++;
}

}  // namespace csb
