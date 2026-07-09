// Stream format (repeat until EOF):
//   n_cp laps
//   n_cp lines: cp_x cp_y
//   4 lines: x y vx vy ang next shield boosted
//   t0 t1
//   N
//   N*4 lines: tx ty mode thr   (mode 0=power 1=shield 2=boost 3=invalid)
// Env CSB_FP_QUIET=1 suppresses per-battle OK lines.
// Exit 1 if any miss.
#define CSB_PHYSICS_NO_GLOBAL_USING
#include "physics.h"
#include "fast_physics.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

// Returns turns on success, -2 on miss, -1 on EOF
static int run_one(bool quiet) {
    int n_cp = 0, laps = 0;
    if (std::scanf("%d %d", &n_cp, &laps) != 2) return -1;
    if (n_cp < 1 || n_cp > 8 || laps < 1 || laps > 5) {
        std::printf("MISS bad header n_cp=%d laps=%d\n", n_cp, laps);
        return -2;
    }
    std::vector<csb::Point> track((size_t)n_cp);
    double xy[16];
    for (int i = 0; i < n_cp; ++i) {
        if (std::scanf("%lf %lf", &track[(size_t)i].x, &track[(size_t)i].y) != 2) return -2;
        xy[2 * i] = track[(size_t)i].x;
        xy[2 * i + 1] = track[(size_t)i].y;
    }
    csb::Game fid;
    fid.setTrack(track, laps);
    csb::fast_physics::Game fp;
    fp.clear();
    fp.setTrack(xy, n_cp, laps);
    for (int i = 0; i < 4; ++i) {
        double x, y, vx, vy, ang;
        int next, sh, bo;
        if (std::scanf("%lf %lf %lf %lf %lf %d %d %d", &x, &y, &vx, &vy, &ang, &next, &sh, &bo) != 8)
            return -2;
        fid.setPodState(i, x, y, vx, vy, ang, next, sh, bo);
        fp.setPod(i, x, y, vx, vy, ang, next, sh, bo);
        fp.pods[i].hasRotated = fid.pods[i].hasRotated;
        fp.pods[i].won = fid.pods[i].won;
    }
    int t0, t1;
    if (std::scanf("%d %d", &t0, &t1) != 2) return -2;
    fid.setPlayerTimeouts(t0, t1);
    fp.setTimeouts(t0, t1);
    fid.turn = 1;
    fp.turn = 1;
    int N = 0;
    if (std::scanf("%d", &N) != 1) return -2;
    for (int t = 0; t < N; ++t) {
        csb::PlayerMove fm[4];
        csb::fast_physics::Move pm[4];
        for (int i = 0; i < 4; ++i) {
            int mode = 0, thr = 0;
            double tx = 0, ty = 0;
            if (std::scanf("%lf %lf %d %d", &tx, &ty, &mode, &thr) != 4) {
                std::printf("MISS scan turn %d pod %d\n", t, i);
                return -2;
            }
            fm[i].target = {tx, ty};
            fm[i].thrust = thr;
            fm[i].shield = (mode == 1);
            fm[i].boost = (mode == 2);
            fm[i].invalid_input = (mode == 3);
            fm[i].valid = true;
            pm[i].tx = tx;
            pm[i].ty = ty;
            pm[i].thrust = thr;
            pm[i].shield = (mode == 1);
            pm[i].boost = (mode == 2);
            pm[i].invalid_input = (mode == 3);
        }
        fid.step(fm);
        fp.step(pm);
        for (int i = 0; i < 4; ++i) {
            const auto& a = fid.pods[i];
            const auto& b = fp.pods[i];
            if (a.p.x != b.px || a.p.y != b.py || a.s.x != b.vx || a.s.y != b.vy || a.next != b.next ||
                a.shieldtimer != b.shieldtimer || a.boosted != b.boosted || a.won != b.won) {
                std::printf("MISS turn %d pod %d pos/vel/next/flags\n", t, i);
                return -2;
            }
            double da = a.angle - b.angle;
            if (da < 0) da = -da;
            if (da > 3.141592653589793) da = 6.283185307179586 - da;
            if (da > 1e-12) {
                std::printf("MISS turn %d pod %d angle\n", t, i);
                return -2;
            }
        }
        if (fid.playerTimeout[0] != fp.playerTimeout[0] || fid.playerTimeout[1] != fp.playerTimeout[1]) {
            std::printf("MISS turn %d timeouts\n", t);
            return -2;
        }
    }
    if (!quiet) std::printf("OK %d turns EXACT\n", N);
    return N;
}

int main() {
    const bool quiet = (std::getenv("CSB_FP_QUIET") != nullptr);
    int games = 0;
    long long turns_total = 0;
    for (;;) {
        int r = run_one(quiet);
        if (r == -1) break;
        if (r == -2) {
            std::printf("SUMMARY games=%d fails>0 (aborted on miss) turns_ok=%lld\n", games, turns_total);
            return 1;
        }
        ++games;
        turns_total += r;
    }
    std::printf("SUMMARY games=%d fails=0 turns=%lld EXACT\n", games, turns_total);
    return games == 0 ? 2 : 0;
}
