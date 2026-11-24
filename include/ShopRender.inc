#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "NongTraiManager.hpp"
#include "Gradient.hpp"
#include "Color.hpp"
#include "stb_image_write.h"

namespace shop_render {

    const int ITEMS_PER_ROW = 4;
    const int ITEM_W = 280;
    const int ITEM_H = 340;
    const int PAD = 22;
    const int IMG_SIZE = 104;
    const int HEADER_H = 62;
    const int SECTION_PAD = 26;

    inline int measure_text(const std::string& text, int size) {
        return TextRenderer::measure_text_width(size, text);
    }

    inline std::string fmt_vnd(long long v) {
        bool neg = v < 0;
        unsigned long long u = neg ? (unsigned long long)(-v) : (unsigned long long)v;
        std::string s = std::to_string(u);
        for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ".");
        return (neg ? "-" : "") + s + " đ";
    }

    inline void draw_procedural_tool_icon(std::vector<uint8_t>& cv, int W, int H,
                                          const std::string& code, int x, int y, int sz) {
        int cx = x + sz / 2;
        int cy = y + sz / 2;
        if (code == "W1") {
            // Watering can
            TextRenderer::draw_rounded_rect_filled(cv, W, H, cx - 26, cy - 8, 46, 36, 8, 56, 189, 248, 255);
            TextRenderer::draw_rounded_rect_outline(cv, W, H, cx - 26, cy - 8, 46, 36, 8, 2, 14, 116, 144, 255);
            TextRenderer::draw_rounded_rect_outline(cv, W, H, cx - 20, cy - 26, 34, 22, 8, 4, 14, 116, 144, 255);
            TextRenderer::draw_line(cv, W, H, cx + 20, cy + 6, cx + 40, cy - 16, 56, 189, 248, 7, 255);
            TextRenderer::draw_circle_filled(cv, W, H, cx + 42, cy - 18, 6, 14, 116, 144, 255);
        } else if (code == "S1") {
            // Shovel
            TextRenderer::draw_line(cv, W, H, cx - 26, cy + 28, cx + 18, cy - 16, 146, 64, 14, 7, 255);
            TextRenderer::draw_rounded_rect_filled(cv, W, H, cx + 8, cy - 32, 26, 26, 6, 148, 163, 184, 255);
            TextRenderer::draw_rounded_rect_outline(cv, W, H, cx + 8, cy - 32, 26, 26, 6, 2, 71, 85, 105, 255);
            TextRenderer::draw_circle_filled(cv, W, H, cx - 28, cy + 30, 8, 180, 83, 9, 255);
        } else {
            // Land plot (L0)
            TextRenderer::draw_rounded_rect_filled(cv, W, H, cx - 38, cy - 28, 76, 56, 10, 74, 222, 128, 255);
            TextRenderer::draw_rounded_rect_filled(cv, W, H, cx - 30, cy - 20, 60, 40, 6, 146, 94, 52, 255);
            TextRenderer::draw_line(cv, W, H, cx - 24, cy - 6, cx + 24, cy - 6, 101, 63, 33, 2, 220);
            TextRenderer::draw_line(cv, W, H, cx - 24, cy + 8, cx + 24, cy + 8, 101, 63, 33, 2, 220);
        }
    }

    inline void draw_header(std::vector<uint8_t>& cv, int W, int H, const std::string& text,
                            int x, int y, int width, const char* hex1, const char* hex2) {
        TextRenderer::draw_rounded_rect_shadow(cv, W, H, x, y, width, HEADER_H, 14, 8, 55);
        render::color::RGBA c1 = render::color::from_hex(hex1);
        render::color::RGBA c2 = render::color::from_hex(hex2);
        // Gradient header pill
        TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, width, HEADER_H, 14, c1.r, c1.g, c1.b, 255);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, x, y, width, HEADER_H, 14, 2, c2.r, c2.g, c2.b, 220);
        int tw = measure_text(text, 28);
        TextRenderer::draw_text_shadow(cv, W, H, x + (width - tw) / 2, y + HEADER_H / 2 + 10, 28,
                                       255, 255, 255, text);
    }

    inline void draw_item(std::vector<uint8_t>& cv, int W, int H,
                          const std::string& name, const std::string& code,
                          long long price, const std::string& desc,
                          int x, int y, bool is_seed, int growthTime = 0, long long harvest = 0) {
        TextRenderer::draw_rounded_rect_shadow(cv, W, H, x, y, ITEM_W, ITEM_H, 18, 10, 45);
        TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, ITEM_W, ITEM_H, 18, 255, 255, 255, 255);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, x, y, ITEM_W, ITEM_H, 18, 2, 203, 213, 225, 255);

        // Top code badge
        TextRenderer::draw_rounded_rect_filled(cv, W, H, x + ITEM_W - 68, y + 12, 54, 26, 10, 239, 246, 255, 255);
        int cw = measure_text(code, 16);
        TextRenderer::draw_text_on_canvas(cv, W, H, x + ITEM_W - 41 - cw / 2, y + 30, 16, 37, 99, 235, code);

        std::string imgCode = code;
        std::transform(imgCode.begin(), imgCode.end(), imgCode.begin(), ::tolower);
        std::string imgPath = "assets/repo/nongtrai/shop/" + imgCode + ".png";
        if (is_seed) {
            const CropDef* cd = find_crop_by_code(code);
            if (cd) imgPath = "assets/repo/nongtrai/shop/" + cd->crop_image_code + ".png";
        }
        AssetManager::load(imgPath, imgPath);
        ImageRGBA* img = AssetManager::get(imgPath);
        if (img && img->valid()) {
            TextRenderer::draw_image_aspect_fit(cv, W, H, img, x + (ITEM_W - IMG_SIZE) / 2, y + 26, IMG_SIZE, IMG_SIZE);
        } else {
            draw_procedural_tool_icon(cv, W, H, code, x + (ITEM_W - IMG_SIZE) / 2, y + 26, IMG_SIZE);
        }

        int cx = x + ITEM_W / 2;
        int textY = y + 158;

        int nw = measure_text(name, 22);
        TextRenderer::draw_text_on_canvas(cv, W, H, cx - nw / 2, textY, 22, 30, 41, 59, name);
        textY += 32;

        std::string priceStr = "💰 " + fmt_vnd(price);
        int pw = measure_text(priceStr, 20);
        TextRenderer::draw_text_on_canvas(cv, W, H, cx - pw / 2, textY, 20, 220, 38, 38, priceStr);
        textY += 32;

        if (is_seed) {
            std::string timeStr = "⏱️ " + std::to_string(growthTime / 60) + " phút";
            int tmw = measure_text(timeStr, 19);
            TextRenderer::draw_text_on_canvas(cv, W, H, cx - tmw / 2, textY, 19, 37, 99, 235, timeStr);
            textY += 30;

            std::string harvStr = "💎 Thu: " + fmt_vnd(harvest);
            int hw = measure_text(harvStr, 19);
            TextRenderer::draw_text_on_canvas(cv, W, H, cx - hw / 2, textY, 19, 22, 163, 74, harvStr);
        } else {
            std::vector<std::string> lines = TextRenderer::wrap_text(17, desc, ITEM_W - 32);
            for (size_t li = 0; li < lines.size() && li < 3; li++) {
                int lw = measure_text(lines[li], 17);
                TextRenderer::draw_text_on_canvas(cv, W, H, cx - lw / 2, textY + (int)li * 24, 17, 100, 116, 139, lines[li]);
            }
        }
    }

    inline void render_shop(long long land_price, const std::string& out_path) {
        auto tools = get_shop_items();
        auto seeds = get_crops();

        int toolRows = ((int)tools.size() + ITEMS_PER_ROW - 1) / ITEMS_PER_ROW;
        int seedRows = ((int)seeds.size() + ITEMS_PER_ROW - 1) / ITEMS_PER_ROW;

        int W = (ITEM_W + PAD) * ITEMS_PER_ROW + PAD;
        int H = (ITEM_H + PAD) * (toolRows + seedRows) + PAD * 3 + HEADER_H * 2 + SECTION_PAD;

        std::vector<uint8_t> cv;
        TextRenderer::init_canvas(cv, W, H, 241, 245, 249);

        int currentY = PAD;
        draw_header(cv, W, H, "🛠️ KHU VỰC DỤNG CỤ 🛠️", PAD, currentY, W - PAD * 2, "#2563EB", "#60A5FA");
        currentY += HEADER_H + PAD;

        int i = 0;
        for (auto& kv : tools) {
            int row = i / ITEMS_PER_ROW;
            int col = i % ITEMS_PER_ROW;
            int x = col * (ITEM_W + PAD) + PAD;
            int y = currentY + row * (ITEM_H + PAD);
            long long price = (kv.second.key == "LAND") ? land_price : kv.second.price;
            draw_item(cv, W, H, kv.second.name, kv.second.code, price, kv.second.description, x, y, false);
            i++;
        }

        currentY += toolRows * (ITEM_H + PAD) + SECTION_PAD;
        draw_header(cv, W, H, "🌱 KHU VỰC HẠT GIỐNG 🌱", PAD, currentY, W - PAD * 2, "#059669", "#34D399");
        currentY += HEADER_H + PAD;

        i = 0;
        for (auto& kv : seeds) {
            int row = i / ITEMS_PER_ROW;
            int col = i % ITEMS_PER_ROW;
            int x = col * (ITEM_W + PAD) + PAD;
            int y = currentY + row * (ITEM_H + PAD);
            draw_item(cv, W, H, kv.second.name, kv.second.code, kv.second.seed_price, "", x, y,
                      true, kv.second.growth_time_sec, kv.second.harvest_value);
            i++;
        }

        TextRenderer::save_canvas(cv, W, H, out_path);
    }
}
