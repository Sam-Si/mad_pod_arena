#include <iostream>
#include <string>
#include <cstdlib>

using namespace std;

struct BotConfig {
    string name;
    int horizon;
    int population;
    double dist_weight;
    double align_weight;
    double block_weight;
    double shield_penalty;
};

BotConfig RandomConfig(int id) {
    BotConfig c;
    c.name = "Bot_" + std::to_string(id);
    c.horizon = (rand() % 2 == 0) ? 4 : 6;
    c.population = (rand() % 2 == 0) ? 20 : 40;
    
    double dists[] = {0.5, 1.0, 2.0};
    c.dist_weight = dists[rand() % 3];
    
    double aligns[] = {1.0, 3.0, 5.0};
    c.align_weight = aligns[rand() % 3];
    
    double blocks[] = {0.0, 2.0, 5.0};
    c.block_weight = blocks[rand() % 3];
    
    double shields[] = {0.0, 50.0};
    c.shield_penalty = shields[rand() % 2];
    
    return c;
}

int main() {
    srand(42);
    for (int i = 0; i <= 449; ++i) {
        BotConfig c = RandomConfig(i);
        if (i == 449) {
            cout << "Bot 449 config:" << endl;
            cout << "Horizon: " << c.horizon << endl;
            cout << "Population: " << c.population << endl;
            cout << "Dist Weight: " << c.dist_weight << endl;
            cout << "Align Weight: " << c.align_weight << endl;
            cout << "Block Weight: " << c.block_weight << endl;
            cout << "Shield Penalty: " << c.shield_penalty << endl;
        }
    }
    return 0;
}
