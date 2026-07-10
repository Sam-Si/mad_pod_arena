std::unique_ptr<IBot> CreateGABot(const BotConfig& config) {
    return std::unique_ptr<IBot>(new GABot(config));
}
std::unique_ptr<IBot> CreateGABot(double time_budget_ms) {
    BotConfig config;
    const char* env_threads = std::getenv("BOT_THREADS");
    if (env_threads) config.num_threads = std::max(1, std::atoi(env_threads));
    else config.num_threads = 1;
    config.turn_time_limit_ms = time_budget_ms;
    config.first_turn_time_limit_ms = std::min(1000.0, time_budget_ms * 20.0);
    return CreateGABot(config);
}

// ======== MAIN GAME LOOP ========
