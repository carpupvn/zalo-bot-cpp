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
    const int W = 820;
    const int H = 350;

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 10, 14, 34);

    {
        render::gradient::RadialGradient g(W / 2.0f, H / 2.0f, W / 1.8f);
        if (is_jackpot) {
            g.add_stop(0.0f, render::color::from_hex("#B45309"));
            g.add_stop(0.6f, render::color::from_hex("#78350F"));
            g.add_stop(1.0f, render::color::from_hex("#451A03"));
        } else {
            g.add_stop(0.0f, render::color::from_hex("#1E293B"));
            g.add_stop(0.65f, render::color::from_hex("#0F172A"));
            g.add_stop(1.0f, render::color::from_hex("#020617"));
        }
        g.fill(canvas, W, H);
    }

    {
        int cnt = is_jackpot ? 90 : 50;
        for (int i = 0; i < cnt; i++) {
            int x = rand_int(0, W - 1);
            int y = rand_int(0, H - 1);
            unsigned char a = (unsigned char)(rand_float(0.20f, is_jackpot ? 0.85f : 0.55f) * 255);
            TextRenderer::draw_circle_filled(canvas, W, H, x, y, 1, 255, 255, 255, a);
        }
    }

    // Left glass frame + disc
    int sq_size = 230;
    int sq_x = (int)(W * 0.22f) - sq_size / 2;
    int sq_y = H / 2 - sq_size / 2;
    TextRenderer::draw_rounded_rect_filled(canvas, W, H, sq_x, sq_y, sq_size, sq_size, 26, 255, 255, 255, 24);
    TextRenderer::draw_rounded_rect_outline(canvas, W, H, sq_x, sq_y, sq_size, sq_size, 26, 2, 148, 163, 184, 140);

    int disk_cx = (int)(W * 0.22f);
    int disk_cy = H / 2;
    int disk_r = sq_size / 2 - 14;
    TextRenderer::draw_circle_filled(canvas, W, H, disk_cx, disk_cy, disk_r, 255, 255, 255, 38);
    TextRenderer::draw_rounded_rect_outline(canvas, W, H, disk_cx - disk_r, disk_cy - disk_r,
                                            disk_r * 2, disk_r * 2, disk_r, 2, 255, 255, 255, 110);

    {
        int dice_size = 56;
        int safe_r = disk_r - dice_size / 2 - 4;
        const float base_angles[3] = { -1.5708f, 0.5236f, 2.6180f };
        for (int i = 0; i < 3 && i < (int)dice.size(); i++) {
            float angle = base_angles[i] + rand_float(-0.30f, 0.30f);
            float r_ratio = rand_float(0.68f, 0.92f);
            int px = disk_cx + (int)(std::cos(angle) * safe_r * r_ratio);
            int py = disk_cy + (int)(std::sin(angle) * safe_r * r_ratio);
            std::string key = "dice_" + std::to_string(dice[i]);
            ImageRGBA* im = AssetManager::get(key);
            if (!im || !im->valid()) continue;
            float rot = rand_float(-0.55f, 0.55f);
            TextRenderer::draw_image_rotated(canvas, W, H, im, px, py, dice_size, rot);
        }
    }

    // Right side panel
    int text_cx = (int)(W * 0.67f);
    bool is_even = (total % 2 == 0);
    std::string result_text = is_even ? "CHẴN" : "LẺ";

    unsigned char r1, g1, b1, r2, g2, b2;
    if (is_even) {
        // Cyan -> Royal Blue
        r1 = 56;  g1 = 239; b1 = 255;
        r2 = 59;  g2 = 130; b2 = 246;
    } else {
        // Amber -> Coral Red
        r1 = 253; g1 = 224; b1 = 71;
        r2 = 239; g2 = 68;  b2 = 68;
    }

    {
        int size = 68;
        int tw = TextRenderer::measure_text_width(size, result_text);
        int tx = text_cx - tw / 2;
        int ty = 95;
        for (int dx = -3; dx <= 3; dx++) {
            for (int dy = -3; dy <= 3; dy++) {
                if ((dx == 0 && dy == 0) || dx * dx + dy * dy > 11) continue;
                TextRenderer::draw_text_on_canvas(canvas, W, H, tx + dx, ty + dy, size, 0, 0, 0, result_text);
            }
        }
        TextRenderer::draw_text_gradient(canvas, W, H, tx, ty, size, r1, g1, b1, r2, g2, b2, result_text);
    }

    {
        std::string s = "Tổng điểm: " + std::to_string(total);
        int size = 34;
        int tw = TextRenderer::measure_text_width(size, s);
        TextRenderer::draw_text_shadow(canvas, W, H, text_cx - tw / 2, 160, size, 241, 245, 249, s);
    }

    {
        bool pwin = (is_even && player_choice == "chan") || (!is_even && player_choice == "le");
        std::string s;
        unsigned char mr, mg, mb;
        if (pwin) {
            cpp_int net = winnings - bet;
            s = "Thắng: +" + render::utils::format_number(net) + " VNĐ";
            mr = 74; mg = 222; mb = 128;
        } else {
            s = "Thua: -" + render::utils::format_number(bet) + " VNĐ";
            mr = 248; mg = 113; mb = 113;
        }
        int size = 34;
        int tw = TextRenderer::measure_text_width(size, s);
        TextRenderer::draw_text_shadow(canvas, W, H, text_cx - tw / 2, 220, size, mr, mg, mb, s);
    }

    if (!history.empty()) {
        int box_w = 400, box_h = 44;
        int box_x = text_cx - box_w / 2;
        int box_y = H - 72;
        TextRenderer::draw_rounded_rect_filled(canvas, W, H, box_x, box_y, box_w, box_h, 22, 0, 0, 0, 110);
        TextRenderer::draw_rounded_rect_outline(canvas, W, H, box_x, box_y, box_w, box_h, 22, 1, 255, 255, 255, 110);
        TextRenderer::draw_text_on_canvas(canvas, W, H, box_x + 18, box_y + 29, 20, 226, 232, 240, "Lịch sử:");

        int n = std::min(10, (int)history.size());
        int start_i = (int)history.size() - n;
        for (int i = 0; i < n; i++) {
            int dx = box_x + 115 + i * 27;
            int dy = box_y + 22;
            bool is_even_hist = history[start_i + i];
            TextRenderer::draw_circle_filled(canvas, W, H, dx, dy, 10, 255, 255, 255, 220);
            if (is_even_hist)
                TextRenderer::draw_circle_filled(canvas, W, H, dx, dy, 8, 56, 189, 248, 255);
            else
                TextRenderer::draw_circle_filled(canvas, W, H, dx, dy, 8, 239, 68, 68, 255);
        }
    }

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

} // namespace game_render
