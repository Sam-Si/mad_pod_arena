#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

using namespace std;

const double PI = 3.14159265358979323846;

struct Vec2 {
    double x, y;
    Vec2() : x(0), y(0) {}
    Vec2(double x, double y) : x(x), y(y) {}
};

struct Pod {
    Vec2 pos;
    Vec2 vel;
    double angle;
    int next_cp_id;
    int shield_cd = 0;
};

// Extremely simplified replication of the CSB physics to check Turn 1 -> 2
// Turn 1 to Turn 2 logic:
// Input at T1: angle = -1. Target = (11478, 6099)
// Pod 0 Pos: 5057, 4739
// Target: 11478, 6099
// Angle snap: exactly to target.

double NormalizeAngle(double a) {
    while (a < 0) a += 360.0;
    while (a >= 360.0) a -= 360.0;
    return a;
}

int main() {
    double px = 5057;
    double py = 4739;
    double tx = 11478;
    double ty = 6099;
    
    double dx = tx - px;
    double dy = ty - py;
    double angle = std::round(atan2(dy, dx) * 180.0 / PI);
    angle = NormalizeAngle(angle);
    
    // Thrust 200
    double vx = cos(angle * PI / 180.0) * 200.0;
    double vy = sin(angle * PI / 180.0) * 200.0;
    
    px += vx;
    py += vy;
    
    px = std::round(px);
    py = std::round(py);
    vx = std::trunc(vx * 0.85);
    vy = std::trunc(vy * 0.85);
    
    cout << "Pod 0 Predicted: Pos(" << px << ", " << py << ") Vel(" << vx << ", " << vy << ") Angle: " << angle << endl;
    cout << "Pod 0 Actual   : Pos(5253, 4780) Vel(166, 35) Angle: 12" << endl;
    
    return 0;
}
