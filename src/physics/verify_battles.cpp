// Verify csb physics against Codingame leaderboard battle JSON (frames[] schema).
//
// ROLE=DIAGNOSTIC (stricter numeric compare). NOT the PR merge corpus gate.
// Merge gate (MERGE_PHYSICS_OK): see docs/VERIFICATION_TRUTH_POLICY.md — Python
//   sim/verify_battles.py --gate + golden --tier pass + test_physics.
//
// Usage:
//   verify_battles [--dir PATH] [--file PATH] [--mode cumulative|per_turn|both]
//                  [--verbose] [--stop-on-fail] [--limit N] [--no-ncp]
//
// Default dir: battles/leaderboard_battles (relative to CWD or absolute).

#include "physics.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <map>

// ---- minimal JSON / frame extraction (no external deps) ---------------------

static std::string readFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// Unescape a JSON string value (handles \n \" \\)
static std::string jsonUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') { out.push_back('\n'); ++i; continue; }
            if (n == 't') { out.push_back('\t'); ++i; continue; }
            if (n == 'r') { out.push_back('\r'); ++i; continue; }
            if (n == '"' || n == '\\' || n == '/') { out.push_back(n); ++i; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

// Extract "key": "....string...." allowing escapes; returns unescaped body.
static bool extractJsonStringField(const std::string& block, const std::string& key, std::string& out) {
    std::string pat = "\"" + key + "\"";
    size_t k = block.find(pat);
    if (k == std::string::npos) return false;
    size_t colon = block.find(':', k + pat.size());
    if (colon == std::string::npos) return false;
    size_t q1 = block.find('"', colon + 1);
    if (q1 == std::string::npos) return false;
    size_t i = q1 + 1;
    std::string raw;
    while (i < block.size()) {
        if (block[i] == '\\' && i + 1 < block.size()) {
            raw.push_back(block[i]);
            raw.push_back(block[i + 1]);
            i += 2;
            continue;
        }
        if (block[i] == '"') break;
        raw.push_back(block[i]);
        ++i;
    }
    out = jsonUnescape(raw);
    return true;
}

static bool extractJsonIntField(const std::string& block, const std::string& key, int& out) {
    std::string pat = "\"" + key + "\"";
    size_t k = block.find(pat);
    if (k == std::string::npos) return false;
    size_t colon = block.find(':', k + pat.size());
    if (colon == std::string::npos) return false;
    size_t p = colon + 1;
    while (p < block.size() && (block[p] == ' ' || block[p] == '\t')) ++p;
    try {
        out = std::stoi(block.substr(p));
        return true;
    } catch (...) {
        return false;
    }
}

static bool extractJsonBoolField(const std::string& block, const std::string& key, bool& out) {
    std::string pat = "\"" + key + "\"";
    size_t k = block.find(pat);
    if (k == std::string::npos) return false;
    size_t colon = block.find(':', k + pat.size());
    if (colon == std::string::npos) return false;
    size_t t = block.find("true", colon);
    size_t f = block.find("false", colon);
    if (t != std::string::npos && (f == std::string::npos || t < f)) {
        if (t - colon < 10) { out = true; return true; }
    }
    if (f != std::string::npos && f - colon < 10) { out = false; return true; }
    return false;
}

struct FrameRaw {
    int agentId = -1;
    bool keyframe = false;
    std::string stdout_text;
    std::string view;
};

// Split top-level frames array objects (naive brace scan).
static std::vector<std::string> splitFrameObjects(const std::string& content) {
    std::vector<std::string> frames;
    size_t arr = content.find("\"frames\"");
    if (arr == std::string::npos) return frames;
    size_t lb = content.find('[', arr);
    if (lb == std::string::npos) return frames;

    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = lb + 1; i < content.size(); ++i) {
        char c = content[i];
        if (c == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0 && start != std::string::npos) {
                frames.push_back(content.substr(start, i - start + 1));
                start = std::string::npos;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }
    return frames;
}

static FrameRaw parseFrameObject(const std::string& obj) {
    FrameRaw fr;
    extractJsonIntField(obj, "agentId", fr.agentId);
    extractJsonBoolField(obj, "keyframe", fr.keyframe);
    extractJsonStringField(obj, "stdout", fr.stdout_text);
    extractJsonStringField(obj, "view", fr.view);
    return fr;
}

static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::stringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    return lines;
}

static std::vector<std::string> nonEmptyLines(const std::string& s) {
    auto lines = splitLines(s);
    std::vector<std::string> out;
    for (auto& l : lines) {
        // trim
        size_t a = l.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = l.find_last_not_of(" \t\r\n");
        out.push_back(l.substr(a, b - a + 1));
    }
    return out;
}

static bool parsePodLine(const std::string& line, csb::PodSnapshot& snap) {
    std::stringstream ss(line);
    std::string ang_s;
    double thrust_unused, tx_unused, ty_unused;
    int sh_flag = 0, ncp = 1, player_unused = 0;
    if (!(ss >> snap.x >> snap.y >> snap.vx >> snap.vy)) return false;
    // thrust, shield?, tx, ty, angle, shield_flag, next_cp, player_id
    if (!(ss >> thrust_unused >> sh_flag >> tx_unused >> ty_unused >> ang_s >> sh_flag >> ncp)) {
        // frame0 style may have fewer trailing fields already in ang_s path
        return false;
    }
    if (ang_s == "null") snap.angle = -1.0;
    else {
        try { snap.angle = std::stod(ang_s); } catch (...) { snap.angle = -1.0; }
    }
    snap.shield_flag = sh_flag;
    snap.next_cp = ncp;
    (void)player_unused;
    return true;
}

// Alternate: pod lines always have >= 11 tokens; parse positionally.
static bool parsePodLineTokens(const std::string& line, csb::PodSnapshot& snap) {
    std::vector<std::string> tok;
    std::stringstream ss(line);
    std::string t;
    while (ss >> t) tok.push_back(t);
    if (tok.size() < 11) return false;
    try {
        snap.x = std::stod(tok[0]);
        snap.y = std::stod(tok[1]);
        snap.vx = std::stod(tok[2]);
        snap.vy = std::stod(tok[3]);
        if (tok[8] == "null") snap.angle = -1.0;
        else snap.angle = std::stod(tok[8]);
        snap.shield_flag = std::stoi(tok[9]);
        snap.next_cp = std::stoi(tok[10]);
    } catch (...) {
        return false;
    }
    return true;
}

static bool extractPodsFromView(const std::vector<std::string>& view_lines, bool frame0,
                                std::array<csb::PodSnapshot, 4>& out) {
    const int idxs_f0[4] = {5, 7, 9, 11};
    const int idxs_kf[4] = {1, 3, 5, 7};
    const int* idxs = frame0 ? idxs_f0 : idxs_kf;
    for (int i = 0; i < 4; ++i) {
        int li = idxs[i];
        if (li >= static_cast<int>(view_lines.size())) return false;
        if (!parsePodLineTokens(view_lines[li], out[i])) return false;
    }
    return true;
}

static bool extractTrack(const std::vector<std::string>& view_lines, std::vector<csb::Point>& track) {
    if (view_lines.size() < 4) return false;
    std::stringstream ss(view_lines[3]);
    double x, y;
    track.clear();
    while (ss >> x >> y) {
        track.push_back({x, y});
    }
    return track.size() >= 2;
}

struct GameTurn {
    csb::PlayerMove moves[4];
    bool has_expected = false;
    std::array<csb::PodSnapshot, 4> expected{};
};

struct BattleLoaded {
    std::vector<csb::Point> track;
    std::array<csb::PodSnapshot, 4> init{};
    std::vector<GameTurn> turns;
    bool ok = false;
};

static BattleLoaded loadBattle(const std::string& path) {
    BattleLoaded b;
    std::string content = readFile(path);
    if (content.empty()) return b;

    auto frame_objs = splitFrameObjects(content);
    if (frame_objs.empty()) return b;

    std::vector<FrameRaw> frames;
    frames.reserve(frame_objs.size());
    for (auto& o : frame_objs) frames.push_back(parseFrameObject(o));

    auto v0 = splitLines(frames[0].view);
    if (!extractTrack(v0, b.track)) return b;
    if (!extractPodsFromView(v0, true, b.init)) return b;

    for (size_t i = 1; i + 1 < frames.size(); i += 2) {
        GameTurn gt;
        auto la = nonEmptyLines(frames[i].stdout_text);
        auto lb = nonEmptyLines(frames[i + 1].stdout_text);
        if (la.size() < 2 || lb.size() < 2) continue;
        gt.moves[0] = csb::parseMove(la[0]);
        gt.moves[1] = csb::parseMove(la[1]);
        gt.moves[2] = csb::parseMove(lb[0]);
        gt.moves[3] = csb::parseMove(lb[1]);

        if (frames[i + 1].keyframe && !frames[i + 1].view.empty()) {
            auto vl = splitLines(frames[i + 1].view);
            if (extractPodsFromView(vl, false, gt.expected)) {
                gt.has_expected = true;
            }
        }
        b.turns.push_back(gt);
    }
    b.ok = !b.turns.empty();
    return b;
}

static void seedGame(csb::Game& g, const BattleLoaded& b) {
    g.setTrack(b.track, csb::kDefaultLaps);
    for (int i = 0; i < 4; ++i) {
        const auto& s = b.init[i];
        g.setPodState(i, s.x, s.y, s.vx, s.vy, s.angle, s.next_cp, 0, 0);
    }
    g.playerTimeout[0] = csb::kTimeoutLimit;
    g.playerTimeout[1] = csb::kTimeoutLimit;
    g.turn = 0;
}

struct VerifyOutcome {
    std::string path;
    std::string mode;
    bool pass = false;
    bool skip = false;
    int first_fail_turn = -1;
    std::string errors;
    int turns_checked = 0;
    int total_turns = 0;
};

static VerifyOutcome verifyCumulative(const std::string& path, const BattleLoaded& b, bool check_ncp) {
    VerifyOutcome o;
    o.path = path;
    o.mode = "cumulative";
    o.total_turns = static_cast<int>(b.turns.size());
    if (!b.ok) { o.skip = true; return o; }

    csb::Game g;
    seedGame(g, b);

    for (size_t ti = 0; ti < b.turns.size(); ++ti) {
        g.step(b.turns[ti].moves);
        if (!b.turns[ti].has_expected) continue;
        o.turns_checked++;
        for (int pi = 0; pi < 4; ++pi) {
            int tsz = static_cast<int>(b.track.size());
            auto cr = csb::comparePod(g.pods[pi], b.turns[ti].expected[pi],
                                      0.01, 0.01, 0.001, check_ncp, tsz);
            if (!cr.ok) {
                o.pass = false;
                o.first_fail_turn = static_cast<int>(ti) + 1;
                std::ostringstream ss;
                ss << "pod" << pi << " " << cr.detail;
                o.errors = ss.str();
                return o;
            }
        }
    }
    o.pass = true;
    return o;
}

static VerifyOutcome verifyPerTurn(const std::string& path, const BattleLoaded& b, bool check_ncp) {
    VerifyOutcome o;
    o.path = path;
    o.mode = "per_turn";
    o.total_turns = static_cast<int>(b.turns.size());
    if (!b.ok) { o.skip = true; return o; }

    csb::Game g;
    seedGame(g, b);
    // Preserve shield/boost across resyncs from simulation side.
    int shield_carry[4] = {0, 0, 0, 0};
    int boost_carry[4] = {0, 0, 0, 0};

    for (size_t ti = 0; ti < b.turns.size(); ++ti) {
        // Resync kinematics from previous ground truth (or init on first turn)
        if (ti == 0) {
            seedGame(g, b);
        } else if (b.turns[ti - 1].has_expected) {
            for (int i = 0; i < 4; ++i) {
                const auto& s = b.turns[ti - 1].expected[i];
                g.setPodState(i, s.x, s.y, s.vx, s.vy, s.angle, s.next_cp,
                              shield_carry[i], boost_carry[i]);
            }
            g.turn = static_cast<int>(ti);  // first-turn rotate only on ti==0
        }

        g.step(b.turns[ti].moves);
        for (int i = 0; i < 4; ++i) {
            shield_carry[i] = g.pods[i].shieldtimer;
            boost_carry[i] = g.pods[i].boosted;
        }

        if (!b.turns[ti].has_expected) continue;
        o.turns_checked++;
        int tsz = static_cast<int>(b.track.size());
        for (int pi = 0; pi < 4; ++pi) {
            auto cr = csb::comparePod(g.pods[pi], b.turns[ti].expected[pi],
                                      0.01, 0.01, 0.001, check_ncp, tsz);
            if (!cr.ok) {
                o.pass = false;
                o.first_fail_turn = static_cast<int>(ti) + 1;
                std::ostringstream ss;
                ss << "pod" << pi << " " << cr.detail;
                o.errors = ss.str();
                return o;
            }
        }
    }
    o.pass = true;
    return o;
}

static void printUsage() {
    std::cout
        << "Usage: verify_battles [options]\n"
        << "  --dir <path>       Directory of battle_*.json (default: battles/leaderboard_battles)\n"
        << "  --file <path>      Verify a single battle file\n"
        << "  --mode <m>         cumulative | per_turn | both (default: both)\n"
        << "  --limit <n>        Max battles to process\n"
        << "  --verbose, -v      Print each failure line\n"
        << "  --stop-on-fail     Stop at first failure\n"
        << "  --no-ncp           Do not compare next_cp (pos/vel/ang only)\n"
        << "  --help, -h\n";
}

int main(int argc, char** argv) {
    std::string dir = "battles/leaderboard_battles";
    std::string single;
    std::string mode = "both";
    int limit = 0;
    bool verbose = false;
    bool stop_on_fail = false;
    bool check_ncp = true;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == "--file" && i + 1 < argc) single = argv[++i];
        else if (a == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (a == "--limit" && i + 1 < argc) limit = std::stoi(argv[++i]);
        else if (a == "--verbose" || a == "-v") verbose = true;
        else if (a == "--stop-on-fail") stop_on_fail = true;
        else if (a == "--no-ncp") check_ncp = false;
        else if (a == "--help" || a == "-h") { printUsage(); return 0; }
    }

    std::vector<std::string> files;
    if (!single.empty()) {
        files.push_back(single);
    } else {
        DIR* d = opendir(dir.c_str());
        if (!d) {
            std::cerr << "Directory not found: " << dir << "\n";
            return 1;
        }
        while (dirent* ent = readdir(d)) {
            std::string name = ent->d_name;
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
                files.push_back(dir + "/" + name);
            }
        }
        closedir(d);
        std::sort(files.begin(), files.end());
    }
    if (limit > 0 && static_cast<int>(files.size()) > limit) {
        files.resize(limit);
    }

    std::vector<std::string> modes;
    if (mode == "both") { modes = {"cumulative", "per_turn"}; }
    else modes = {mode};

    int exit_code = 0;
    for (const auto& m : modes) {
        int total = 0, pass_c = 0, fail_c = 0, skip_c = 0;
        auto t0 = std::chrono::high_resolution_clock::now();

        std::cout << "\n=== CSB Physics Verification [" << m << "] ===\n";
        std::cout << "Battles: " << files.size() << "  check_ncp=" << (check_ncp ? "yes" : "no") << "\n";

        for (size_t fi = 0; fi < files.size(); ++fi) {
            auto battle = loadBattle(files[fi]);
            VerifyOutcome r;
            if (m == "cumulative") r = verifyCumulative(files[fi], battle, check_ncp);
            else r = verifyPerTurn(files[fi], battle, check_ncp);

            total++;
            if (r.skip) skip_c++;
            else if (r.pass) pass_c++;
            else {
                fail_c++;
                exit_code = 1;
                if (verbose || fail_c <= 40) {
                    std::cout << "FAIL " << files[fi].substr(files[fi].find_last_of('/') == std::string::npos ? 0 : files[fi].find_last_of('/') + 1)
                              << " t=" << r.first_fail_turn
                              << " " << r.errors << "\n";
                }
                if (stop_on_fail) break;
            }
            if ((fi + 1) % 1000 == 0) {
                std::cout << "... " << (fi + 1) << "/" << files.size()
                          << " pass=" << pass_c << " fail=" << fail_c << "\n" << std::flush;
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        std::cout << "Total=" << total << " PASS=" << pass_c << " FAIL=" << fail_c
                  << " SKIP=" << skip_c;
        if (total > 0) {
            std::cout << "  pass_rate=" << std::fixed << std::setprecision(2)
                      << (100.0 * pass_c / total) << "%";
        }
        std::cout << "  time=" << std::setprecision(1) << sec << "s\n";
    }

    return exit_code;
}
