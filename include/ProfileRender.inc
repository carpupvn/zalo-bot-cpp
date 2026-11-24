#pragma once
#include <boost/multiprecision/cpp_int.hpp>
using cpp_int = boost::multiprecision::cpp_int;

#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "Color.hpp"
#include "Gradient.hpp"
#include "Utils.hpp"

namespace profile_render {

struct PlayerCardData {
    std::string name;
    std::string account;
    std::string avatar_key;
    cpp_int balance = 0;
    cpp_int total_winnings = 0;
    cpp_int total_losses = 0;
    cpp_int net_profit = 0;
    int total_games = 0;
    int total_wins = 0;
    int win_rate = 0;
    std::string created_time = "Ẩn";
    std::string daily_status = "Chưa nhận quà";
    bool is_online = true;
    bool is_active = true;
    bool is_active_pc = false;
    bool is_active_web = false;
};

inline void render_player_card(const PlayerCardData& d, const std::string& out_path) {
    const int W = 1080;
    const int H = 560;

    static std::mt19937 rng{std::random_device{}()};
    auto rand_f = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 20, 12, 42);

    {
        render::gradient::LinearGradient bg(0, 0, (float)W, (float)H);
        bg.add_stop(0.0f, render::color::from_hex("#1e1b4b"));
        bg.add_stop(0.5f, render::color::from_hex("#4c1d95"));
        bg.add_stop(1.0f, render::color::from_hex("#1e293b"));
        bg.fill(canvas, W, H);
    }

    for (int i = 0; i < 80; i++) {
        int x = (int)rand_f(0, W - 1);
        int y = (int)rand_f(0, H - 1);
        int r = (int)rand_f(1, 3);
        unsigned char a = (unsigned char)(rand_f(0.10f, 0.35f) * 255);
        TextRenderer::draw_circle_filled(canvas, W, H, x, y, r, 255, 255, 255, a);
    }

    // Outer glass card
    TextRenderer::draw_rounded_rect_shadow(canvas, W, H, 24, 24, W - 48, H - 48, 26, 14, 95);
    TextRenderer::draw_rounded_rect_filled(canvas, W, H, 24, 24, W - 48, H - 48, 26, 18, 20, 42, 185);
    TextRenderer::draw_rounded_rect_outline(canvas, W, H, 24, 24, W - 48, H - 48, 26, 2, 167, 139, 250, 180);

    // Avatar + rainbow ring
    const int avt_cx = 185;
    const int avt_cy = 215;
    const int avt_r  = 92;

    TextRenderer::draw_circle_outline_rainbow(canvas, W, H, avt_cx, avt_cy, avt_r + 6, 9);
    TextRenderer::draw_circle_filled(canvas, W, H, avt_cx, avt_cy, avt_r, 50, 52, 76, 255);
    if (!d.avatar_key.empty()) {
        ImageRGBA* im = AssetManager::get(d.avatar_key);
        if (im && im->valid())
            TextRenderer::draw_circle_image(canvas, W, H, im, avt_cx, avt_cy, avt_r);
    }

    int dot_r = 15;
    int dot_x = avt_cx + (int)(avt_r * 0.71);
    int dot_y = avt_cy + (int)(avt_r * 0.71);
    TextRenderer::draw_circle_filled(canvas, W, H, dot_x, dot_y, dot_r + 3, 255, 255, 255, 230);
    if (d.is_online)
        TextRenderer::draw_circle_filled(canvas, W, H, dot_x, dot_y, dot_r, 34, 197, 94, 255);
    else
        TextRenderer::draw_circle_filled(canvas, W, H, dot_x, dot_y, dot_r, 148, 163, 184, 255);

    int name_y = avt_cy + avt_r + 54;
    std::vector<std::string> name_lines = TextRenderer::wrap_text(26, d.name, 260);
    for (size_t i = 0; i < name_lines.size() && i < 2; i++) {
        int nw = TextRenderer::measure_text_width(26, name_lines[i]);
        TextRenderer::draw_text_shadow(canvas, W, H, avt_cx - nw / 2, name_y + (int)i * 32, 26,
                                       255, 255, 255, name_lines[i]);
    }

    int icon_y = name_y + (int)std::min<size_t>(2, name_lines.size()) * 32 + 10;
    int icon_size = 24, icon_gap = 10;
    std::vector<std::string> icons;
    if (d.is_active)     icons.push_back("1f4f1");
    if (d.is_active_pc)  icons.push_back("1f4bb");
    if (d.is_active_web) icons.push_back("1f310");
    int n_ic = (int)icons.size();
    int total_ic_w = n_ic * icon_size + (n_ic > 0 ? (n_ic - 1) * icon_gap : 0);
    int ic_start_x = avt_cx - total_ic_w / 2;
    for (int i = 0; i < n_ic; i++) {
        std::string key = "emoji_" + icons[i];
        ImageRGBA* ic = AssetManager::get(key);
        if (!ic) ic = AssetManager::load(key, "assets/emoji/" + icons[i] + ".png");
        if (ic && ic->valid())
            TextRenderer::draw_image_on_canvas(canvas, W, H, ic,
                                               ic_start_x + i * (icon_size + icon_gap), icon_y, icon_size, icon_size);
    }

    // Vertical divider
    TextRenderer::draw_line(canvas, W, H, 330, 100, 330, H - 100, 167, 139, 250, 1, 90);

    // Title
    std::string title = "💳 THÔNG TIN NGƯỜI CHƠI";
    int title_size = 38;
    int tw = TextRenderer::measure_text_width(title_size, title);
    TextRenderer::draw_text_gradient_random(canvas, W, H, (W - tw) / 2, 74, title_size, title);

    // Fields
    int info_x = 365;
    int y = 132;
    const int field_size = 25;
    const int gap = 41;

    auto draw_field = [&](const std::string& label, const std::string& value,
                          unsigned char vr = 248, unsigned char vg = 250, unsigned char vb = 252) {
        int lw = TextRenderer::measure_text_width(field_size, label);
        TextRenderer::draw_text_gradient_random(canvas, W, H, info_x, y, field_size, label);
        TextRenderer::draw_text_on_canvas(canvas, W, H, info_x + lw + 10, y, field_size, vr, vg, vb, value);
        y += gap;
    };

    draw_field("🆔 Tên Đăng Nhập:", d.account);
    draw_field("💰 Số Dư Hiện Tại:", render::utils::format_number(d.balance) + " VNĐ", 253, 224, 71);
    draw_field("🏆 Tổng Thắng:", render::utils::format_number(d.total_winnings) + " VNĐ", 134, 239, 172);
    draw_field("💸 Tổng Thua:", render::utils::format_number(d.total_losses) + " VNĐ", 252, 165, 165);

    if (d.net_profit >= 0)
        draw_field("💹 Lợi Nhuận Ròng:", "+" + render::utils::format_number(d.net_profit) + " VNĐ", 74, 222, 128);
    else
        draw_field("💹 Lợi Nhuận Ròng:", render::utils::format_number(d.net_profit) + " VNĐ", 248, 113, 113);

    int losses_cnt = std::max(0, d.total_games - d.total_wins);
    draw_field("🎮 Số Lượt Chơi:",
               std::to_string(d.total_games) + " Games (" +
               std::to_string(d.total_wins) + "W / " + std::to_string(losses_cnt) + "L)");

    // Win rate progress bar
    {
        std::string label = "📊 Tỉ Lệ Thắng:";
        int lw = TextRenderer::measure_text_width(field_size, label);
        TextRenderer::draw_text_gradient_random(canvas, W, H, info_x, y, field_size, label);

        int bar_x = info_x + lw + 16;
        int bar_y = y - 16;
        int bar_w = 220;
        int bar_h = 18;
        int wr = std::clamp(d.win_rate, 0, 100);

        TextRenderer::draw_rounded_rect_filled(canvas, W, H, bar_x, bar_y, bar_w, bar_h, 9, 55, 65, 85, 255);
        int filled_w = bar_w * wr / 100;
        if (filled_w > 0) {
            filled_w = std::max(filled_w, bar_h);
            TextRenderer::draw_rounded_rect_filled(canvas, W, H, bar_x, bar_y, filled_w, bar_h, 9, 52, 211, 153, 255);
        }

        TextRenderer::draw_text_on_canvas(canvas, W, H, bar_x + bar_w + 14, y, field_size,
                                          255, 255, 255, std::to_string(wr) + "%");
        y += gap;
    }

    draw_field("📅 Tham Gia / HĐ:", d.created_time);
    draw_field("🎁 Quà Daily:", d.daily_status);

    // Footer
    TextRenderer::draw_line(canvas, W, H, 60, H - 86, W - 60, H - 86, 167, 139, 250, 1, 140);
    std::string footer = "✨ Chúc Bạn 8386 | Chơi Vui Có Chừng Mực ✨";
    int fw = TextRenderer::measure_text_width(24, footer);
    TextRenderer::draw_text_gradient_random(canvas, W, H, (W - fw) / 2, H - 48, 24, footer);

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

} // namespace profile_render
