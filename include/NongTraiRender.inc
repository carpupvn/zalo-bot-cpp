#pragma once
#include <string>
#include <vector>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <random>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "NongTraiManager.hpp"
#include "stb_image_write.h"
#include "stb_truetype.h"

namespace nong_trai_render {
    const int PLOT_SIZE = 80;
    const int GRASS_HEIGHT = 20;
    const int MAX_PLOTS_PER_ROW = 12;

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

    inline void render_farm(const Farm& farm, const std::string& out_path) {
        int plotsCount = farm.plots.size();
        if (plotsCount == 0) return;

        int cols = std::min(plotsCount, MAX_PLOTS_PER_ROW);
        int rows = (plotsCount + MAX_PLOTS_PER_ROW - 1) / MAX_PLOTS_PER_ROW;
        int grassRows = rows * 2 + 1;
        int W = cols * PLOT_SIZE + 2 * GRASS_HEIGHT;
        int H = rows * PLOT_SIZE + grassRows * GRASS_HEIGHT;

        std::vector<uint8_t> cv;
        TextRenderer::init_canvas(cv, W, H, 144, 238, 144); // Nền cỏ xanh

        // Vẽ hoa tuyết (chấm trắng ngẫu nhiên)
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> distX(0, W), distY(0, H);
        int flowerCount = (W * H) / 5000;
        for (int i = 0; i < flowerCount; i++) {
            int fx = distX(rng), fy = distY(rng);
            TextRenderer::draw_circle_filled(cv, W, H, fx, fy, 1, 255, 255, 255, 255);
            for (int a = 0; a < 6; a++) {
                double angle = a * M_PI / 3.0;
                int ox = fx + (int)(cos(angle) * 2), oy = fy + (int)(sin(angle) * 2);
                TextRenderer::draw_circle_filled(cv, W, H, ox, oy, 1, 255, 255, 255, 255);
            }
        }

        long long now = std::time(nullptr) * 1000; // Giả sử dùng ms

        for (int i = 0; i < plotsCount; i++) {
            int row = i / MAX_PLOTS_PER_ROW;
            int col = i % MAX_PLOTS_PER_ROW;
            int x = col * PLOT_SIZE + GRASS_HEIGHT;
            int y = row * PLOT_SIZE + (row * 2 + 1) * GRASS_HEIGHT - 6;

            const Plot& plot = farm.plots[i];
            bool hasCrop = !plot.crop_key.empty();

            // Vẽ đất nếu không có cây
            if (!hasCrop) {
                draw_image(cv, W, H, "assets/repo/nongtrai/dat/dat_tot.png", x, y, PLOT_SIZE, PLOT_SIZE);
            }

            if (hasCrop) {
                const CropDef* cd = find_crop_by_key(plot.crop_key);
                if (cd) {
                    long long totalMs = (long long)cd->growth_time_sec * 1000;
                    long long passedMs = std::time(nullptr) * 1000 - plot.planted_at;
                    double progress = (double)passedMs / totalMs;
                    if (progress > 1.0) progress = 1.0;
                    if (progress < 0.0) progress = 0.0;

                    std::string stage = "seed";
                    if (progress >= 1.0) stage = "ripe";
                    else if (progress >= 0.66) stage = "growing";
                    else if (progress >= 0.33) stage = "young";

                    // Vẽ thanh tiến trình
                    int barW = (int)(PLOT_SIZE * 0.8);
                    int barH = 5;
                    int barX = x + (PLOT_SIZE - barW) / 2;
                    int barY = y + 5;

                    TextRenderer::draw_rect_filled(cv, W, H, barX, barY, barW, barH, 0, 0, 0, 255);
                    int fillW = (int)(barW * progress);
                    if (fillW > 0) TextRenderer::draw_rect_filled(cv, W, H, barX, barY, fillW, barH, 50, 255, 50, 255); // Màu xanh lá thay gradient

                    // Vẽ ảnh cây
                    std::string imgPath = "assets/repo/nongtrai/caytrong/";
                    if (stage == "seed") imgPath += "gieohat.png";
                    else if (stage == "young") imgPath += cd->crop_image_code + ".png";
                    else if (stage == "growing") imgPath += cd->crop_image_code + "-uong.png";
                    else if (stage == "ripe") imgPath += cd->crop_image_code + "-chin.png";

                    draw_image(cv, W, H, imgPath, x, y, PLOT_SIZE, PLOT_SIZE);
                }
            }

            // Vẽ thanh độ ẩm
            int wBarW = (int)(PLOT_SIZE * 0.8);
            int wBarH = 5;
            int wBarX = x + (PLOT_SIZE - wBarW) / 2;
            int wBarY = y + PLOT_SIZE - wBarH - 5 + 20;

            TextRenderer::draw_rect_filled(cv, W, H, wBarX, wBarY, wBarW, wBarH, 0, 0, 0, 255);
            int wFill = (int)(wBarW * (plot.water_level / 100.0));
            if (wFill > 0) TextRenderer::draw_rect_filled(cv, W, H, wBarX, wBarY, wFill, wBarH, 30, 144, 255, 255);

            // Vẽ giọt nước (dùng hình tròn)
            TextRenderer::draw_circle_filled(cv, W, H, wBarX - 10, wBarY + 2, 4, 30, 144, 255, 255);

            // Vẽ số thứ tự
            std::string idxStr = "#" + std::to_string(i + 1);
            int txtW = measure_text(idxStr, 16);
            TextRenderer::draw_text_on_canvas(cv, W, H, x + (PLOT_SIZE - txtW) / 2, wBarY + 20, 16, 0, 0, 0, idxStr);
        }

        stbi_write_png(out_path.c_str(), W, H, 4, cv.data(), W * 4);
    }

    inline void render_farm_bg(const std::string& out_path) {
        std::vector<std::string> buildings = {"nhakho.png", "farm.png", "bxh.png", "cuahangfarm.png", "atm.png"};
        std::vector<ImageRGBA*> imgs;
        int totalW = 0, maxH = 0;

        for (auto& b : buildings) {
            std::string path = "assets/repo/nongtrai/zone/" + b;
            AssetManager::load(path, path);
            ImageRGBA* img = AssetManager::get(path);
            if (img) { imgs.push_back(img); totalW += img->width; if (img->height > maxH) maxH = img->height; }
        }

        if (imgs.empty()) return;

        int padding = 40, spacing = 20;
        int W = totalW + padding * 2 + spacing * (imgs.size() - 1);
        int H = maxH * 2;

        std::vector<uint8_t> cv;
        TextRenderer::init_canvas(cv, W, H, 0, 0, 0);

        int skyH = (int)(H * 0.6);
        TextRenderer::draw_rect_filled(cv, W, H, 0, 0, W, skyH, 135, 206, 235, 255); // Bầu trời
        TextRenderer::draw_rect_filled(cv, W, H, 0, skyH, W, H - skyH, 144, 238, 144, 255); // Cỏ

        int roadH = (int)((H - skyH) * 0.3);
        int roadY = skyH + (H - skyH - roadH) / 2;
        TextRenderer::draw_rect_filled(cv, W, H, 0, roadY, W, roadH, 222, 184, 135, 255); // Đường đất

        int curX = padding;
        for (auto img : imgs) {
            int y = skyH - 10 - img->height + 16;
            TextRenderer::draw_image_on_canvas(cv, W, H, img, curX, y, img->width, img->height);
            curX += img->width + spacing;
        }

        stbi_write_png(out_path.c_str(), W, H, 4, cv.data(), W * 4);
    }
}
