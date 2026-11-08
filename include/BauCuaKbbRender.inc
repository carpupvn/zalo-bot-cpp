#pragma once
#include <string>
#include <vector>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "stb_image_write.h"
#include "stb_truetype.h"

namespace game_render {
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

    inline void draw_bg(std::vector<uint8_t>& cv, int W, int H) {
        for (int i = 0; i < H; i++) {
            double t = (double)i / H;
            uint8_t r = (uint8_t)(20 + (40 - 20) * t);
            uint8_t g = (uint8_t)(20 + (30 - 20) * t);
            uint8_t b = (uint8_t)(30 + (60 - 30) * t);
            TextRenderer::draw_rect_filled(cv, W, H, 0, i, W, 1, r, g, b, 255);
        }
    }

        inline void render_baucua_result(const std::vector<int>& dices, long long total_win, const std::string& out_path) {
        (void)total_win;
        const int IMG_SIZE = 400;
        int W = IMG_SIZE * 3, H = IMG_SIZE;
        std::vector<uint8_t> cv;
        TextRenderer::init_canvas(cv, W, H, 240, 240, 240); // Nền #f0f0f0

        const char* imgNames[] = {"bau", "cua", "tom", "ca", "ga", "nai"};
        for (int i = 0; i < 3; i++) {
            std::string imgPath = "assets/repo/baucua/" + std::string(imgNames[dices[i]]) + ".png";
            AssetManager::load(imgPath, imgPath);
            ImageRGBA* img = AssetManager::get(imgPath);
            if (img) {
                TextRenderer::draw_image_on_canvas(cv, W, H, img, i * IMG_SIZE, 0, IMG_SIZE, IMG_SIZE);
            }
        }

        stbi_write_png(out_path.c_str(), W, H, 4, cv.data(), W * 4);
    }

    inline void render_kbb_result(int player_choice, int bot_choice, int result, const std::string& out_path) {
        int W = 900, H = 500;
        std::vector<uint8_t> cv;
        TextRenderer::init_canvas(cv, W, H, 0, 0, 0);
        draw_bg(cv, W, H);

        std::string title = "✊ KÉO BÚA BAO ✌️";
        int tw = measure_text(title, 45);
        TextRenderer::draw_text_gradient_random(cv, W, H, (W - tw) / 2, 40, 45, title);

        const char* kbbImg[] = {"keo", "bua", "bao"};

        TextRenderer::draw_rounded_rect_filled(cv, W, H, 80, 130, 300, 250, 20, 40, 40, 50, 255);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, 80, 130, 300, 250, 20, 2, 100, 150, 255, 255);
        TextRenderer::draw_text_on_canvas(cv, W, H, 80 + 150 - measure_text("BẠN", 30)/2, 150, 30, 100, 200, 255, "BẠN");

        std::string pImgPath = "assets/repo/keobuabao/" + std::string(kbbImg[player_choice]) + "-left.png";
        AssetManager::load(pImgPath, pImgPath);
        ImageRGBA* pImg = AssetManager::get(pImgPath);
        if (pImg) TextRenderer::draw_image_on_canvas(cv, W, H, pImg, 100, 200, 260, 150);

        TextRenderer::draw_rounded_rect_filled(cv, W, H, 520, 130, 300, 250, 20, 40, 40, 50, 255);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, 520, 130, 300, 250, 20, 2, 255, 100, 100, 255);
        TextRenderer::draw_text_on_canvas(cv, W, H, 520 + 150 - measure_text("BOT", 30)/2, 150, 30, 255, 100, 100, "BOT");

        std::string bImgPath = "assets/repo/keobuabao/" + std::string(kbbImg[bot_choice]) + "-right.png";
        AssetManager::load(bImgPath, bImgPath);
        ImageRGBA* bImg = AssetManager::get(bImgPath);
        if (bImg) TextRenderer::draw_image_on_canvas(cv, W, H, bImg, 540, 200, 260, 150);

        TextRenderer::draw_text_on_canvas(cv, W, H, 450 - measure_text("VS", 50)/2, 230, 50, 255, 200, 50, "VS");

        std::string resText = (result == 0) ? "🎉 BẠN THẮNG! 🎉" : (result == 1 ? "🤝 HÒA! 🤝" : "😢 BẠN THUA! 😢");
        uint8_t r = (result == 0) ? 50 : (result == 1 ? 200 : 255);
        uint8_t g = (result == 0) ? 255 : (result == 1 ? 200 : 50);
        uint8_t b = (result == 0) ? 50 : (result == 1 ? 50 : 50);
        
        int rtw = measure_text(resText, 40);
        TextRenderer::draw_text_on_canvas(cv, W, H, (W - rtw) / 2, 420, 40, r, g, b, resText);

        stbi_write_png(out_path.c_str(), W, H, 4, cv.data(), W * 4);
    }
}
