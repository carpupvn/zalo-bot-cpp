#include "ChromeCookieReader.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <set>
#include <regex>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <sqlite3.h>

// ============================================================
// Crypto helpers
// ============================================================

static std::vector<uint8_t> dpapi_decrypt(const std::vector<uint8_t>& in) {
    if (in.empty()) return {};

    DATA_BLOB in_blob;
    in_blob.cbData = (DWORD)in.size();
    in_blob.pbData = (BYTE*)in.data();

    DATA_BLOB out_blob;
    out_blob.cbData = 0;
    out_blob.pbData = nullptr;

    if (!CryptUnprotectData(&in_blob, nullptr, nullptr, nullptr, nullptr, 0, &out_blob))
        return {};

    std::vector<uint8_t> out(out_blob.pbData, out_blob.pbData + out_blob.cbData);
    LocalFree(out_blob.pbData);
    return out;
}

static std::string base64_decode(const std::string& b64) {
    DWORD out_len = 0;
    CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64,
                         nullptr, &out_len, nullptr, nullptr);
    std::string out(out_len, '\0');
    CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64,
                         (BYTE*)out.data(), &out_len, nullptr, nullptr);
    out.resize(out_len);
    return out;
}

static std::vector<uint8_t> get_master_key(const std::string& profile_dir) {
    auto pos = profile_dir.find_last_of("\\/");
    if (pos == std::string::npos) return {};
    std::string local_state_path = profile_dir.substr(0, pos) + "\\Local State";

    std::ifstream f(local_state_path, std::ios::binary);
    if (!f) return {};
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    auto p = content.find("\"encrypted_key\"");
    if (p == std::string::npos) return {};
    p = content.find('"', p + 15);
    if (p == std::string::npos) return {};
    auto q = content.find('"', p + 1);
    if (q == std::string::npos) return {};
    std::string b64 = content.substr(p + 1, q - p - 1);

    std::string decoded = base64_decode(b64);
    if (decoded.size() < 5) return {};
    std::vector<uint8_t> blob(decoded.begin() + 5, decoded.end());
    return dpapi_decrypt(blob);
}

static std::string aes_gcm_decrypt(const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& in) {
    if (in.size() < 3 + 12 + 16) return {};
    std::string prefix((char*)in.data(), 3);
    if (prefix != "v10" && prefix != "v11") return {};

    std::vector<uint8_t> nonce(in.begin() + 3, in.begin() + 15);
    const size_t tag_len = 16;
    size_t cipher_len = in.size() - 15 - tag_len;

    std::vector<uint8_t> cipher(in.begin() + 15, in.begin() + 15 + cipher_len);
    std::vector<uint8_t> tag(in.begin() + 15 + cipher_len, in.end());

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return {};

    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                     (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                     sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

    BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                (PUCHAR)key.data(), (ULONG)key.size(), 0);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = nonce.data();
    authInfo.cbNonce = (ULONG)nonce.size();
    authInfo.pbTag   = tag.data();
    authInfo.cbTag   = (ULONG)tag.size();

    std::vector<uint8_t> plain(cipher_len);
    ULONG out_len = 0;

    NTSTATUS st = BCryptDecrypt(hKey,
                                 cipher.data(), (ULONG)cipher.size(),
                                 &authInfo, nullptr, 0,
                                 plain.data(), (ULONG)plain.size(),
                                 &out_len, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(st)) return {};
    plain.resize(out_len);
    return std::string(plain.begin(), plain.end());
}

// ============================================================
// ChromeCookieReader
// ============================================================

std::vector<ChromeCookieReader::Cookie>
ChromeCookieReader::read_cookies(const std::string& profile_dir) {
    std::vector<Cookie> result;

    std::string cookies_db = profile_dir + "\\Network\\Cookies";
    std::ifstream test(cookies_db, std::ios::binary);
    if (!test.good()) {
        cookies_db = profile_dir + "\\Cookies";
        std::ifstream t2(cookies_db, std::ios::binary);
        if (!t2.good()) {
            std::cerr << "   [Cookie] Không tìm thấy file Cookies ở: "
                      << profile_dir << "\\Network\\Cookies\n";
            return result;
        }
    }

    auto copy_file = [](const std::string& src_path, const std::string& dst_path) {
        std::ifstream s(src_path, std::ios::binary);
        if (!s.good()) return false;
        std::ofstream d(dst_path, std::ios::binary);
        d << s.rdbuf();
        return true;
    };

    std::string tmp_db = profile_dir + "\\Cookies_tmp_copy";

    copy_file(cookies_db,              tmp_db);
    copy_file(cookies_db + "-wal",     tmp_db + "-wal");
    copy_file(cookies_db + "-shm",     tmp_db + "-shm");
    copy_file(cookies_db + "-journal", tmp_db + "-journal");

    auto master_key = get_master_key(profile_dir);
    if (master_key.empty()) {
        std::cerr << "   [Cookie] Không lấy được master key (Local State)\n";
        return result;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(tmp_db.c_str(), &db) != SQLITE_OK) {
        std::cerr << "   [Cookie] Không mở được SQLite\n";
        return result;
    }

    const std::set<std::string> needed = {
        "zpsid", "zpw_sek", "__zi", "app.event.zalo.me", "_zlang"
    };

    const char* sql = "SELECT name, encrypted_value, value FROM cookies "
                      "WHERE host_key LIKE '%zalo.me%';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "   [Cookie] SQL prepare fail: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return result;
    }

    int total_zalo_cookies = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name_c = (const char*)sqlite3_column_text(stmt, 0);
        const void* enc_blob = sqlite3_column_blob(stmt, 1);
        int enc_len = sqlite3_column_bytes(stmt, 1);
        const char* val_c = (const char*)sqlite3_column_text(stmt, 2);

        if (!name_c) continue;
        std::string name = name_c;
        total_zalo_cookies++;

        std::cerr << "   [Cookie] Tìm thấy cookie: " << name << "\n";

        if (!needed.count(name)) continue;

        std::string value;
        if (enc_len > 0 && enc_blob) {
            std::vector<uint8_t> enc((uint8_t*)enc_blob, (uint8_t*)enc_blob + enc_len);
            value = aes_gcm_decrypt(master_key, enc);
            if (value.empty()) {
                std::cerr << "   [Cookie]   -> decrypt FAILED (" << name << ")\n";
            }
        }
        if (value.empty() && val_c) {
            value = val_c;
        }
        if (value.empty()) continue;

        result.push_back({name, value});
    }

    std::cerr << "   [Cookie] Tổng: " << total_zalo_cookies
              << " cookie zalo.me, lấy được " << result.size() << "\n";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    DeleteFileA(tmp_db.c_str());
    DeleteFileA((tmp_db + "-wal").c_str());
    DeleteFileA((tmp_db + "-shm").c_str());
    DeleteFileA((tmp_db + "-journal").c_str());

    return result;
}

std::string ChromeCookieReader::read_imei(const std::string& profile_dir) {
    std::string ldb_dir = profile_dir + "\\Local Storage\\leveldb\\";

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((ldb_dir + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "";

    std::regex uuid_re("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}-[0-9a-f]{32}");

    std::string found;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        if (name.size() < 4) continue;
        std::string ext = name.substr(name.size() - 4);
        if (ext != ".log" && ext != ".ldb") continue;

        std::string full = ldb_dir + name;
        std::ifstream f(full, std::ios::binary);
        if (!f) continue;
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        for (const char* key : {"sh_z_uuid", "z_uuid"}) {
            auto pos = content.find(key);
            while (pos != std::string::npos) {
                size_t search_end = (std::min)(pos + 300, content.size());
                std::string window = content.substr(pos, search_end - pos);

                std::smatch m;
                if (std::regex_search(window, m, uuid_re)) {
                    found = m.str();
                    FindClose(h);

                    // Lọc chỉ giữ hex + dấu gạch
                    std::string clean;
                    for (char c : found) {
                        if ((c >= '0' && c <= '9') ||
                            (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F') ||
                            c == '-') {
                            clean += c;
                        }
                    }
                    return clean;
                }
                pos = content.find(key, pos + 1);
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return "";
}

std::string ChromeCookieReader::get_chrome_version(const std::string& chrome_path) {
    DWORD ver_size = GetFileVersionInfoSizeA(chrome_path.c_str(), nullptr);
    if (!ver_size) return "";
    std::vector<char> buf(ver_size);
    if (!GetFileVersionInfoA(chrome_path.c_str(), 0, ver_size, buf.data())) return "";

    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueA(buf.data(), "\\", (LPVOID*)&info, &len)) return "";
    if (!info) return "";

    char ver[64];
    std::snprintf(ver, sizeof(ver), "%d.%d.%d.%d",
                  HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                  HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    return ver;
}