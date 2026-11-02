// GameRender.inc - Render anh game (port tu bot JS cv-chan-le.js)
// Include file nay sau cac include TextRenderer/AssetManager/Gradient/...
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

namespace game_render {

inline std::mt19937& rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}

inline int rand_int(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng());
}

inline float rand_float(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng());
}

// ============================================================
// render_chanle_image - Port tu cv-chan-le.js
// ============================================================
inline void render_chanle_image(
    const std::vector<int>& dice,
    int total,
    const std::string& player_choice,
    cpp_int bet,
    bool is_jackpot,
    const std::vector<bool>& history,
    cpp_int winnings,
    const std::string& out_path)
{
    const int W = 800;
    const int H = 340;

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 0, 0, 0);

    // 1. Radial gradient background
    {
        render::gradient::RadialGradient g(W/2.0f, H/2.0f, W/2.0f);
        if (is_jackpot) {
            g.add_stop(0.0f, render::color::from_hex("#FFD700"));
            g.add_stop(0.7f, render::color::from_hex("#DAA520"));
            g.add_stop(1.0f, render::color::from_hex("#B8860B"));
        } else {
            g.add_stop(0.0f, render::color::from_hex("#000066"));
            g.add_stop(0.7f, render::color::from_hex("#000033"));
            g.add_stop(1.0f, render::color::from_hex("#000022"));
        }
        g.fill(canvas, W, H);
    }

    // 2. Sparkles (dom sang lap lanh)
    {
        int cnt = is_jackpot ? 100 : 60;
        for (int i = 0; i < cnt; i++) {
            int x = rand_int(0, W - 1);
            int y = rand_int(0, H - 1);
            unsigned char a = (unsigned char)(rand_float(0.3f, is_jackpot ? 0.9f : 0.7f) * 255);
            render::color::blend_pixel(canvas, W, H, x, y,
                render::color::RGBA(255, 255, 255, a));
        }
    }

    // 3. Square box ben trai
    int sq_size = 220;
    int sq_x = (int)(W * 0.23f) - sq_size / 2;
    int sq_y = H / 2 - sq_size / 2;
    TextRenderer::draw_rounded_rect_filled(canvas, W, H,
        sq_x, sq_y, sq_size, sq_size, 0, 255, 255, 255, 25);
    TextRenderer::draw_rounded_rect_outline(canvas, W, H,
        sq_x, sq_y, sq_size, sq_size, 0, 3, 255, 255, 255, 130);

    // 4. Disk tron ben trong
    int disk_cx = (int)(W * 0.23f);
    int disk_cy = H / 2;
    int disk_r = sq_size / 2 - 10;
    TextRenderer::draw_circle_filled(canvas, W, H, disk_cx, disk_cy, disk_r,
        255, 255, 255, 50);
    for (int angle = 0; angle < 360; angle++) {
        float rad = angle * 3.14159265f / 180.0f;
        for (int t = 0; t < 2; t++) {
            int px = disk_cx + (int)(std::cos(rad) * (disk_r - t));
            int py = disk_cy + (int)(std::sin(rad) * (disk_r - t));
            render::color::blend_pixel(canvas, W, H, px, py,
                render::color::RGBA(255, 255, 255, 130));
        }
    }

    // 5. Ve 3 dice trong disk (dung 3 goc co dinh + jitter)
    {
        int dice_size = 55;
        int draw_size = 55;
        int safe_r = disk_r - dice_size / 2;

        // Tinh 3 vi tri theo goc 0, 120, 240 do + jitter nho
        std::vector<std::pair<int,int>> pos;
        const float base_angles[3] = { -1.5708f, 0.5236f, 2.6180f };  // -90, 30, 150
        for (int i = 0; i < 3; i++) {
            float angle = base_angles[i] + rand_float(-0.35f, 0.35f);
            float r_ratio = rand_float(0.70f, 0.95f);
            int px = disk_cx + (int)(std::cos(angle) * safe_r * r_ratio);
            int py = disk_cy + (int)(std::sin(angle) * safe_r * r_ratio);
            pos.push_back(std::make_pair(px, py));
        }

        // Ve dice
        for (int i = 0; i < 3 && i < (int)dice.size(); i++) {
            std::string key = "dice_" + std::to_string(dice[i]);
            ImageRGBA* im = AssetManager::get(key);
            if (!im || !im->valid()) continue;
            float rot = rand_float(-0.6f, 0.6f);  // xoay +-34 do
            TextRenderer::draw_image_rotated(canvas, W, H, im,
                pos[i].first, pos[i].second, draw_size, rot);
        }
    }
    // 6. Text ben phai
    int text_cx = (int)(W * 0.66f);
    bool is_even = (total % 2 == 0);
    std::string result_text = is_even ? "CHẴN" : "LẺ";

    unsigned char r1, g1, b1, r2, g2, b2;
    if (is_even) {
        r1 = 0;   g1 = 255; b1 = 0;
        r2 = 50;  g2 = 205; b2 = 50;
    } else {
        r1 = 0;   g1 = 255; b1 = 0;
        r2 = 50;  g2 = 205; b2 = 50;
    }

    // Big result text voi outline
    {
        int size = 58;;
        int tw = TextRenderer::measure_text_width(size, result_text);
        int tx = text_cx - tw / 2;
        int ty = (int)(H * 0.30f) - size / 2;
        for (int dx = -4; dx <= 4; dx++) {
            for (int dy = -4; dy <= 4; dy++) {
                if (dx == 0 && dy == 0) continue;
                if (dx*dx + dy*dy > 16) continue;
                TextRenderer::draw_text_on_canvas(canvas, W, H, tx + dx, ty + dy,
                    size, 0, 0, 0, result_text);
            }
        }
        TextRenderer::draw_text_gradient(canvas, W, H, tx, ty, size,
            r1, g1, b1, r2, g2, b2, result_text);
    }

    // Total
    {
        std::string s = "Tổng: " + std::to_string(total);
        int size = 48;;
        int tw = TextRenderer::measure_text_width(size, s);
        int tx = text_cx - tw / 2;
        int ty = (int)(H * 0.55f) - size / 2;
        TextRenderer::draw_text_on_canvas(canvas, W, H, tx + 2, ty + 2, size, 0, 0, 0, s);
        TextRenderer::draw_text_on_canvas(canvas, W, H, tx, ty, size, 255, 255, 255, s);
    }

    // Money
    {
        bool pwin = (is_even && player_choice == "chan") || (!is_even && player_choice == "le");
        std::string s;
        unsigned char mr, mg, mb;
        if (pwin) {
            cpp_int net = winnings - bet;
            s = "Thắng: +" + render::utils::format_number(net) + " VNĐ";
            mr = 255; mg = 215; mb = 0;
        } else {
            s = "Thua: " + render::utils::format_number(bet) + " VNĐ";
            mr = 224; mg = 224; mb = 224;
        }
        int size = 42;;
        int tw = TextRenderer::measure_text_width(size, s);
        int tx = text_cx - tw / 2;
        int ty = (int)(H * 0.80f) - size / 2;
        TextRenderer::draw_text_on_canvas(canvas, W, H, tx + 2, ty + 2, size, 0, 0, 0, s);
        TextRenderer::draw_text_on_canvas(canvas, W, H, tx, ty, size, mr, mg, mb, s);
    }

    // 7. History bar
    if (!history.empty()) {
        int hist_x = (int)(W * 0.56f);
        int hist_y = H - 48;
        int box_w = 350;
        int box_h = 36;
        int box_x = hist_x - 100;
        int box_y = hist_y - 25;

        TextRenderer::draw_rounded_rect_filled(canvas, W, H,
            box_x, box_y, box_w, box_h, 18, 0, 0, 0, 76);
        TextRenderer::draw_rounded_rect_outline(canvas, W, H,
            box_x, box_y, box_w, box_h, 18, 1, 255, 255, 255, 180);

        TextRenderer::draw_text_on_canvas(canvas, W, H,
            hist_x - 88, hist_y - 4, 20, 255, 255, 255, "Lịch sử:");

        int n = (int)history.size();
        if (n > 10) n = 10;
        int start_i = (int)history.size() - n;
        for (int i = 0; i < n; i++) {
            int dx = hist_x - 8 + i * 25;
            int dy = hist_y - 11;
            bool is_even_hist = history[start_i + i];
            TextRenderer::draw_circle_filled(canvas, W, H, dx, dy, 9, 255, 255, 255, 255);
            if (is_even_hist) {
                TextRenderer::draw_circle_filled(canvas, W, H, dx, dy, 7, 240, 240, 240, 255);
            } else {
                TextRenderer::draw_circle_filled(canvas, W, H, dx, dy, 7, 20, 20, 20, 255);
            }
        }
    }

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

} // namespace game_render
