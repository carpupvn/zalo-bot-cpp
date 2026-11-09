#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace safemem {

// Kiem tra 1 byte co phai la UTF-8 continuation (10xxxxxx) khong
bool is_continuation(unsigned char c);

// Do dai (byte) cua 1 ky tu UTF-8 bat dau bang byte c. 0 = invalid.
std::size_t utf8_char_len(unsigned char c);

// Sua chuoi: byte nao khong hop le UTF-8 -> thay bang '?'
// Dam bao khong con byte nao gay json::parse / nlohmann::json crash
std::string sanitize_utf8(const std::string& input);

// Cat chuoi an toan (khong cat giua ky tu UTF-8)
std::string truncate_utf8(const std::string& input, std::size_t max_bytes);

// Chia chuoi thanh cac doan <= max_bytes
// Uu tien cat o newline hoac space de van de doc
std::vector<std::string> split_utf8(const std::string& input, std::size_t max_bytes);

// Ep giai phong bo nho cua string (swap voi empty)
void force_clear(std::string& s);

// Ep giai phong vector
template<typename T>
inline void force_clear_vec(std::vector<T>& v) {
    std::vector<T>().swap(v);
}

} // namespace safemem