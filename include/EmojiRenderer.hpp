#pragma once
#include <string>
#include <vector>
#include <cstdint>

class EmojiRenderer {
public:
    static void init(const std::string& emoji_dir);

    static bool draw(std::vector<uint8_t>& canvas, int W, int H,
                     int x, int y, int size,
                     const std::string& emoji_utf8);

    static bool draw_codepoint(std::vector<uint8_t>& canvas, int W, int H,
                               int x, int y, int size, uint32_t cp);

    static bool has_codepoint(uint32_t cp);

    static std::string utf8_to_codepoint(const std::string& utf8);

private:
    static std::string emoji_dir_;
};
