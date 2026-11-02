#include "EconomyManager.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>

// ============================================================
// Nap / Rut: wallet <-> bank
// ============================================================
bool EconomyManager::deposit(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    if (amount <= 0) return false;
    if (users[user_id].wallet < amount) return false;
    users[user_id].wallet -= amount;
    users[user_id].bank += amount;
    return true;
}

bool EconomyManager::withdraw(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    if (amount <= 0) return false;
    if (users[user_id].bank < amount) return false;
    users[user_id].bank -= amount;
    users[user_id].wallet += amount;
    return true;
}

// ============================================================
// Chuyen khoan bank -> bank
// ============================================================
bool EconomyManager::transfer(const std::string& from, const std::string& to, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    if (amount <= 0) return false;
    if (from == to) return false;
    if (users[from].bank < amount) return false;
    users[from].bank -= amount;
    users[to].bank += amount;
    return true;
}

// ============================================================
// Vay / Tra
// ============================================================
bool EconomyManager::borrow(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    if (amount <= 0) return false;
    if (amount > 1000000) return false;
    users[user_id].bank += amount;
    // debt = amount * (1 + 0.05) = amount * 105 / 100
    users[user_id].debt += amount * 105 / 100;
    return true;
}

bool EconomyManager::repay(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    if (amount <= 0) return false;
    if (users[user_id].bank < amount) return false;
    if (users[user_id].debt <= 0) return false;
    cpp_int repay_amount = (amount < users[user_id].debt) ? amount : users[user_id].debt;
    users[user_id].bank -= repay_amount;
    users[user_id].debt -= repay_amount;
    return true;
}

void EconomyManager::apply_interest() {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& kv : users) {
        if (kv.second.debt > 0) {
            kv.second.debt = kv.second.debt * 105 / 100;
        }
    }
}

// ============================================================
// Game
// ============================================================
bool EconomyManager::place_bet(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    if (amount <= 0) return false;
    if (users[user_id].bank < amount) return false;
    users[user_id].bank -= amount;
    users[user_id].total_wagered += amount;
    return true;
}

void EconomyManager::add_winnings(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    users[user_id].bank += amount;
    users[user_id].total_won += amount;
}

void EconomyManager::record_loss(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    users[user_id].total_lost += amount;
}

// ============================================================
// Admin
// ============================================================
void EconomyManager::set_money(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    users[user_id].bank = amount;
}

void EconomyManager::buff(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    users[user_id].bank += amount;
}

void EconomyManager::nerf(const std::string& user_id, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    if (users[user_id].bank > amount) {
        users[user_id].bank -= amount;
    } else {
        users[user_id].bank = 0;
    }
}

// ============================================================
// Truy van
// ============================================================
cpp_int EconomyManager::get_wallet(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx);
    return users[user_id].wallet;
}

cpp_int EconomyManager::get_bank(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx);
    return users[user_id].bank;
}

cpp_int EconomyManager::get_debt(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx);
    return users[user_id].debt;
}

UserEconomy EconomyManager::get_user_economy(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx);
    return users[user_id];
}

void EconomyManager::set_full_data(const std::string& user_id, cpp_int wallet, cpp_int bank, cpp_int debt, int gp, int gw) {
    std::lock_guard<std::mutex> lock(mtx);
    UserEconomy& e = users[user_id];
    e.wallet = wallet;
    e.bank = bank;
    e.debt = debt;
    e.games_played = gp;
    e.games_won = gw;
}

// ============================================================
// Hu game
// ============================================================
void EconomyManager::set_husk(const std::string& game_name, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    husks[game_name] = amount;
}

cpp_int EconomyManager::get_husk(const std::string& game_name) {
    std::lock_guard<std::mutex> lock(mtx);
    return husks[game_name];
}

void EconomyManager::add_to_husk(const std::string& game_name, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    husks[game_name] += amount;
}

void EconomyManager::subtract_from_husk(const std::string& game_name, cpp_int amount) {
    std::lock_guard<std::mutex> lock(mtx);
    if (husks[game_name] > amount) {
        husks[game_name] -= amount;
    } else {
        husks[game_name] = 0;
    }
}

// ============================================================
// Daily
// ============================================================
std::string EconomyManager::claim_daily(const std::string& user_id, cpp_int reward) {
    std::lock_guard<std::mutex> lock(mtx);
    auto now = std::time(nullptr);
    const long long COOLDOWN = 24LL * 3600;

    auto it = last_daily_.find(user_id);
    if (it != last_daily_.end()) {
        long long diff = (long long)(now - it->second);
        if (diff < COOLDOWN) {
            long long left = COOLDOWN - diff;
            long long h = left / 3600;
            long long m = (left % 3600) / 60;
            long long s = left % 60;
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "Còn %lldh %lldm %llds mới điểm danh được",
                          h, m, s);
            return std::string(buf);
        }
    }
    last_daily_[user_id] = now;
    users[user_id].bank += reward;
    return "";
}
