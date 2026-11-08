#pragma once
#include <string>
#include <vector>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "NongTraiManager.hpp"
#include "stb_image_write.h"
#include "stb_truetype.h"

namespace shop_render {
    const int ITEMS_PER_ROW = 4;
    const int ITEM_W = 280;
    const int ITEM_H = 330;
    const int PAD = 20;
    const int IMG_SIZE = 100;
    const int HEADER_H = 60;
    const int SECTION_PAD = 30;

    inline int measure_text(const std::string& text, int size) {
        static stbtt_fontinfo font;
        static bool font_loaded = false;
        if (!font_loaded) {
            FILE* f = fopen("assets/BeVietnamPro-Bold.ttf", "rb");
            if (f) {
                fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
                unsigned char* buf = (unsigned char*)malloc(sz);
                fread(buf, 1, sz, f); fclose(f);
                stbtt_InitFont(&font, buf, 0);
                font_loaded = true;
            }
        }
        if (!font_loaded) return (int)(text.length() * size * 0.5);
        float scale = stbtt_ScaleForPixelHeight(&font, (float)size);
        int x = 0;
        for (size_t i = 0; i < text.length(); ) {
            int cp = 0;
            unsigned char c = text[i];
            if (c < 0x80) { cp = c; i += 1; }
            else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (text[i+1] & 0x3F); i += 2; }
            else if ((c & 0xF0) == 0xE0) { cp = ((c & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F); i += 3; }
            else if ((c & 0xF8) == 0xF0) { cp = ((c & 0x07) << 18) | ((text[i+1] & 0x3F) << 12) | ((text[i+2] & 0x3F) << 6) | (text[i+3] & 0x3F); i += 4; }
            else { i += 1; continue; }
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font, cp, &advance, &lsb);
            x += (int)(advance * scale);
        }
        return x;
    }

    inline void draw_image(std::vector<uint8_t>& cv, int W, int H, const std::string& imgPath, int x, int y, int w, int h) {
        AssetManager::load(imgPath, imgPath);
        ImageRGBA* img = AssetManager::get(imgPath);
        if (img) TextRenderer::draw_image_on_canvas(cv, W, H, img, x, y, w, h);
    }

    inline void draw_header(std::vector<uint8_t>& cv, int W, int H, const std::string& text, int x, int y, int width) {
        TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, width, HEADER_H, 10, 74, 144, 226, 255);
        int tw = measure_text(text, 28);
        TextRenderer::draw_text_on_canvas(cv, W, H, x + (width - tw) / 2, y + 15, 28, 255, 255, 255, text);
    }

    inline void draw_item(std::vector<uint8_t>& cv, int W, int H, const std::string& name, const std::string& code, long long price, const std::string& desc, int x, int y, bool is_seed, int growthTime = 0, long long harvest = 0) {
        // Vẽ khung
        TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, ITEM_W, ITEM_H, 15, 255, 255, 255, 255);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, x, y, ITEM_W, ITEM_H, 15, 1, 225, 228, 232, 255);

        // Load ảnh
        std::string imgPath = "assets/repo/nongtrai/shop/" + code + ".png";
        // Code ảnh đặc biệt: F1->f1, MF1->mf1, SF1->sf1
        std::string imgCode = code;
        std::transform(imgCode.begin(), imgCode.end(), imgCode.begin(), ::tolower);
        imgPath = "assets/repo/nongtrai/shop/" + imgCode + ".png";

        // Với hạt giống, mã ảnh là cropImageCode (1.png, 2.png...)
        if (is_seed) {
            const CropDef* cd = find_crop_by_code(code);
            if (cd) imgPath = "assets/repo/nongtrai/shop/" + cd->crop_image_code + ".png";
        }

        draw_image(cv, W, H, imgPath, x + (ITEM_W - IMG_SIZE) / 2, y + 30, IMG_SIZE, IMG_SIZE);

        int textY = y + 150;
        int cx = x + ITEM_W / 2;

        // Tên
        int tw = measure_text(name, 22);
        TextRenderer::draw_text_on_canvas(cv, W, H, cx - tw/2, textY, 22, 44, 62, 80, name);
        textY += 35;

        // Mã
        std::string codeStr = "Ma: " + code;
        tw = measure_text(codeStr, 24);
        TextRenderer::draw_text_on_canvas(cv, W, H, cx - tw/2, textY, 24, 44, 62, 80, codeStr);
        textY += 35;

        // Giá
        std::string priceStr = "💰 " + std::to_string(price) + "d";
        tw = measure_text(priceStr, 20);
        TextRenderer::draw_text_on_canvas(cv, W, H, cx - tw/2, textY, 20, 231, 76, 60, priceStr);
        textY += 35;

        if (is_seed) {
            std::string timeStr = "⏱️ " + std::to_string(growthTime / 60) + " phut";
            tw = measure_text(timeStr, 20);
            TextRenderer::draw_text_on_canvas(cv, W, H, cx - tw/2, textY, 20, 52, 152, 219, timeStr);
            textY += 30;
            std::string harvStr = "💎 " + std::to_string(harvest) + "d";
            tw = measure_text(harvStr, 20);
            TextRenderer::draw_text_on_canvas(cv, W, H, cx - tw/2, textY, 20, 39, 174, 96, harvStr);
        } else {
            // Mô tả (đơn giản cắt theo từ)
            std::string d = desc;
            if (d.length() > 30) d = d.substr(0, 27) + "...";
            tw = measure_text(d, 16);
            TextRenderer::draw_text_on_canvas(cv, W, H, cx - tw/2, textY, 16, 127, 140, 141, d);
        }
    }

    inline void render_shop(long long land_price, const std::string& out_path) {
        auto tools = get_shop_items();
        auto seeds = get_crops();

        int toolRows = (tools.size() + ITEMS_PER_ROW - 1) / ITEMS_PER_ROW;
        int seedRows = (seeds.size() + ITEMS_PER_ROW - 1) / ITEMS_PER_ROW;

        int W = (ITEM_W + PAD) * ITEMS_PER_ROW + PAD;
        int H = (ITEM_H + PAD) * (toolRows + seedRows) + PAD * 3 + HEADER_H * 2 + SECTION_PAD;

        std::vector<uint8_t> cv;
        TextRenderer::init_canvas(cv, W, H, 246, 247, 249);

        // Vẽ khu vực dụng cụ
        int currentY = PAD;
        draw_header(cv, W, H, "🛠️ KHU VỰC DỤNG CỤ 🛠️", PAD, currentY, W - PAD * 2);
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

        // Vẽ khu vực hạt giống
        currentY += toolRows * (ITEM_H + PAD) + SECTION_PAD;
        draw_header(cv, W, H, "🌱 KHU VỰC HẠT GIỐNG 🌱", PAD, currentY, W - PAD * 2);
        currentY += HEADER_H + PAD;

        i = 0;
        for (auto& kv : seeds) {
            int row = i / ITEMS_PER_ROW;
            int col = i % ITEMS_PER_ROW;
            int x = col * (ITEM_W + PAD) + PAD;
            int y = currentY + row * (ITEM_H + PAD);
            draw_item(cv, W, H, kv.second.name, kv.second.code, kv.second.seed_price, "", x, y, true, kv.second.growth_time_sec, kv.second.harvest_value);
            i++;
        }

        stbi_write_png(out_path.c_str(), W, H, 4, cv.data(), W * 4);
    }
}
