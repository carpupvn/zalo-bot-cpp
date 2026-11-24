#include "EmojiRenderer.hpp"
#include "AssetManager.hpp"
#include "TextRenderer.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <mutex>

std::string EmojiRenderer::emoji_dir_ = "assets/emoji";

static std::unordered_set<uint32_t> g_missing_cp;
static std::mutex g_missing_mtx;

std::string EmojiRenderer::utf8_to_codepoint(const std::string& utf8) {
    if (utf8.empty()) return "";
    unsigned char c0 = (unsigned char)utf8[0];
    uint32_t cp = 0;
    if (c0 < 0x80) {
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
    std::cout << "[Emoji] Dir: " << emoji_dir_ << "\n";
}

// Fallback alias for common emojis missing from assets/emoji/
static uint32_t alias_cp(uint32_t cp) {
    switch (cp) {
        case 0x1F389: return 0x2728;  // 🎉 -> ✨
        case 0x1F38A: return 0x2728;  // 🎊 -> ✨
        case 0x1F91D: return 0x2705;  // 🤝 -> ✅
        case 0x1F622: return 0x1F480; // 😢 -> 💀 (or procedural)
        case 0x1F48E: return 0x2B50;  // 💎 -> ⭐
        case 0x1F331: return 0x2728;  // 🌱 -> ✨
        case 0x1F6E0: return 0x1F527; // 🛠️ -> 🔧
        case 0x1F4DB: return 0x1FAAA; // 📛 -> 🪪
        case 0x1F382: return 0x1F381; // 🎂 -> 🎁
        case 0x26A7:  return 0x1F464; // ⚧ -> 👤
        case 0x23F0:  return 0x23F1;  // ⏰ -> ⏱
        case 0x1F600: case 0x1F604: return 0x2B50;
        case 0x1F44B: return 0x2728;  // 👋
        case 0x1F5D1: return 0x274C;  // 🗑
        case 0x26D4:  return 0x1F6AB; // ⛔ -> 🚫
        default: return 0;
    }
}

static bool has_procedural_icon(uint32_t cp) {
    switch (cp) {
        case 0x1F48E: // 💎 diamond
        case 0x1F331: // 🌱 seedling
        case 0x1F389: // 🎉 party sparkle
        case 0x1F91D: // 🤝 handshake / draw badge
        case 0x1F622: // 😢 sad badge
        case 0x270A:  // ✊ fist
        case 0x270C:  // ✌️ victory
        case 0x1F590: // 🖐️ palm
        case 0x26AA:  // ⚪
        case 0x26AB:  // ⚫
        case 0x1F534: // 🔴
        case 0x1F535: // 🔵
        case 0x1F7E2: // 🟢
        case 0x1F7E1: // 🟡
            return true;
        default:
            return false;
    }
}

static void draw_procedural_icon(std::vector<uint8_t>& canvas, int W, int H,
                                 int x, int y, int sz, uint32_t cp) {
    int cx = x + sz / 2;
    int cy = y + sz / 2;
    int r = std::max(3, sz * 40 / 100);
    if (cp == 0x26AA) {
        TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 240, 244, 250, 255);
        return;
    }
    if (cp == 0x26AB) {
        TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 30, 35, 48, 255);
        return;
    }
    if (cp == 0x1F534) { TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 239, 68, 68, 255); return; }
    if (cp == 0x1F535) { TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 59, 130, 246, 255); return; }
    if (cp == 0x1F7E2) { TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 34, 197, 94, 255); return; }
    if (cp == 0x1F7E1) { TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 250, 204, 21, 255); return; }
    if (cp == 0x1F48E) { // 💎
        int top = y + sz / 6, mid = y + sz * 42 / 100, bot = y + sz * 88 / 100;
        for (int py = top; py <= bot; py++) {
            float t = (py <= mid)
                ? 0.45f + 0.55f * (float)(py - top) / std::max(1, mid - top)
                : 1.0f - (float)(py - mid) / std::max(1, bot - mid);
            int halfw = std::max(1, (int)(t * sz * 0.44f));
            unsigned char cr = (py <= mid) ? 120 : 56;
            unsigned char cg = (py <= mid) ? 235 : 189;
            unsigned char cb = 255;
            TextRenderer::draw_rect_filled(canvas, W, H, cx - halfw, py, halfw * 2, 1, cr, cg, cb, 255);
        }
        return;
    }
    if (cp == 0x1F331) { // 🌱
        TextRenderer::draw_line(canvas, W, H, cx, y + sz * 82 / 100, cx, y + sz * 35 / 100, 46, 204, 113, std::max(2, sz / 10), 255);
        TextRenderer::draw_circle_filled(canvas, W, H, cx - sz / 5, y + sz * 38 / 100, std::max(2, sz / 5), 52, 211, 153, 255);
        TextRenderer::draw_circle_filled(canvas, W, H, cx + sz / 5, y + sz * 30 / 100, std::max(2, sz / 5), 16, 185, 129, 255);
        return;
    }
    if (cp == 0x1F389) { // 🎉
        TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 255, 200, 50, 255);
        TextRenderer::draw_circle_filled(canvas, W, H, cx - r / 2, cy - r / 2, std::max(2, r / 3), 255, 90, 120, 255);
        TextRenderer::draw_circle_filled(canvas, W, H, cx + r / 2, cy - r / 2, std::max(2, r / 3), 80, 200, 255, 255);
        return;
    }
    if (cp == 0x1F91D) { // 🤝
        TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 251, 191, 36, 255);
        TextRenderer::draw_circle_filled(canvas, W, H, cx - r / 4, cy, std::max(2, r / 2), 245, 158, 11, 255);
        return;
    }
    if (cp == 0x1F622) { // 😢
        TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 250, 204, 21, 255);
        TextRenderer::draw_circle_filled(canvas, W, H, cx - r / 3, cy - r / 5, std::max(1, r / 7), 40, 40, 40, 255);
        TextRenderer::draw_circle_filled(canvas, W, H, cx + r / 3, cy - r / 5, std::max(1, r / 7), 40, 40, 40, 255);
        TextRenderer::draw_circle_filled(canvas, W, H, cx + r / 3, cy + r / 4, std::max(1, r / 6), 56, 189, 248, 255);
        return;
    }
    // ✊ ✌️ 🖐️ fallback warm hand circle
    TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, r, 253, 214, 138, 255);
}

static ImageRGBA* load_emoji_png(const std::string& dir, uint32_t cp) {
    {
        std::lock_guard<std::mutex> lk(g_missing_mtx);
        if (g_missing_cp.count(cp)) return nullptr;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%x", cp);
    std::string key = std::string("emoji_") + buf;
    ImageRGBA* img = AssetManager::get(key);
    if (img && img->valid()) return img;

    std::string path = dir + "/" + std::string(buf) + ".png";
    img = AssetManager::load(key, path);
    if (img && img->valid()) return img;

    {
        std::lock_guard<std::mutex> lk(g_missing_mtx);
        g_missing_cp.insert(cp);
    }
    return nullptr;
}

bool EmojiRenderer::has_codepoint(uint32_t cp) {
    if (load_emoji_png(emoji_dir_, cp)) return true;
    if (has_procedural_icon(cp)) return true;
    uint32_t al = alias_cp(cp);
    if (al && load_emoji_png(emoji_dir_, al)) return true;
    return false;
}

bool EmojiRenderer::draw_codepoint(std::vector<uint8_t>& canvas, int W, int H,
                                   int x, int y, int size, uint32_t cp) {
    ImageRGBA* img = load_emoji_png(emoji_dir_, cp);
    if (img) {
        TextRenderer::draw_image_on_canvas(canvas, W, H, img, x, y, size, size);
        return true;
    }
    if (has_procedural_icon(cp)) {
        draw_procedural_icon(canvas, W, H, x, y, size, cp);
        return true;
    }
    uint32_t al = alias_cp(cp);
    if (al) {
        img = load_emoji_png(emoji_dir_, al);
        if (img) {
            TextRenderer::draw_image_on_canvas(canvas, W, H, img, x, y, size, size);
            return true;
        }
    }
    return false;
}

bool EmojiRenderer::draw(std::vector<uint8_t>& canvas, int W, int H,
                         int x, int y, int size,
                         const std::string& emoji_utf8) {
    std::string cp_hex = utf8_to_codepoint(emoji_utf8);
    if (cp_hex.empty()) return false;
    uint32_t cp = (uint32_t)std::strtoul(cp_hex.c_str(), nullptr, 16);
    return draw_codepoint(canvas, W, H, x, y, size, cp);
}
