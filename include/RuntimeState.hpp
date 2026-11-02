#include <string>
#pragma once
#include <atomic>

extern std::atomic<bool> g_running;
extern std::atomic<bool> g_bot_enabled;
extern std::atomic<bool> g_use_image_mode;
extern std::atomic<bool> g_econ_dirty;
extern std::atomic<bool> g_game_enabled;

// Per-thread state (tid empty -> use default)
bool get_bot_for_thread(const std::string& tid);
void set_bot_for_thread(const std::string& tid, bool v);
bool get_game_for_thread(const std::string& tid);
void set_game_for_thread(const std::string& tid, bool v);
void reset_thread_state();
