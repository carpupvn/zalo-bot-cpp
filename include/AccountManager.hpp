#pragma once
#include <string>
#include <map>
#include <vector>
#include <utility>
#include <mutex>
#include <sqlite3.h>
#include <boost/multiprecision/cpp_int.hpp>

using cpp_int = boost::multiprecision::cpp_int;

struct AccountEconomyData {
    cpp_int wallet = 0;
    cpp_int bank = 0;
    cpp_int debt = 0;
    int games_played = 0;
    int games_won = 0;
    cpp_int total_won = 0;
    cpp_int total_lost = 0;
    long long last_daily = 0;
};

class AccountManager {
public:
    AccountManager();
    ~AccountManager();

    bool init(const std::string& db_path);

    std::string register_account(const std::string& username,
                                 const std::string& password);
    std::string login_user(const std::string& username,
                           const std::string& password,
                           const std::string& uid);
    void logout(const std::string& uid);
    void restore_session(const std::string& uid, const std::string& username);

    bool is_logged_in(const std::string& uid) const;
    std::string get_username(const std::string& uid) const;
    std::string get_uid_by_username(const std::string& username) const;
    bool username_exists(const std::string& username) const;

    bool load_economy(const std::string& username, AccountEconomyData& out) const;
    bool save_economy(const std::string& username, const AccountEconomyData& in);
    std::map<std::string, std::string> get_logged_in_map() const;

    void export_to_txt(const std::string& path);
    bool get_top_wallet(int limit,
                        std::vector<std::pair<std::string, cpp_int>>& out) const;

private:
    sqlite3* db_ = nullptr;

    std::map<std::string, std::string> uid_to_username_;
    std::map<std::string, std::string> username_to_uid_;
    mutable std::mutex mtx_;

    std::string sha256_hex(const std::string& input);
    std::string generate_salt();
    bool validate_username(const std::string& u, std::string& err);
    bool validate_password(const std::string& p, std::string& err);
};
