// =============================================================================
// Golden capture / regeneration tool
// =============================================================================
// HISTORICAL: Committed goldens in src/physics/testdata/fast_goldens.json and
// fast_rollout_goldens.json were captured from unmodified GAPhysicsSimulator
// (engine.cpp) on a pre-port tree. Their JSON "source" fields still say
// "GAPhysicsSimulator" / "GAPhysicsSimulator+ApplyGAAction" — that provenance
// is authoritative and must not be silently overwritten.
//
// CURRENT BEHAVIOR: This binary calls FastSimulateTurn / csb::fast (identity
// vs the port). Regenerating replaces GA provenance with a circular self-test.
//
// To overwrite committed paths that declare source GAPhysicsSimulator, pass
// --force-identity explicitly. Without it, the tool refuses to overwrite those
// files and only writes to a path you specify that does not exist yet, OR prints
// a warning and writes only if the output path is not the committed golden.
// =============================================================================
#include "src/engine/engine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

static void WritePod(FILE* f, const Pod& p, const char* indent) {
    fprintf(f,
        "%s{\"id\": %d, \"team\": %d, \"pos\": [%.17g, %.17g], \"vel\": [%.17g, %.17g], "
        "\"angle\": %.17g, \"shield_cd\": %d, \"boost_available\": %s, "
        "\"next_cp_id\": %d, \"laps_completed\": %d, \"timeout\": %d}",
        indent, p.id, p.team, p.pos.x, p.pos.y, p.vel.x, p.vel.y, p.angle, p.shield_cd,
        p.boost_available ? "true" : "false", p.next_cp_id, p.laps_completed, p.timeout);
}

static double RandRange(double lo, double hi) {
    uint32_t r = FastRand();
    double u = (r >> 8) * (1.0 / 16777216.0);
    return lo + (hi - lo) * u;
}

static int RandInt(int lo, int hi) { return FastRandInt(lo, hi); }

enum Class { kRandom, kClustered, kShield4, kAngleNeg };

static void SynthPods(Pod pods[4], Class cls) {
    for (int i = 0; i < 4; ++i) {
        pods[i] = Pod();
        pods[i].id = i;
        pods[i].team = (i < 2) ? 0 : 1;
        pods[i].next_cp_id = RandInt(0, 3);
        pods[i].laps_completed = RandInt(0, 2);
        pods[i].boost_available = RandInt(0, 1) != 0;
        pods[i].timeout = RandInt(0, 100);
        pods[i].shield_cd = RandInt(0, 4);
        pods[i].angle = RandRange(0.0, 359.0);
        pods[i].pos.x = RandRange(2000.0, 14000.0);
        pods[i].pos.y = RandRange(1000.0, 8000.0);
        pods[i].vel.x = RandRange(-400.0, 400.0);
        pods[i].vel.y = RandRange(-400.0, 400.0);
    }
    if (cls == kClustered) {
        double cx = RandRange(4000.0, 12000.0);
        double cy = RandRange(2000.0, 7000.0);
        for (int i = 0; i < 4; ++i) {
            pods[i].pos.x = cx + RandRange(-300.0, 300.0);
            pods[i].pos.y = cy + RandRange(-300.0, 300.0);
            double ang = (i * 90.0) * 3.14159265358979323846 / 180.0;
            pods[i].vel.x = std::cos(ang) * RandRange(200.0, 500.0);
            pods[i].vel.y = std::sin(ang) * RandRange(200.0, 500.0);
        }
    }
    if (cls == kShield4) {
        pods[0].shield_cd = 4;
        pods[2].shield_cd = 4;
        pods[0].pos.x = 8000; pods[0].pos.y = 4500;
        pods[1].pos.x = 8500; pods[1].pos.y = 4500;
        pods[0].vel.x = 300; pods[0].vel.y = 0;
        pods[1].vel.x = -300; pods[1].vel.y = 0;
    }
    if (cls == kAngleNeg) {
        pods[0].angle = -1.0;
        pods[3].angle = -1.0;
    }
}

struct Spec { int id; int k; Class cls; };

static bool FileDeclaresGAPhysicsSource(const char* path) {
    std::ifstream in(path);
    if (!in.good()) return false;
    std::stringstream buf;
    buf << in.rdbuf();
    std::string s = buf.str();
    return s.find("\"source\": \"GAPhysicsSimulator") != std::string::npos
        || s.find("\"source\":\"GAPhysicsSimulator") != std::string::npos;
}

static bool AllowWrite(const char* path, bool force_identity) {
    if (!FileDeclaresGAPhysicsSource(path)) return true;
    if (force_identity) {
        fprintf(stderr,
            "WARNING: --force-identity overwriting GA-provenance golden at %s "
            "(identity self-test only; not historical GA)\n", path);
        return true;
    }
    fprintf(stderr,
        "REFUSE: %s declares source GAPhysicsSimulator (committed pre-port goldens).\n"
        "Regeneration would be identity-only via FastSimulateTurn/csb::fast.\n"
        "Pass --force-identity to overwrite anyway, or write to a different path.\n",
        path);
    return false;
}

int main(int argc, char** argv) {
    const char* out_path = "src/physics/testdata/fast_goldens.json";
    const char* rollout_path = "src/physics/testdata/fast_rollout_goldens.json";
    bool force_identity = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--force-identity") force_identity = true;
        else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (a == "--rollout-out" && i + 1 < argc) rollout_path = argv[++i];
        else if (a[0] != '-') {
            // positional: out_path [rollout_path]
            out_path = argv[i];
            if (i + 1 < argc && argv[i+1][0] != '-') rollout_path = argv[++i];
        }
    }

    if (!AllowWrite(out_path, force_identity)) return 2;
    if (!AllowWrite(rollout_path, force_identity)) return 2;

    std::vector<Spec> specs;
    int id = 0;
    for (int i = 0; i < 8; ++i) {
        Class c = kRandom;
        if (i < 2) c = kClustered;
        else if (i < 3) c = kShield4;
        else if (i < 4) c = kAngleNeg;
        specs.push_back({id++, 1, c});
    }
    for (int i = 0; i < 12; ++i) {
        Class c = kRandom;
        if (i < 2) c = kClustered;
        else if (i == 2) c = kShield4;
        else if (i == 3) c = kAngleNeg;
        specs.push_back({id++, 5, c});
    }
    for (int i = 0; i < 12; ++i) {
        Class c = kRandom;
        if (i == 0) c = kClustered;
        specs.push_back({id++, 20, c});
    }

    FILE* f = fopen(out_path, "w");
    if (!f) { perror(out_path); return 1; }
    // Identity regeneration labels source as such (do not claim GAPhysicsSimulator).
    fprintf(f, "{\n  \"version\": 1,\n  \"source\": \"FastSimulateTurn_identity\",\n"
               "  \"source_ref\": \"csb::fast (NOT historical GA — use --force-identity knowingly)\",\n"
               "  \"scenarios\": [\n");

    for (size_t si = 0; si < specs.size(); ++si) {
        const Spec& sp = specs[si];
        uint32_t seed = 0xC5BFA57u + (uint32_t)sp.id;
        SeedRand(seed);
        Pod pods[4];
        SynthPods(pods, sp.cls);
        Pod pods_in[4];
        for (int i = 0; i < 4; ++i) pods_in[i] = pods[i];

        bool friendly_after = false;
        for (int t = 0; t < sp.k; ++t) {
            g_friendly_collision = false;
            FastSimulateTurn(pods);
            friendly_after = g_friendly_collision;
        }

        fprintf(f, "    {\n      \"id\": %d,\n      \"seed\": %u,\n      \"k\": %d,\n"
                   "      \"friendly_after\": %s,\n      \"pods_in\": [\n",
                sp.id, seed, sp.k, friendly_after ? "true" : "false");
        for (int i = 0; i < 4; ++i) {
            WritePod(f, pods_in[i], "        ");
            fprintf(f, "%s\n", i < 3 ? "," : "");
        }
        fprintf(f, "      ],\n      \"pods_out\": [\n");
        for (int i = 0; i < 4; ++i) {
            WritePod(f, pods[i], "        ");
            fprintf(f, "%s\n", i < 3 ? "," : "");
        }
        fprintf(f, "      ]\n    }%s\n", si + 1 < specs.size() ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    fprintf(stderr, "Wrote %zu scenarios to %s\n", specs.size(), out_path);

    FILE* rf = fopen(rollout_path, "w");
    if (!rf) { perror(rollout_path); return 1; }
    fprintf(rf, "{\n  \"version\": 1,\n  \"source\": \"FastSimulateTurn_identity+ApplyGAAction\",\n"
                "  \"source_ref\": \"csb::fast (NOT historical GA)\",\n  \"scenarios\": [\n");
    const int n_roll = 16;
    for (int rid = 0; rid < n_roll; ++rid) {
        int k = (rid < 8) ? 1 : 5;
        uint32_t seed = 0xC5BFA57u + 1000u + (uint32_t)rid;
        SeedRand(seed);
        Pod pods[4];
        SynthPods(pods, (rid % 4 == 0) ? kClustered : kRandom);
        Pod pods_in[4];
        for (int i = 0; i < 4; ++i) pods_in[i] = pods[i];

        fprintf(rf, "    {\n      \"id\": %d,\n      \"seed\": %u,\n      \"k\": %d,\n",
                rid, seed, k);
        fprintf(rf, "      \"turns\": [\n");
        for (int t = 0; t < k; ++t) {
            fprintf(rf, "        {\"actions\": [");
            for (int i = 0; i < 4; ++i) {
                double ang = RandRange(-18.0, 18.0);
                int thr = RandInt(0, 200);
                if (rid % 5 == 0 && i == 0 && t == 0) thr = -1;
                fprintf(rf, "[%.17g, %d]%s", ang, thr, i < 3 ? ", " : "");
                pods[i].ApplyGAAction(ang, thr);
            }
            fprintf(rf, "]}%s\n", t + 1 < k ? "," : "");
            g_friendly_collision = false;
            FastSimulateTurn(pods);
        }
        bool friendly_after = g_friendly_collision;
        fprintf(rf, "      ],\n      \"friendly_after\": %s,\n      \"pods_in\": [\n",
                friendly_after ? "true" : "false");
        for (int i = 0; i < 4; ++i) {
            WritePod(rf, pods_in[i], "        ");
            fprintf(rf, "%s\n", i < 3 ? "," : "");
        }
        fprintf(rf, "      ],\n      \"pods_out\": [\n");
        for (int i = 0; i < 4; ++i) {
            WritePod(rf, pods[i], "        ");
            fprintf(rf, "%s\n", i < 3 ? "," : "");
        }
        fprintf(rf, "      ]\n    }%s\n", rid + 1 < n_roll ? "," : "");
    }
    fprintf(rf, "  ]\n}\n");
    fclose(rf);
    fprintf(stderr, "Wrote %d rollout scenarios to %s\n", n_roll, rollout_path);
    return 0;
}
