#pragma once
// Public API for GA bot library (tournament links this; no include-cpp).
#include "src/engine/bot.h"
#include "src/cg/bot_config.h"
#include <memory>

std::unique_ptr<IBot> CreateGABot(const BotConfig& config);
std::unique_ptr<IBot> CreateGABot(double time_budget_ms = 7.5);

void SetGABotVerbose(bool verbose);
