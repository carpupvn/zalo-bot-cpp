#pragma once
#include <string>
#include <vector>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <random>
#include <chrono>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "NongTraiManager.hpp"
#include "stb_image_write.h"

namespace nong_trai_render {

    const int PLOT_SIZE = 104;
    const int PLOT_GAP = 18;
    const int PAD_X = 28;
    const int HEADER_H = 66;
    const int MAX_PLOTS_PER_ROW = 8;

    inline int measure_text(const std::string& text, int size) {
        return TextRenderer::measure_text_width(size, text);
    }

    inline void draw_image(std::vector<uint8_t>& cv, int W, int H, const std::string& imgPath, int x, int y, int w, int h) {
        AssetManager::load(imgPath, imgPath);
        ImageRGBA* img = AssetManager::get(imgPath);
        if (img && img->valid()) TextRenderer::draw_image_aspect_fit(cv, W, H, img, x, y, w, h);
    }

    inline void render_farm(const Farm& farm, const std::string& out_path) {
        int plotsCount = (int)farm.plots.size();
        if (plotsCount == 0) return;

        int cols = std::min(plotsCount, MAX_PLOTS_PER_ROW);
        if (cols < 4) cols = 4;
        int rows = (plotsCount + MAX_PLOTS_PER_ROW - 1) / MAX_PLOTS_PER_ROW;
        const int cell_h = PLOT_SIZE + 54;
        int W = std::max(540, cols * PLOT_SIZE + (cols - 1) * PLOT_GAP + PAD_X * 2);
        int H = HEADER_H + 24 + rows * cell_h + 24;

        std::vector<uint8_t> cv;
        TextRenderer::init_canvas(cv, W, H, 134, 239, 172);
        TextRenderer::fill_linear_gradient(cv, W, H, 0, H, 167, 243, 208, 110, 231, 183);

        // Decorative grass speckles
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> distX(10, W - 10), distY(HEADER_H + 10, H - 10);
        for (int i = 0; i < (W * H) / 4500; i++) {
            TextRenderer::draw_circle_filled(cv, W, H, distX(rng), distY(rng), 2, 255, 255, 255, 160);
        }

        // Top header banner
        long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int ready_cnt = 0, planted_cnt = 0;
        for (const auto& p : farm.plots) {
            if (!p.crop_key.empty()) {
                planted_cnt++;
                const CropDef* cd = find_crop_by_key(p.crop_key);
                if (cd && (now_ms - p.planted_at) >= (long long)cd->growth_time_sec * 1000) ready_cnt++;
            }
        }
        TextRenderer::draw_rounded_rect_filled(cv, W, H, 18, 14, W - 36, 48, 14, 20, 83, 45, 235);
        std::string hdr = "🌾 NÔNG TRẠI  |  " + std::to_string(plotsCount) + " Ô Đất  |  Đang trồng: "
                        + std::to_string(planted_cnt) + "  |  Thu hoạch: " + std::to_string(ready_cnt);
        int hw = measure_text(hdr, 20);
        TextRenderer::draw_text_on_canvas(cv, W, H, (W - hw) / 2, 45, 20, 255, 255, 255, hdr);

        int grid_w = cols * PLOT_SIZE + (cols - 1) * PLOT_GAP;
        int start_x = (W - grid_w) / 2;

        for (int i = 0; i < plotsCount; i++) {
            int row = i / MAX_PLOTS_PER_ROW;
            int col = i % MAX_PLOTS_PER_ROW;
            int x = start_x + col * (PLOT_SIZE + PLOT_GAP);
            int y = HEADER_H + 18 + row * cell_h;

            const Plot& plot = farm.plots[i];
            bool hasCrop = !plot.crop_key.empty();

            // Base soil plot (always drawn!)
            TextRenderer::draw_rounded_rect_shadow(cv, W, H, x, y, PLOT_SIZE, PLOT_SIZE, 14, 8, 60);
            if (plot.status == "dry") {
                TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, PLOT_SIZE, PLOT_SIZE, 14, 168, 130, 92, 255);
                TextRenderer::draw_line(cv, W, H, x + 22, y + 32, x + 52, y + 66, 120, 82, 48, 2, 200);
                TextRenderer::draw_line(cv, W, H, x + 68, y + 26, x + 44, y + 74, 120, 82, 48, 2, 200);
                TextRenderer::draw_rounded_rect_outline(cv, W, H, x, y, PLOT_SIZE, PLOT_SIZE, 14, 2, 180, 83, 9, 255);
            } else if (plot.status == "low") {
                TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, PLOT_SIZE, PLOT_SIZE, 14, 146, 102, 62, 255);
                TextRenderer::draw_rounded_rect_outline(cv, W, H, x, y, PLOT_SIZE, PLOT_SIZE, 14, 2, 217, 119, 6, 220);
            } else {
                draw_image(cv, W, H, "assets/repo/nongtrai/dat/dat_tot.png", x, y, PLOT_SIZE, PLOT_SIZE);
                TextRenderer::draw_rounded_rect_outline(cv, W, H, x, y, PLOT_SIZE, PLOT_SIZE, 14, 2, 22, 101, 52, 180);
            }

            if (hasCrop) {
                const CropDef* cd = find_crop_by_key(plot.crop_key);
                if (cd) {
                    long long totalMs = (long long)cd->growth_time_sec * 1000;
                    long long passedMs = now_ms - plot.planted_at;
                    double progress = std::clamp((double)passedMs / std::max(1LL, totalMs), 0.0, 1.0);

                    std::string stage = "seed";
                    if (progress >= 1.0) stage = "ripe";
                    else if (progress >= 0.66) stage = "growing";
                    else if (progress >= 0.33) stage = "young";

                    std::string imgPath = "assets/repo/nongtrai/caytrong/";
                    if (stage == "seed") imgPath += "gieohat.png";
                    else if (stage == "young") imgPath += cd->crop_image_code + ".png";
                    else if (stage == "growing") imgPath += cd->crop_image_code + "-uong.png";
                    else imgPath += cd->crop_image_code + "-chin.png";

                    draw_image(cv, W, H, imgPath, x + 10, y + 14, PLOT_SIZE - 20, PLOT_SIZE - 22);

                    // Growth progress bar at top of plot
                    int barW = PLOT_SIZE - 16;
                    int barH = 7;
                    int barX = x + 8;
                    int barY = y + 6;
                    TextRenderer::draw_rounded_rect_filled(cv, W, H, barX, barY, barW, barH, 3, 15, 23, 42, 210);
                    int fillW = (int)(barW * progress);
                    if (fillW > 0) {
                        fillW = std::max(fillW, barH);
                        unsigned char cr = (progress >= 1.0) ? 250 : 34;
                        unsigned char cg = (progress >= 1.0) ? 204 : 197;
                        unsigned char cb = (progress >= 1.0) ? 21  : 94;
                        TextRenderer::draw_rounded_rect_filled(cv, W, H, barX, barY, fillW, barH, 3, cr, cg, cb, 255);
                    }
                }
            }

            // Water bar below plot
            int wBarW = PLOT_SIZE - 12;
            int wBarH = 7;
            int wBarX = x + 6;
            int wBarY = y + PLOT_SIZE + 8;
            TextRenderer::draw_rounded_rect_filled(cv, W, H, wBarX, wBarY, wBarW, wBarH, 3, 30, 41, 59, 210);
            int wFill = (int)(wBarW * std::clamp(plot.water_level, 0, 100) / 100.0);
            if (wFill > 0) {
                wFill = std::max(wFill, wBarH);
                unsigned char wr = (plot.water_level <= 40) ? 245 : 56;
                unsigned char wg = (plot.water_level <= 40) ? 158 : 189;
                unsigned char wb = (plot.water_level <= 40) ? 11  : 248;
                TextRenderer::draw_rounded_rect_filled(cv, W, H, wBarX, wBarY, wFill, wBarH, 3, wr, wg, wb, 255);
            }

            // Plot number + moisture %
            std::string idxStr = "#" + std::to_string(i + 1) + " (" + std::to_string(plot.water_level) + "%)";
            int txtW = measure_text(idxStr, 16);
            TextRenderer::draw_text_on_canvas(cv, W, H, x + (PLOT_SIZE - txtW) / 2, wBarY + 26, 16, 20, 40, 28, idxStr);
        }

        TextRenderer::save_canvas(cv, W, H, out_path);
    }

    inline void render_farm_bg(const std::string& out_path) {
        std::vector<std::string> buildings = {"nhakho.png", "farm.png", "bxh.png", "cuahangfarm.png", "atm.png"};
        std::vector<ImageRGBA*> imgs;
        int totalW = 0, maxH = 0;
        for (auto& b : buildings) {
            std::string path = "assets/repo/nongtrai/zone/" + b;
            AssetManager::load(path, path);
            ImageRGBA* img = AssetManager::get(path);
            if (img && img->valid()) { imgs.push_back(img); totalW += img->width; if (img->height > maxH) maxH = img->height; }
        }
        if (imgs.empty()) return;
        int padding = 40, spacing = 20;
        int W = totalW + padding * 2 + spacing * ((int)imgs.size() - 1);
        int H = maxH * 2;
        std::vector<uint8_t> cv;
        TextRenderer::init_canvas(cv, W, H, 135, 206, 235);
        int skyH = (int)(H * 0.6);
        TextRenderer::fill_linear_gradient(cv, W, H, 0, skyH, 56, 189, 248, 186, 230, 253);
        TextRenderer::draw_rect_filled(cv, W, H, 0, skyH, W, H - skyH, 134, 239, 172, 255);
        int roadH = (int)((H - skyH) * 0.3);
        int roadY = skyH + (H - skyH - roadH) / 2;
        TextRenderer::draw_rect_filled(cv, W, H, 0, roadY, W, roadH, 217, 179, 130, 255);
        int curX = padding;
        for (auto img : imgs) {
            int y = skyH - 10 - img->height + 16;
            TextRenderer::draw_image_on_canvas(cv, W, H, img, curX, y, img->width, img->height);
            curX += img->width + spacing;
        }
        TextRenderer::save_canvas(cv, W, H, out_path);
    }
}
