// AdminReply.inc - Format cau tra loi cho cac lenh admin
#pragma once
#include <string>
#include <algorithm>
#include "Utils.hpp"
#include <boost/multiprecision/cpp_int.hpp>

using cpp_int = boost::multiprecision::cpp_int;


inline std::string format_big(const cpp_int& n) {
    std::string s = n.convert_to<std::string>();
    bool neg = (!s.empty() && s[0] == 0x2D);
    std::string digits = neg ? s.substr(1) : s;
    std::string out;
    int cnt = 0;
    for (int i = (int)digits.size() - 1; i >= 0; i--) {
        out.push_back(digits[i]);
        cnt++;
        if (cnt % 3 == 0 && i > 0) out.push_back(0x2E);
    }
    if (neg) out.push_back(0x2D);
    std::reverse(out.begin(), out.end());
    return out;
}

namespace admin_reply {

inline std::string buff(cpp_int before, cpp_int after, cpp_int amount) {
    std::string s;
    s += "🔄 Buff tiền thành công!";
    s += "\n\n💰 Số tiền buff: " + format_big(amount) + " VNĐ";
    s += "\n\n📊 Biến động số dư:";
    s += "\n- Trước: " + format_big(before) + " VNĐ";
    s += "\n- Sau: " + format_big(after) + " VNĐ";
    s += "\n✅✅✅";
    return s;
}

inline std::string nerf(cpp_int before, cpp_int after, cpp_int amount) {
    std::string s;
    s += "🔥 Nerf tiền thành công!";
    s += "\n\n💸 Số tiền nerf: " + format_big(amount) + " VNĐ";
    s += "\n\n📊 Biến động số dư:";
    s += "\n- Trước: " + format_big(before) + " VNĐ";
    s += "\n- Sau: " + format_big(after) + " VNĐ";
    s += "\n✅✅✅";
    return s;
}

inline std::string setmoney(cpp_int before, cpp_int after, cpp_int amount) {
    std::string s;
    s += "📝 Set tiền thành công!";
    s += "\n\n💰 Số tiền set: " + format_big(amount) + " VNĐ";
    s += "\n\n📊 Biến động số dư:";
    s += "\n- Trước: " + format_big(before) + " VNĐ";
    s += "\n- Sau: " + format_big(after) + " VNĐ";
    s += "\n✅✅✅";
    return s;
}

inline std::string sethu(const std::string& game, cpp_int before, cpp_int after, cpp_int amount) {
    std::string s;
    s += "🎰 Set hũ thành công!";
    s += "\n\n🎯 Game: " + game;
    s += "\n💰 Số tiền set: " + format_big(amount) + " VNĐ";
    s += "\n\n📊 Biến động hũ:";
    s += "\n- Trước: " + format_big(before) + " VNĐ";
    s += "\n- Sau: " + format_big(after) + " VNĐ";
    s += "\n✅✅✅";
    return s;
}

} // namespace admin_reply
