#include "Database.hpp"
#include <iostream>

Database::Database(const std::string& db_path) : db_path(db_path), db(nullptr) {}

Database::~Database() {
    if (db) sqlite3_close(db);
}

bool Database::init() {
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[DB] Không thể mở database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    const char* sql_users = 
        "CREATE TABLE IF NOT EXISTS users ("
        "user_id TEXT PRIMARY KEY,"
        "wallet INTEGER DEFAULT 0,"
        "bank INTEGER DEFAULT 0,"
        "debt INTEGER DEFAULT 0,"
        "games_played INTEGER DEFAULT 0,"
        "games_won INTEGER DEFAULT 0"
        ");";
    
    const char* sql_husks =
        "CREATE TABLE IF NOT EXISTS husks ("
        "game_name TEXT PRIMARY KEY,"
        "amount INTEGER DEFAULT 0"
        ");";
    
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql_users, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "[DB] Lỗi tạo bảng users: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    if (sqlite3_exec(db, sql_husks, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "[DB] Lỗi tạo bảng husks: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    
    std::cout << "[DB] Khởi tạo database thành công." << std::endl;
    return true;
}

bool Database::save_user_economy(const std::string& user_id, long long wallet,
                                 long long bank, long long debt, 
                                 int games_played, int games_won) {
    const char* sql = 
        "INSERT OR REPLACE INTO users (user_id, wallet, bank, debt, games_played, games_won) "
        "VALUES (?, ?, ?, ?, ?, ?);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, wallet);
    sqlite3_bind_int64(stmt, 3, bank);
    sqlite3_bind_int64(stmt, 4, debt);
    sqlite3_bind_int(stmt, 5, games_played);
    sqlite3_bind_int(stmt, 6, games_won);
    
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool Database::load_user_economy(const std::string& user_id, long long& wallet,
                                 long long& bank, long long& debt, 
                                 int& games_played, int& games_won) {
    const char* sql = "SELECT wallet, bank, debt, games_played, games_won FROM users WHERE user_id = ?;";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_STATIC);
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        wallet = sqlite3_column_int64(stmt, 0);
        bank = sqlite3_column_int64(stmt, 1);
        debt = sqlite3_column_int64(stmt, 2);
        games_played = sqlite3_column_int(stmt, 3);
        games_won = sqlite3_column_int(stmt, 4);
        found = true;
    }
    
    sqlite3_finalize(stmt);
    return found;
}

bool Database::save_husk(const std::string& game_name, long long amount) {
    const char* sql = "INSERT OR REPLACE INTO husks (game_name, amount) VALUES (?, ?);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, game_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, amount);
    
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool Database::load_husk(const std::string& game_name, long long& amount) {
    const char* sql = "SELECT amount FROM husks WHERE game_name = ?;";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, game_name.c_str(), -1, SQLITE_STATIC);
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        amount = sqlite3_column_int64(stmt, 0);
        found = true;
    }
    
    sqlite3_finalize(stmt);
    return found;
}
// ============================================================
// Top user giàu nhất
// ============================================================
bool Database::get_top_wallet(int limit,
                              std::vector<std::pair<std::string, long long>>& out) {
    if (!db) return false;
    const char* sql = "SELECT username, wallet FROM accounts "
                      "ORDER BY wallet DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* u = (const char*)sqlite3_column_text(stmt, 0);
        long long w = sqlite3_column_int64(stmt, 1);
        if (u) out.emplace_back(std::string(u), w);
    }
    sqlite3_finalize(stmt);
    return true;
}