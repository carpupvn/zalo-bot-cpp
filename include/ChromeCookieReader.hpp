#pragma once
#include <string>
#include <vector>

class ChromeCookieReader {
public:
    struct Cookie {
        std::string name;
        std::string value;
    };

    // Đọc cookie zalo.me từ profile
    static std::vector<Cookie> read_cookies(const std::string& profile_dir);

    // Đọc IMEI từ LocalStorage
    static std::string read_imei(const std::string& profile_dir);

    // Lấy version Chrome từ chrome.exe
    static std::string get_chrome_version(const std::string& chrome_path);
};