#pragma once
#include <boost/multiprecision/cpp_int.hpp>
using cpp_int = boost::multiprecision::cpp_int;

#include <vector>
#include <string>
#include <cmath>
#include <random>
#include <utility>
#include <cstdio>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "Color.hpp"
#include "Gradient.hpp"
#include "Shape.hpp"
#include "Utils.hpp"

namespace taixiu_render {

inline std::mt19937& rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}

inline int ri(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng());
}

inline float rf(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng());
}

inline void text_efx(std::vector<uint8_t>& canvas, int W, int H,
                     int cx, int cy, int size,
                     unsigned char r1, unsigned char g1, unsigned char b1,
                     unsigned char r2, unsigned char g2, unsigned char b2,
                     const std::string& text, int outline = 3)
{
    int tw = TextRenderer::measure_text_width(size, text);
    int tx = cx - tw / 2;
    int ty = cy + size / 3;
    for (int dx = -outline; dx <= outline; dx++) {
        for (int dy = -outline; dy <= outline; dy++) {
            if ((dx == 0 && dy == 0) || dx * dx + dy * dy > outline * outline + 1) continue;
            TextRenderer::draw_text_on_canvas(canvas, W, H, tx + dx, ty + dy, size, 0, 0, 0, text);
        }
    }
    TextRenderer::draw_text_gradient(canvas, W, H, tx, ty, size, r1, g1, b1, r2, g2, b2, text);
}

inline void draw_sparkles(std::vector<uint8_t>& canvas, int W, int H, int count) {
    for (int i = 0; i < count; i++) {
        int x = ri(0, W - 1);
        int y = ri(0, H - 1);
        int rr = (int)rf(1.0f, 2.0f);
        unsigned char a = (unsigned char)(rf(0.15f, 0.65f) * 255);
        TextRenderer::draw_circle_filled(canvas, W, H, x, y, rr, 255, 255, 255, a);
    }
}

inline void draw_disc_outline(std::vector<uint8_t>& canvas, int W, int H, int cx, int cy, int radius) {
    TextRenderer::draw_rounded_rect_outline(canvas, W, H, cx - radius, cy - radius,
                                            radius * 2, radius * 2, radius, 3, 255, 255, 255, 150);
}

inline void render_result(
    const std::vector<int>& dice,
    int total,
    const std::string& result,
    const cpp_int& tai_total,
    const cpp_int& xiu_total,
    bool has_jackpot,
    const cpp_int& jackpot_amount,
    const std::string& out_path)
{
    const int W = 820;
    const int H = has_jackpot ? 360 : 300;
    const int cx = W / 2, cy = H / 2;
    const int lc = W / 4 - 20, rc = W * 3 / 4 + 20;
    const int radius = 98;

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 40, 0, 0);

    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            float dx = (float)(px - cx), dy = (float)(py - cy);
            float t = std::min(1.0f, std::sqrt(dx * dx + dy * dy) / (W * 0.55f));
            unsigned char rr, gg, bb;
            if (has_jackpot) {
                rr = (unsigned char)(184 + (101 - 184) * t);
                gg = (unsigned char)(134 + (67 - 134) * t);
                bb = (unsigned char)(11 + (33 - 11) * t);
            } else {
                rr = (unsigned char)(90 + (32 - 90) * t);
                gg = (unsigned char)(10 + (4 - 10) * t);
                bb = (unsigned char)(14 + (10 - 14) * t);
            }
            render::color::set_pixel(canvas, W, H, px, py, render::color::RGBA(rr, gg, bb, 255));
        }
    }

    draw_sparkles(canvas, W, H, has_jackpot ? 90 : 60);

    // Center plate
    TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, radius + 6, 0, 0, 0, 80);
    TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, radius, 255, 255, 255, 48);
    draw_disc_outline(canvas, W, H, cx, cy, radius);

    // 3 dice with bilinear rotation
    const int dsz = 52;
    const float safe_r = radius - dsz * 0.65f;
    const float base_angles[3] = { -1.5708f, 0.5236f, 2.6180f };
    for (int i = 0; i < 3 && i < (int)dice.size(); i++) {
        float ang = base_angles[i] + rf(-0.25f, 0.25f);
        float rr = rf(0.68f, 0.90f);
        int px = cx + (int)(std::cos(ang) * safe_r * rr);
        int py = cy + (int)(std::sin(ang) * safe_r * rr);
        ImageRGBA* im = AssetManager::get("dice_" + std::to_string(dice[i]));
        if (im && im->valid()) {
            TextRenderer::draw_image_rotated(canvas, W, H, im, px, py, dsz, rf(-0.6f, 0.6f));
        }
    }

    bool is_tai = (result == "tai");
    unsigned char tr1 = is_tai ? 255 : 160, tg1 = is_tai ? 225 : 175, tb1 = is_tai ? 65 : 195;
    unsigned char tr2 = is_tai ? 255 : 120, tg2 = is_tai ? 140 : 135, tb2 = is_tai ? 40 : 155;
    unsigned char xr1 = !is_tai ? 56 : 160,  xg1 = !is_tai ? 235 : 175, xb1 = !is_tai ? 255 : 195;
    unsigned char xr2 = !is_tai ? 96 : 120,  xg2 = !is_tai ? 165 : 135, xb2 = !is_tai ? 250 : 155;

    text_efx(canvas, W, H, lc, cy - 26, 44, tr1, tg1, tb1, tr2, tg2, tb2, "TÀI", 3);
    text_efx(canvas, W, H, lc, cy + 26, 28, tr1, tg1, tb1, tr2, tg2, tb2,
             render::utils::format_number(tai_total) + " đ", 2);

    text_efx(canvas, W, H, rc, cy - 26, 44, xr1, xg1, xb1, xr2, xg2, xb2, "XỈU", 3);
    text_efx(canvas, W, H, rc, cy + 26, 28, xr1, xg1, xb1, xr2, xg2, xb2,
             render::utils::format_number(xiu_total) + " đ", 2);

    text_efx(canvas, W, H, cx, cy + radius + 24, 34, 255, 255, 255, 255, 215, 0,
             "Tổng: " + std::to_string(total), 3);

    if (has_jackpot) {
        std::string jtext = "💥 NỔ HŨ: " + render::utils::format_number(jackpot_amount) + " VNĐ";
        text_efx(canvas, W, H, cx, H - 28, 30, 255, 230, 80, 255, 255, 220, jtext, 3);
    }

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

inline void render_waiting(
    int remaining_sec,
    const cpp_int& tai_total,
    const cpp_int& xiu_total,
    const std::string& out_path)
{
    const int W = 820, H = 300;
    const int cx = W / 2, cy = H / 2;
    const int lc = W / 4 - 20, rc = W * 3 / 4 + 20;
    const int radius = 96;

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 42, 0, 0);

    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            float dx = (float)(px - cx), dy = (float)(py - cy);
            float t = std::min(1.0f, std::sqrt(dx * dx + dy * dy) / (W * 0.55f));
            unsigned char rr = (unsigned char)(85 + (32 - 85) * t);
            unsigned char gg = (unsigned char)(12 + (4 - 12) * t);
            unsigned char bb = (unsigned char)(28 + (16 - 28) * t);
            render::color::set_pixel(canvas, W, H, px, py, render::color::RGBA(rr, gg, bb, 255));
        }
    }

    draw_sparkles(canvas, W, H, 50);

    TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, radius, 255, 255, 255, 42);
    draw_disc_outline(canvas, W, H, cx, cy, radius);

    text_efx(canvas, W, H, cx, cy, 56, 255, 235, 100, 255, 150, 50,
             std::to_string(remaining_sec) + "s", 4);

    text_efx(canvas, W, H, lc, cy - 24, 42, 255, 215, 0, 255, 130, 40, "TÀI", 3);
    text_efx(canvas, W, H, lc, cy + 24, 28, 255, 230, 140, 255, 255, 255,
             render::utils::format_number(tai_total) + " đ", 2);

    text_efx(canvas, W, H, rc, cy - 24, 42, 56, 235, 255, 59, 130, 246, "XỈU", 3);
    text_efx(canvas, W, H, rc, cy + 24, 28, 180, 235, 255, 255, 255, 255,
             render::utils::format_number(xiu_total) + " đ", 2);

    text_efx(canvas, W, H, cx, H - 24, 22, 220, 220, 240, 255, 255, 255,
             "Đang nhận cược... Gõ /taixiu tai|xiu <tiền>", 2);

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

} // namespace taixiu_render
