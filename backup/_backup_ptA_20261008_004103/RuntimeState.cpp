#include "RuntimeState.hpp"

std::atomic<bool> g_running{true};
std::atomic<bool> g_bot_enabled{false};
std::atomic<bool> g_use_image_mode{true};   // ← đổi false → trues
std::atomic<bool> g_econ_dirty{false};
std::atomic<bool> g_game_enabled{false};
