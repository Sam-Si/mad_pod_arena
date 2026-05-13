import re

with open("tournament.cpp", "r") as f:
    text = f.read()

# Change num_bots and num_rounds
text = re.sub(r'int num_bots = \d+;', 'int num_bots = 32;', text)
text = re.sub(r'int num_rounds = \d+;', 'int num_rounds = 4;', text)

# Add code to print the top 5 bots' configurations at the end
end_idx = text.rfind('return 0;')
if end_idx != -1:
    print_code = """
    std::cout << "\\n--- TOP 5 BOTS CONFIGURATIONS ---" << std::endl;
    for (int i = 0; i < std::min(5, num_bots); ++i) {
        std::cout << i + 1 << ". " << players[i].config.name 
                  << " | Wins: " << players[i].wins 
                  << " | Elo: " << players[i].elo
                  << " | H=" << players[i].config.horizon
                  << " P=" << players[i].config.population
                  << " D=" << players[i].config.dist_weight
                  << " A=" << players[i].config.align_weight
                  << " B=" << players[i].config.block_weight
                  << " S=" << players[i].config.shield_penalty
                  << std::endl;
    }
    """
    text = text[:end_idx] + print_code + text[end_idx:]

with open("fast_tournament.cpp", "w") as f:
    f.write(text)
