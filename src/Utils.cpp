#include "Utils.hpp"
#include <algorithm>
#include <cmath>

namespace render::utils {

std::string format_number(int64_t n) {
    bool neg = n < 0;
    std::string s = std::to_string(neg ? -n : n);
    std::string out;
    out.reserve(s.size() + s.size() / 3);
    int count = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        out.push_back(s[i]);
        count++;
        if (count % 3 == 0 && i > 0) out.push_back('.');
    }
    if (neg) out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

// Overload cho cpp_int (dùng chung logic format dấu chấm)
std::string format_number(const cpp_int& n) {
    std::string s = n.convert_to<std::string>();
    bool neg = (!s.empty() && s[0] == '-');
    std::string digits = neg ? s.substr(1) : s;
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    int count = 0;
    for (int i = (int)digits.size() - 1; i >= 0; i--) {
        out.push_back(digits[i]);
        count++;
        if (count % 3 == 0 && i > 0) out.push_back('.');
    }
    if (neg) out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

bool is_valid_url(const std::string& url) {
    if (url.size() < 10) return false;
    bool has_scheme =
        url.rfind("http://", 0) == 0 ||
        url.rfind("https://", 0) == 0;
    if (!has_scheme) return false;
    size_t host_start = url.find("://") + 3;
    size_t host_end = url.find('/', host_start);
    if (host_end == std::string::npos) host_end = url.size();
    std::string host = url.substr(host_start, host_end - host_start);
    if (host.empty()) return false;
    if (host.find('.') == std::string::npos) return false;
    if (host.find(' ') != std::string::npos) return false;
    return true;
}

} // namespace render::utils