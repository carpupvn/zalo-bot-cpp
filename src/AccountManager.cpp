#include "AccountManager.hpp"
#include <iostream>
#include <fstream>
#include <random>
#include <vector>
#include <cstdio>
#include <cctype>
#include <windows.h>
#include <bcrypt.h>

AccountManager::AccountManager() {}

AccountManager::~AccountManager() {
    if (db_) sqlite3_close(db_);
}

bool AccountManager::init(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[Account] Không mở được DB: " << db_path << "\n";
        return false;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS accounts ("
        "  username TEXT PRIMARY KEY,"
        "  password_hash TEXT NOT NULL,"
        "  salt TEXT NOT NULL,"
        "  wallet TEXT DEFAULT '0',"
        "  bank TEXT DEFAULT '0',"
        "  debt TEXT DEFAULT '0',"
        "  games_played INTEGER DEFAULT 0,"
        "  games_won INTEGER DEFAULT 0"
        ");";

    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "[Account] Lỗi tạo bảng: " << (err ? err : "?") << "\n";
        sqlite3_free(err);
        return false;
    }

    std::cout << "[Account] DB khởi tạo OK: " << db_path << "\n";
    return true;
}

// ============================================================
// SHA256 hex (dùng Windows BCrypt)
// ============================================================
std::string AccountManager::sha256_hex(const std::string& input) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::string result;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return "";

    DWORD hash_len = 0, cb = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
                      (PUCHAR)&hash_len, sizeof(hash_len), &cb, 0);

    std::vector<UCHAR> hash(hash_len);
    BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(hHash, (PUCHAR)input.c_str(), (ULONG)input.size(), 0);
    BCryptFinishHash(hHash, hash.data(), hash_len, 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    char buf[4];
    for (auto b : hash) {
        std::snprintf(buf, sizeof(buf), "%02x", b);
        result += buf;
    }
    return result;
}

std::string AccountManager::generate_salt() {
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::string s;
    for (int i = 0; i < 16; i++) s += hex[dis(gen)];
    return s;
}

// ============================================================
// Validate
// ============================================================
bool AccountManager::validate_username(const std::string& u, std::string& err) {
    if (u.size() < 3 || u.size() > 20) {
        err = "Username phải từ 3-20 ký tự";
        return false;
    }
    for (char c : u) {
        if (!(std::isalnum((unsigned char)c) || c == '_')) {
            err = "Username chỉ được chứa chữ, số và _";
            return false;
        }
    }
    return true;
}

bool AccountManager::validate_password(const std::string& p, std::string& err) {
    if (p.size() < 6) { err = "Mật khẩu phải từ 6 ký tự trở lên"; return false; }
    if (p.size() > 64) { err = "Mật khẩu quá dài (tối đa 64)"; return false; }
    return true;
}

// ============================================================
// Register
// ============================================================
std::string AccountManager::register_account(const std::string& username,
                                              const std::string& password) {
    std::string err;
    if (!validate_username(username, err)) return err;
    if (!validate_password(password, err)) return err;

    if (username_exists(username)) return "Username đã tồn tại";

    std::string salt = generate_salt();
    std::string hash = sha256_hex(salt + ":" + username + ":" + password);
    if (hash.empty()) return "Lỗi hash";

    const char* sql =
        "INSERT INTO accounts (username, password_hash, salt) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return "Lỗi DB";

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return "Lỗi ghi DB";
    return "";
}

bool AccountManager::username_exists(const std::string& username) const {
    const char* sql = "SELECT 1 FROM accounts WHERE username = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

// ============================================================
// Login
// ============================================================
std::string AccountManager::login_user(const std::string& username,
                                        const std::string& password,
                                        const std::string& uid) {
    const char* sql = "SELECT username, password_hash, salt FROM accounts WHERE username = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return "Lỗi DB";

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return "Username không tồn tại";
    }

    std::string stored_hash = (const char*)sqlite3_column_text(stmt, 1);
    std::string salt        = (const char*)sqlite3_column_text(stmt, 2);
    sqlite3_finalize(stmt);

    std::string computed = sha256_hex(salt + ":" + username + ":" + password);
    if (computed != stored_hash) return "Sai mật khẩu";

    std::lock_guard<std::mutex> lk(mtx_);

    // Nếu uid này đang login user khác → logout trước
    auto old = uid_to_username_.find(uid);
    if (old != uid_to_username_.end()) {
        username_to_uid_.erase(old->second);
        uid_to_username_.erase(old);
    }
    // Nếu username đã login ở uid khác → kick uid cũ
    auto old2 = username_to_uid_.find(username);
    if (old2 != username_to_uid_.end() && old2->second != uid) {
        uid_to_username_.erase(old2->second);
        username_to_uid_.erase(old2);
    }

    uid_to_username_[uid] = username;
    username_to_uid_[username] = uid;

    return "";
}

void AccountManager::logout(const std::string& uid) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = uid_to_username_.find(uid);
    if (it != uid_to_username_.end()) {
        username_to_uid_.erase(it->second);
        uid_to_username_.erase(it);
    }
}

bool AccountManager::is_logged_in(const std::string& uid) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return uid_to_username_.count(uid) > 0;
}

std::string AccountManager::get_username(const std::string& uid) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = uid_to_username_.find(uid);
    return (it != uid_to_username_.end()) ? it->second : "";
}

std::string AccountManager::get_uid_by_username(const std::string& username) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = username_to_uid_.find(username);
    return (it != username_to_uid_.end()) ? it->second : "";
}
// ============================================================
// Top user giàu nhất (theo wallet) — query accounts.db
// ============================================================
// ============================================================
// Top user giàu nhất (theo wallet) — wallet lưu TEXT (cpp_int)
// ============================================================
bool AccountManager::get_top_wallet(int limit,
                                    std::vector<std::pair<std::string, cpp_int>>& out) const {
    if (!db_) return false;
    if (limit <= 0) return true;
    const char* sql =
        "SELECT username, bank FROM accounts WHERE username != "" AND username IS NOT NULL WHERE username != "" AND username IS NOT NULL "
        "WHERE bank IS NOT NULL AND bank != '' "
        "ORDER BY LENGTH(bank) DESC, bank DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* u = (const char*)sqlite3_column_text(stmt, 0);
        const char* w = (const char*)sqlite3_column_text(stmt, 1);
        if (!u) continue;
        cpp_int val = 0;
        if (w && *w) {
            try { val = cpp_int(std::string(w)); } catch (...) { val = 0; }
        }
        out.emplace_back(std::string(u), val);
    }
    sqlite3_finalize(stmt);
    return true;
}
// ============================================================
// Economy persistence
// ============================================================
bool AccountManager::load_economy(const std::string& username, AccountEconomyData& out) const {
    if (!db_) return false;
    const char* sql = "SELECT wallet, bank, debt, games_played, games_won "
                      "FROM accounts WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* w = (const char*)sqlite3_column_text(stmt, 0);
        const char* b = (const char*)sqlite3_column_text(stmt, 1);
        const char* d = (const char*)sqlite3_column_text(stmt, 2);
        out.wallet = (w && *w) ? cpp_int(std::string(w)) : cpp_int(0);
        out.bank   = (b && *b) ? cpp_int(std::string(b)) : cpp_int(0);
        out.debt   = (d && *d) ? cpp_int(std::string(d)) : cpp_int(0);
        out.games_played = sqlite3_column_int(stmt, 3);
        out.games_won    = sqlite3_column_int(stmt, 4);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool AccountManager::save_economy(const std::string& username, const AccountEconomyData& in) {
    if (!db_) return false;
    const char* sql = "UPDATE accounts SET wallet=?, bank=?, debt=?, "
                      "games_played=?, games_won=? WHERE username=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string ws = in.wallet.convert_to<std::string>();
    std::string bs = in.bank.convert_to<std::string>();
    std::string ds = in.debt.convert_to<std::string>();
    sqlite3_bind_text(stmt, 1, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, bs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ds.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, in.games_played);
    sqlite3_bind_int(stmt, 5, in.games_won);
    sqlite3_bind_text(stmt, 6, username.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::map<std::string, std::string> AccountManager::get_logged_in_map() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return uid_to_username_;
}



void AccountManager::export_to_txt(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "=== DANH SACH TAI KHOAN ZALO BOT ===\n";
    f << "Format: Username | UID | Wallet | Bank | Debt\n\n";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, "SELECT username, uid, wallet, bank, debt FROM accounts", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string un = (const char*)sqlite3_column_text(stmt, 0);
            std::string uid = (const char*)sqlite3_column_text(stmt, 1);
            std::string w = (const char*)sqlite3_column_text(stmt, 2);
            std::string b = (const char*)sqlite3_column_text(stmt, 3);
            std::string d = (const char*)sqlite3_column_text(stmt, 4);
            f << un << " | " << uid << " | " << w << " | " << b << " | " << d << "\n";
        }
        sqlite3_finalize(stmt);
    }
    f.close();
}

void AccountManager::restore_session(const std::string& uid, const std::string& username) {
    uid_to_username_[uid] = username;
}
