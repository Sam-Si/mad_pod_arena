#include "src/engine/arena.h"
#include "maps/catalog.h"
#include "progress.h"
#define CSB_PHYSICS_NO_GLOBAL_USING 1
#include "src/physics/physics.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

Arena::Arena(std::shared_ptr<IBot> bot0, std::shared_ptr<IBot> bot1)
    : bot0_(bot0), bot1_(bot1) {}

int Arena::GetMapCount() { return GetTournamentMapCount(); }

void Arena::GenerateMap(int map_idx) {
    laps_ = 3;

    if (map_idx < 0 || map_idx >= GetTournamentMapCount()) {
        map_idx = FastRandInt(0, GetTournamentMapCount() - 1);
    }
    const auto& raw = GetTournamentMapsRaw()[static_cast<size_t>(map_idx)];
    cps_.clear();
    cps_.reserve(raw.size());
    for (const auto& pt : raw) {
        cps_.emplace_back(pt.x, pt.y);
    }
    cp_count_ = static_cast<int>(cps_.size());

    // Degrees view pods for IBot (full world). Core state lives in game_ during PlayGame.
    double dx = cps_[1].x - cps_[0].x;
    double dy = cps_[1].y - cps_[0].y;
    double dd = std::sqrt(dx * dx + dy * dy);
    double ux = dx / dd;
    double uy = dy / dd;

    static const Vec2 start_mults[4] = {
        Vec2(500.0, -500.0),
        Vec2(-500.0, 500.0),
        Vec2(1500.0, -1500.0),
        Vec2(-1500.0, 1500.0)
    };

    pods_.resize(4);
    for (int i = 0; i < 4; ++i) {
        pods_[i] = Pod();
        pods_[i].id = i;
        pods_[i].team = i / 2;
        pods_[i].pos.x = Round(cps_[0].x + uy * start_mults[i].x);
        pods_[i].pos.y = Round(cps_[0].y + ux * start_mults[i].y);
        pods_[i].angle = -1.0;
        pods_[i].next_cp_id = 1;
        pods_[i].boost_available = true;
        pods_[i].shield_cd = 0;
        pods_[i].timeout = 0;
        pods_[i].laps_completed = 0;
    }
}

static std::string ThrustToken(const PodAction& a) {
    if (a.thrust == -1) return "SHIELD";
    if (a.thrust == 650) return "BOOST";
    std::ostringstream oss;
    oss << a.thrust;
    return oss.str();
}

// Build degrees IBot view from Fidelity game (full 4 pods). Local CP for next_cp_id.
static void SyncViewFromGame(const csb::Game& g, int track_size, std::vector<Pod>& view) {
    view.resize(4);
    for (int i = 0; i < 4; ++i) {
        const csb::Pod& c = g.pods[static_cast<size_t>(i)];
        Pod& v = view[static_cast<size_t>(i)];
        v.id = i;
        v.team = i / 2;
        v.pos.x = c.p.x;
        v.pos.y = c.p.y;
        v.vel.x = c.s.x;
        v.vel.y = c.s.y;
        // Uninitialized: core uses ~-1° in radians; expose -1 degrees for IBot contract.
        if (!c.hasRotated && c.angle > -0.02 && c.angle < 0.0) {
            v.angle = -1.0;
        } else if (!c.hasRotated && std::fabs(c.angle + csb::kDegToRad) < 1e-9) {
            v.angle = -1.0;
        } else {
            double deg = c.angle * csb::kRadToDeg;
            while (deg >= 360.0) deg -= 360.0;
            while (deg < 0.0) deg += 360.0;
            v.angle = deg;
        }
        v.next_cp_id = csb_progress::LocalNext(c.next, track_size);
        v.boost_available = (c.boosted == 0);
        v.shield_cd = c.shieldtimer;
        // Approximate per-pod timeout display from team timeout (view only).
        v.timeout = (i < 2) ? (csb::kTimeoutLimit + 1 - g.playerTimeout[0]) : (csb::kTimeoutLimit + 1 - g.playerTimeout[1]);
        if (v.timeout < 0) v.timeout = 0;
        int lap = 0, local = 0;
        csb_progress::Decode(c.next, track_size, &lap, &local);
        v.laps_completed = lap;
        if (c.won) {
            v.laps_completed = 3;  // signal finished for any legacy readers
        }
    }
}

ArenaResult Arena::PlayGame(bool verbose, int map_idx) {
    GenerateMap(map_idx);

    // Fidelity world (SSOT) — OQ2: outcomes must match referee Game / replay_driver.
    csb::Game game;
    std::vector<csb::Point> track;
    track.reserve(cps_.size());
    for (const auto& cp : cps_) {
        track.push_back({cp.x, cp.y});
    }
    game.initialize(track, laps_);

    bot0_->Initialize(laps_, cp_count_, cps_, 0);
    bot1_->Initialize(laps_, cp_count_, cps_, 1);

    const int track_size = cp_count_;
    int turn = 0;
    csb::StepOptions fidelity{csb::PhysicsProfile::Fidelity};

    while (true) {
        turn++;
        if (verbose) std::cout << "--- Turn " << turn << " ---" << std::endl;

        SyncViewFromGame(game, track_size, pods_);

        // Preserve full 4-pod observation for both bots (arena.cpp historical contract).
        std::vector<PodAction> actions0 = bot0_->GetActions(pods_);
        std::vector<PodAction> actions1 = bot1_->GetActions(pods_);

        game.applyAction(0, static_cast<int>(actions0[0].tx), static_cast<int>(actions0[0].ty), ThrustToken(actions0[0]));
        game.applyAction(1, static_cast<int>(actions0[1].tx), static_cast<int>(actions0[1].ty), ThrustToken(actions0[1]));
        game.applyAction(2, static_cast<int>(actions1[0].tx), static_cast<int>(actions1[0].ty), ThrustToken(actions1[0]));
        game.applyAction(3, static_cast<int>(actions1[1].tx), static_cast<int>(actions1[1].ty), ThrustToken(actions1[1]));
        game.step(fidelity);

        // Terminal conditions solely from Game / progress (not legacy laps_completed path).
        bool team0_won = game.pods[0].won || game.pods[1].won;
        bool team1_won = game.pods[2].won || game.pods[3].won;
        // Prefer winner() if decisive
        int w = game.checkWinner();
        if (w == 0) return {0, turn, "Team 0 finished the race (Fidelity)"};
        if (w == 1) return {1, turn, "Team 1 finished the race (Fidelity)"};
        if (w == -1 && (team0_won || team1_won || !game.teamAlive(0) || !game.teamAlive(1))) {
            return {-1, turn, "Draw (Fidelity terminal)"};
        }

        bool team0_eliminated = game.playerTimeout[0] <= 0;
        bool team1_eliminated = game.playerTimeout[1] <= 0;
        if (team0_eliminated && team1_eliminated)
            return {-1, turn, "Draw (both eliminated by timeout)"};
        if (team0_eliminated)
            return {1, turn, "Team 1 won (Team 0 eliminated by timeout)"};
        if (team1_eliminated)
            return {0, turn, "Team 0 won (Team 1 eliminated by timeout)"};

        if (team0_won && team1_won)
            return {-1, turn, "Draw (both teams finished on same turn)"};
        if (team0_won) return {0, turn, "Team 0 finished the race"};
        if (team1_won) return {1, turn, "Team 1 finished the race"};

        // CG / Fidelity max turns (was 1000 on legacy arena)
        if (turn >= csb::kMaxGameTurns) {
            return {-1, turn, "Draw (Max turns reached)"};
        }
    }
}
