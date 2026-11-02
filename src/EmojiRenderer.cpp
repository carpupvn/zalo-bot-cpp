#include "EmojiRenderer.hpp"
#include "AssetManager.hpp"
#include "TextRenderer.hpp"
#include <cstdio>
#include <iostream>

std::string EmojiRenderer::emoji_dir_ = "assets/emoji";

// UTF-8 → codepoint hex (lowercase, không padding)
std::string EmojiRenderer::utf8_to_codepoint(const std::string& utf8) {
    if (utf8.empty()) return "";
    unsigned char c0 = (unsigned char)utf8[0];
    uint32_t cp = 0;

    if (c0 < 0x80) {
        // ASCII - không phải emoji
        return "";
    } else if ((c0 & 0xE0) == 0xC0) {
        if (utf8.size() < 2) return "";
        cp = ((c0 & 0x1F) << 6) | ((unsigned char)utf8[1] & 0x3F);
    } else if ((c0 & 0xF0) == 0xE0) {
        if (utf8.size() < 3) return "";
        cp = ((c0 & 0x0F) << 12)
           | (((unsigned char)utf8[1] & 0x3F) << 6)
           | ((unsigned char)utf8[2] & 0x3F);
    } else if ((c0 & 0xF8) == 0xF0) {
        if (utf8.size() < 4) return "";
        cp = ((c0 & 0x07) << 18)
           | (((unsigned char)utf8[1] & 0x3F) << 12)
           | (((unsigned char)utf8[2] & 0x3F) << 6)
           | ((unsigned char)utf8[3] & 0x3F);
    } else {
        return "";
    }

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%x", cp);
    return std::string(buf);
}

void EmojiRenderer::init(const std::string& emoji_dir) {
    emoji_dir_ = emoji_dir;
    // Không load sẵn; sẽ load lazy khi cần
    std::cout << "[Emoji] Dir: " << emoji_dir_ << "\n";
}

bool EmojiRenderer::draw(std::vector<uint8_t>& canvas, int W, int H,
                          int x, int y, int size,
                          const std::string& emoji_utf8) {
    std::string cp = utf8_to_codepoint(emoji_utf8);
    if (cp.empty()) return false;

    std::string key = "emoji_" + cp;
    ImageRGBA* img = AssetManager::get(key);
    if (!img) {
        std::string path = emoji_dir_ + "/" + cp + ".png";
        img = AssetManager::load(key, path);
    }
    if (!img || !img->valid()) return false;

    TextRenderer::draw_image_on_canvas(canvas, W, H, img, x, y, size, size);
    return true;
}