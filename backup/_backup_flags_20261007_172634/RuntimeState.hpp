#pragma once
#include <atomic>

extern std::atomic<bool> g_running;
extern std::atomic<bool> g_bot_enabled;
extern std::atomic<bool> g_use_image_mode;
extern std::atomic<bool> g_econ_dirty;