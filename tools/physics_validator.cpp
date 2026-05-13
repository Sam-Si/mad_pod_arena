#include "engine.h"
#include <iostream>
#include <cmath>

using namespace std;

int main() {
    InitLUT();

    vector<Pod> env(4);
    
    // Turn 1 State
    env[0].pos = Vec2(13756, 8941); env[0].vel = Vec2(0, 0); env[0].angle = -1; env[0].next_cp_id = 1;
    env[1].pos = Vec2(15402, 6433); env[1].vel = Vec2(0, 0); env[1].angle = -1; env[1].next_cp_id = 1;
    env[2].pos = Vec2(14305, 8105); env[2].vel = Vec2(0, 0); env[2].angle = -1; env[2].next_cp_id = 1;
    env[3].pos = Vec2(14853, 7269); env[3].vel = Vec2(0, 0); env[3].angle = -1; env[3].next_cp_id = 1;

    // Turn 1 Actions
    vector<PodAction> actions(4);
    // SamSi (Pods 0 and 1)
    actions[0] = PodAction(10588, 5069, 52);
    actions[1] = PodAction(10588, 5069, 200);
    // Noobkins (Pods 2 and 3)
    actions[2] = PodAction(10588, 5069, 650); // BOOST
    actions[3] = PodAction(10588, 5069, 100);

    // Apply Thrust
    for (int i = 0; i < 4; i++) {
        GameEngine::ApplyThrust(env[i], actions[i]);
    }

    // Play Turn
    GameEngine::PlayTurn(env);

    // Expected Turn 2 State
    vector<Pod> expected(4);
    expected[0].pos = Vec2(13723, 8901); expected[0].vel = Vec2(-27, -34); expected[0].angle = 231;
    expected[1].pos = Vec2(15210, 6378); expected[1].vel = Vec2(-163, -46); expected[1].angle = 196;
    expected[2].pos = Vec2(13802, 7694); expected[2].vel = Vec2(-427, -349); expected[2].angle = 219;
    expected[3].pos = Vec2(14764, 7223); expected[3].vel = Vec2(-75, -38); expected[3].angle = 207;

    bool perfect = true;
    for (int i = 0; i < 4; i++) {
        cout << "Pod " << i << " Simulated: Pos(" << env[i].pos.x << ", " << env[i].pos.y << ") Vel(" << env[i].vel.x << ", " << env[i].vel.y << ") Angle: " << env[i].angle << endl;
        cout << "Pod " << i << " Expected : Pos(" << expected[i].pos.x << ", " << expected[i].pos.y << ") Vel(" << expected[i].vel.x << ", " << expected[i].vel.y << ") Angle: " << expected[i].angle << endl;
        
        if (std::abs(env[i].pos.x - expected[i].pos.x) > 1 || std::abs(env[i].pos.y - expected[i].pos.y) > 1 ||
            std::abs(env[i].vel.x - expected[i].vel.x) > 1 || std::abs(env[i].vel.y - expected[i].vel.y) > 1 ||
            env[i].angle != expected[i].angle) {
            perfect = false;
        }
        cout << "---" << endl;
    }

    if (perfect) cout << "SUCCESS: Physics Engine is perfectly synchronized!" << endl;
    else cout << "FAILURE: Drift detected!" << endl;

    return 0;
}
