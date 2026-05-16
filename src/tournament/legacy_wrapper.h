#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <memory>
#include <mutex>
#include "src/engine/bot.h"

// Renaming conflicting symbols during legacy include
#define PI LEGACY_PI
#define main legacy_main
#define Bot LegacyBotBase
#include "legacy_bot/legacy_bot.cpp"
#undef Bot
#undef main
#undef PI

class LegacyBotWrapper : public IBot {
    Environment env;
    int team_id;
    int cp_count;

public:
    LegacyBotWrapper() : team_id(0), cp_count(0) {
        env.game.mapData = &env.mapData;
    }

    void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) override {
        this->team_id = team_id;
        this->cp_count = cp_count;
        env.mapData.nLaps = laps;
        env.mapData.nBeacons = cp_count;
        for (int i = 0; i < cp_count; i++) {
            env.mapData.beacons[i].pos = {cps[i].x, cps[i].y};
        }
        env.mapData.preCalculateBeaconStuff();
        env.firstRoundInput = true;
    }

    std::vector<PodAction> GetActions(const std::vector<Pod>& pods) override {
        // Sync our internal game state
        int our_start = (team_id == 0) ? 0 : 2;
        int opp_start = (team_id == 0) ? 2 : 0;

        auto sync_ship = [&](int legacy_idx, int source_idx) {
            auto& s = env.game.ships[legacy_idx];
            auto& p = pods[source_idx];
            s.pos = {p.pos.x, p.pos.y};
            s.speed = {p.vel.x, p.vel.y};
            s.angle = Angle(p.angle * LEGACY_PI / 180.0);
            s.nextBeacon = p.next_cp_id;
            s.lapNumber = p.laps_completed;
            s.shieldCounter = (p.shield_cd > 0) ? p.shield_cd + 1 : 0;
            s.inverseShipMass = (p.shield_cd > 0) ? 0.1 : 1.0;
        };

        sync_ship(0, our_start);
        sync_ship(1, our_start + 1);
        sync_ship(2, opp_start);
        sync_ship(3, opp_start + 1);

        env.firstRoundInput = false;

        // Run the metabot
        std::pair<GameAction, GameAction> actions = env.metaBot.getNextActions(env.game);

        std::vector<PodAction> result(2);
        auto map_action = [&](const GameAction& ga, int legacy_idx) {
            auto& s = env.game.ships[legacy_idx];
            double final_angle = s.angle.angleValue + ga.deltaAngle;
            double tx = s.pos.x + std::cos(final_angle) * 10000.0;
            double ty = s.pos.y + std::sin(final_angle) * 10000.0;
            int thrust = ga.isShield ? -1 : ga.thrust;
            return PodAction{tx, ty, thrust};
        };

        result[0] = map_action(actions.first, 0);
        result[1] = map_action(actions.second, 1);
        return result;
    }

    std::string GetName() const override {
        return "LegacyBot";
    }
};
