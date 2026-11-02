#include "RuntimeState.hpp"

std::atomic<bool> g_running{true};
std::atomic<bool> g_bot_enabled{false};
std::atomic<bool> g_use_image_mode{true};   // ← đổi false → trues
std::atomic<bool> g_econ_dirty{false};
std::atomic<bool> g_game_enabled{false};

// ============================================================
// Per-thread state (chi cho group)
// ============================================================
#include <map>
#include <mutex>

static std::map<std::string, bool> g_bot_map;
static std::map<std::string, bool> g_game_map;
static std::mutex g_perthread_mtx;

bool get_bot_for_thread(const std::string& tid) {
    if (tid.empty()) return g_bot_enabled.load();
    std::lock_guard<std::mutex> lk(g_perthread_mtx);
    auto it = g_bot_map.find(tid);
    if (it != g_bot_map.end()) return it->second;
    return g_bot_enabled.load();
}
void set_bot_for_thread(const std::string& tid, bool v) {
    if (tid.empty()) return;
    std::lock_guard<std::mutex> lk(g_perthread_mtx);
    g_bot_map[tid] = v;
}
bool get_game_for_thread(const std::string& tid) {
    if (tid.empty()) return g_game_enabled.load();
    std::lock_guard<std::mutex> lk(g_perthread_mtx);
    auto it = g_game_map.find(tid);
    if (it != g_game_map.end()) return it->second;
    return g_game_enabled.load();
}
void set_game_for_thread(const std::string& tid, bool v) {
    if (tid.empty()) return;
    std::lock_guard<std::mutex> lk(g_perthread_mtx);
    g_game_map[tid] = v;
}
void reset_thread_state() {
    std::lock_guard<std::mutex> lk(g_perthread_mtx);
    g_bot_map.clear();
    g_game_map.clear();
}