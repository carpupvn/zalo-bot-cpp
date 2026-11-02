#pragma once
#include <string>
#include <vector>
#include <cstdint>

class EmojiRenderer {
public:
    // Load emoji vào AssetManager (gọi 1 lần lúc startup)
    static void init(const std::string& emoji_dir);

    // Vẽ emoji UTF-8 tại (x, y) với kích thước `size`
    // Trả về false nếu không tìm thấy emoji
    static bool draw(std::vector<uint8_t>& canvas, int W, int H,
                     int x, int y, int size,
                     const std::string& emoji_utf8);

    // Tra cứu codepoint hex (lowercase, vd "1f4d6") từ UTF-8 emoji
    // Trả về "" nếu không phải emoji đơn
    static std::string utf8_to_codepoint(const std::string& utf8);

private:
    static std::string emoji_dir_;
};