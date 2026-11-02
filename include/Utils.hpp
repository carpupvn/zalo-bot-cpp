#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <cstdint>
#include <string>

namespace render::utils {

using cpp_int = boost::multiprecision::cpp_int;

// 1234567 -> "1.234.567"  (khop formatNumber trong index.js)
std::string format_number(int64_t n);

// Overload cho cpp_int (dùng cho tiền tệ Boost)
std::string format_number(const cpp_int& n);

// Kiem tra URL hop le (http/https)
bool is_valid_url(const std::string& url);

} // namespace render::utils