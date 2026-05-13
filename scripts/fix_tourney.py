with open("tournament.cpp", "r") as f:
    text = f.read()

# Increase bots to 2048 and rounds to 50
text = text.replace("int num_bots = 1024;", "int num_bots = 4096; // Large simulation")
text = text.replace("int num_rounds = 10;", "int num_rounds = 30; // ~30 min run depending on threads")

with open("tournament.cpp", "w") as f:
    f.write(text)
