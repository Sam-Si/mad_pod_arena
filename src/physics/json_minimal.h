#pragma once
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>

struct Checkpoint {
    double x;
    double y;
};

struct BattleData {
    std::vector<Checkpoint> checkpoints;
    std::map<int, std::string> player0_commands;
    std::map<int, std::string> player1_commands;
    int expected_winner = -1;
    int expected_end_turn = -1;
};

inline std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\",");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\",");
    return str.substr(first, (last - first + 1));
}

inline BattleData parse_battle_json(const std::string& filepath) {
    BattleData data;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << filepath << std::endl;
        return data;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Parse trackCheckpoints
    size_t tcp_start = content.find("\"trackCheckpoints\"");
    if (tcp_start != std::string::npos) {
        size_t tcp_end = content.find("]", tcp_start);
        std::string tcp_section = content.substr(tcp_start, tcp_end - tcp_start);
        size_t pos = 0;
        while (true) {
            size_t x_pos = tcp_section.find("\"x\":", pos);
            if (x_pos == std::string::npos) break;
            size_t y_pos = tcp_section.find("\"y\":", x_pos);
            if (y_pos == std::string::npos) break;

            size_t x_end = tcp_section.find_first_of(",}", x_pos);
            size_t y_end = tcp_section.find_first_of(",}", y_pos);

            std::string x_val = trim(tcp_section.substr(x_pos + 4, x_end - (x_pos + 4)));
            std::string y_val = trim(tcp_section.substr(y_pos + 4, y_end - (y_pos + 4)));

            Checkpoint cp;
            cp.x = std::stod(x_val);
            cp.y = std::stod(y_val);
            data.checkpoints.push_back(cp);

            pos = y_end;
        }
    }

    // Parse playerCommands
    auto parse_player_commands = [&](const std::string& player_key, std::map<int, std::string>& dest) {
        size_t p_start = content.find("\"" + player_key + "\"");
        if (p_start == std::string::npos) return;
        size_t p_end = content.find("]", p_start);
        std::string p_section = content.substr(p_start, p_end - p_start);

        size_t pos = 0;
        while (true) {
            size_t turn_pos = p_section.find("\"turn\":", pos);
            if (turn_pos == std::string::npos) break;
            size_t cmd_pos = p_section.find("\"command\":", turn_pos);
            if (cmd_pos == std::string::npos) break;

            size_t turn_end = p_section.find_first_of(",}", turn_pos);
            size_t cmd_end = p_section.find_first_of(",}", cmd_pos);

            std::string turn_val = trim(p_section.substr(turn_pos + 7, turn_end - (turn_pos + 7)));
            std::string cmd_val = trim(p_section.substr(cmd_pos + 10, cmd_end - (cmd_pos + 10)));

            dest[std::stoi(turn_val)] = cmd_val;
            pos = cmd_end;
        }
    };

    parse_player_commands("Player_0", data.player0_commands);
    parse_player_commands("Player_1", data.player1_commands);

    // Parse gameInformationTimeline to get expected winner and end turn
    size_t git_start = content.find("\"gameInformationTimeline\"");
    if (git_start != std::string::npos) {
        size_t git_end = content.find("]", git_start);
        std::string git_section = content.substr(git_start, git_end - git_start);
        
        size_t pos = 0;
        int last_turn = -1;
        std::string last_log = "";
        while (true) {
            size_t turn_pos = git_section.find("\"turn\":", pos);
            if (turn_pos == std::string::npos) break;
            size_t log_pos = git_section.find("\"log\":", turn_pos);
            if (log_pos == std::string::npos) break;
            
            size_t turn_end = git_section.find_first_of(",}", turn_pos);
            size_t log_end = git_section.find_first_of(",}", log_pos);
            
            std::string turn_val = trim(git_section.substr(turn_pos + 7, turn_end - (turn_pos + 7)));
            
            size_t quote_start = git_section.find("\"", log_pos + 6);
            size_t quote_end = git_section.find("\"", quote_start + 1);
            std::string log_val = "";
            if (quote_start != std::string::npos && quote_end != std::string::npos) {
                log_val = git_section.substr(quote_start + 1, quote_end - (quote_start + 1));
            }
            
            try {
                last_turn = std::stoi(turn_val);
                last_log = log_val;
            } catch (...) {}
            
            pos = (quote_end != std::string::npos) ? quote_end : log_end;
        }
        
        data.expected_end_turn = last_turn;
        
        // Replace escaped newlines with actual newlines
        size_t n_pos = 0;
        while ((n_pos = last_log.find("\\n", n_pos)) != std::string::npos) {
            last_log.replace(n_pos, 2, "\n");
            n_pos += 1;
        }

        // Parse the winner from last_log robustly
        std::stringstream ss(last_log);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.find("rank: 1") != std::string::npos) {
                if (line.find("$0") != std::string::npos) {
                    data.expected_winner = 0;
                } else if (line.find("$1") != std::string::npos) {
                    data.expected_winner = 1;
                }
            }
        }
    }

    return data;
}
