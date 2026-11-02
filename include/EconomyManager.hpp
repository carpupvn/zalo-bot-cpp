#pragma once
#include <string>
#include <map>
#include <mutex>
#include <ctime>
#include <boost/multiprecision/cpp_int.hpp>

using cpp_int = boost::multiprecision::cpp_int;

struct UserEconomy {
    cpp_int wallet = 0;      // Vi ca nhan (rut ra, nap vao)
    cpp_int bank = 0;        // TK Bot (game, admin)
    cpp_int debt = 0;        // No
    cpp_int total_wagered = 0;
    cpp_int total_won = 0;
    cpp_int total_lost = 0;
    int games_played = 0;
    int games_won = 0;
};

class EconomyManager {
public:
    // Nap/rut: wallet <-> bank
    bool deposit(const std::string& user_id, cpp_int amount);
    bool withdraw(const std::string& user_id, cpp_int amount);

    // Chuyen khoan (bank <-> bank)
    bool transfer(const std::string& from, const std::string& to, cpp_int amount);

    // Vay ngan hang
    bool borrow(const std::string& user_id, cpp_int amount);
    bool repay(const std::string& user_id, cpp_int amount);
    void apply_interest();

    // Game
    bool place_bet(const std::string& user_id, cpp_int amount);
    void add_winnings(const std::string& user_id, cpp_int amount);
    void record_loss(const std::string& user_id, cpp_int amount);

    // Admin
    void set_money(const std::string& user_id, cpp_int amount);
    void buff(const std::string& user_id, cpp_int amount);
    void nerf(const std::string& user_id, cpp_int amount);

    // Truy van
    cpp_int get_wallet(const std::string& user_id);
    cpp_int get_bank(const std::string& user_id);
    cpp_int get_debt(const std::string& user_id);
    UserEconomy get_user_economy(const std::string& user_id);

    void set_full_data(const std::string& user_id, cpp_int wallet, cpp_int bank, cpp_int debt, int gp, int gw);

    // Hu game
    void set_husk(const std::string& game_name, cpp_int amount);
    cpp_int get_husk(const std::string& game_name);
    void add_to_husk(const std::string& game_name, cpp_int amount);
    void subtract_from_husk(const std::string& game_name, cpp_int amount);

    // Daily
    std::string claim_daily(const std::string& user_id, cpp_int reward);

private:
    std::map<std::string, UserEconomy> users;
    std::map<std::string, cpp_int> husks;
    std::map<std::string, std::time_t> last_daily_;
    std::mutex mtx;
};
