#pragma once
#include <string>
#include <vector>
#include <utility>
#include <sqlite3.h>

class Database {
public:
    Database(const std::string& db_path);
    ~Database();
    
    bool init();
    bool save_user_economy(const std::string& user_id, long long wallet, 
                           long long bank, long long debt, int games_played, int games_won);
    bool load_user_economy(const std::string& user_id, long long& wallet,
                           long long& bank, long long& debt, int& games_played, int& games_won);
    
    bool save_husk(const std::string& game_name, long long amount);
    bool load_husk(const std::string& game_name, long long& amount);

    // Top user giàu nhất theo wallet (username, wallet)
    bool get_top_wallet(int limit,
                        std::vector<std::pair<std::string, long long>>& out);
    
private:
    sqlite3* db;
    std::string db_path;
};