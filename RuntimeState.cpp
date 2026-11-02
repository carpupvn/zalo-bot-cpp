#include "RuntimeState.hpp"

std::atomic<bool> g_running{true};
std::atomic<bool> g_bot_enabled{true};
std::atomic<bool> g_use_image_mode{true};   // ← đổi false → trues
std::atomic<bool> g_econ_dirty{false};
