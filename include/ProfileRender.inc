// ProfileRender.inc - Port tu createUserCardGame (info.js)
#pragma once
#include <boost/multiprecision/cpp_int.hpp>
using cpp_int = boost::multiprecision::cpp_int;
#include <string>
#include <vector>
#include <cmath>
#include <random>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "Color.hpp"
#include "Gradient.hpp"
#include "Shape.hpp"
#include "EmojiRenderer.hpp"
#include "Utils.hpp"

namespace profile_render {

struct PlayerCardData {
    std::string name;
    std::string account;
    std::string avatar_key;    // key trong AssetManager, "" = khong co
    cpp_int balance = 0;     // TK Bot (bank)
    cpp_int total_winnings = 0;
    cpp_int total_losses = 0;
    cpp_int net_profit = 0;
    int total_games = 0;
    int total_wins = 0;
    int win_rate = 0;          // 0-100
    std::string created_time = "An";
    std::string daily_status = "Chua nhan qua";
    bool is_online = false;
    bool is_active = false;
    bool is_active_pc = false;
    bool is_active_web = false;
};

inline std::mt19937& rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}

inline float rand_f(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng());
}

// ============================================================
// render_player_card
// ============================================================
inline void render_player_card(const PlayerCardData& d, const std::string& out_path) {
    const int W = 1080;
    const int H = 515;

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 0, 0, 0);

    // 1. Background: purple gradient
    {
        render::gradient::LinearGradient bg(0, 0, (float)W, (float)H);
        bg.add_stop(0.0f, render::color::from_hex("#6a3093"));
        bg.add_stop(0.5f, render::color::from_hex("#a044ff"));
        bg.add_stop(1.0f, render::color::from_hex("#5f2c82"));
        bg.fill(canvas, W, H);
    }

    // 2. Particles (100 cham trang mo)
    {
        for (int i = 0; i < 100; i++) {
            int x = (int)rand_f(0, W - 1);
            int y = (int)rand_f(0, H - 1);
            int r = (int)rand_f(1, 4);
            unsigned char a = (unsigned char)(rand_f(0.1f, 0.4f) * 255);
            for (int py = y - r; py <= y + r; py++)
                for (int px = x - r; px <= x + r; px++) {
                    int dx = px - x, dy = py - y;
                    if (dx*dx + dy*dy <= r*r)
                        render::color::blend_pixel(canvas, W, H, px, py,
                            render::color::RGBA(255, 255, 255, a));
                }
        }
    }

    // 3. Avatar + rainbow border
    const int avt_cx = 180;
    const int avt_cy = 190;
    const int avt_r  = 90;

    // Vien rainbow 10px
    TextRenderer::draw_circle_outline_rainbow(canvas, W, H, avt_cx, avt_cy, avt_r + 6, 10);

    // Fill avatar (anh hoac fallback xam)
    TextRenderer::draw_circle_filled(canvas, W, H, avt_cx, avt_cy, avt_r, 60, 60, 80, 255);
    if (!d.avatar_key.empty()) {
        ImageRGBA* im = AssetManager::get(d.avatar_key);
        if (im && im->valid())
            TextRenderer::draw_circle_image(canvas, W, H, im, avt_cx, avt_cy, avt_r);
    }

    // Online dot
    int dot_r = 14;
    int dot_x = avt_cx + (int)(avt_r * 0.70);
    int dot_y = avt_cy + (int)(avt_r * 0.70);
    if (d.is_online)
        TextRenderer::draw_circle_filled(canvas, W, H, dot_x, dot_y, dot_r, 30, 200, 60, 255);
    else
        TextRenderer::draw_circle_filled(canvas, W, H, dot_x, dot_y, dot_r, 128, 128, 128, 255);
    TextRenderer::draw_circle_filled(canvas, W, H, dot_x, dot_y, dot_r - 3, 255, 255, 255, 100);

    // 4. Name duoi avatar
    int name_y = avt_cy + avt_r + 50;
    int name_w = TextRenderer::measure_text_width(28, d.name);
    TextRenderer::draw_text_shadow(canvas, W, H, avt_cx - name_w / 2, name_y, 28,
        255, 255, 255, d.name);

    // 5. Icons duoi ten
    int icon_y = name_y + 40;
    int icon_size = 24;
    int icon_gap = 10;
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
        if (!ic) {
            std::string path = "assets/emoji/" + icons[i] + ".png";
            ic = AssetManager::load(key, path);
        }
        if (ic && ic->valid())
            TextRenderer::draw_image_on_canvas(canvas, W, H, ic,
                ic_start_x + i * (icon_size + icon_gap), icon_y, icon_size, icon_size);
    }

    // 6. Title
    std::string title = "Thông Tin Người Chơi";
    int title_size = 48;
    int tw = TextRenderer::measure_text_width(title_size, title);
    TextRenderer::draw_text_gradient_random(canvas, W, H, (W - tw) / 2, 52, title_size, title);

    // 7. Fields
    int info_x = 340;
    int y = 105;
    int field_size = 28;
    int gap = 42;

    auto draw_field = [&](const std::string& label, const std::string& value) {
        int lw = TextRenderer::measure_text_width(field_size, label);
        TextRenderer::draw_text_gradient_random(canvas, W, H, info_x, y, field_size, label);
        TextRenderer::draw_text_on_canvas(canvas, W, H, info_x + lw, y, field_size,
            255, 255, 255, " " + value);
        y += gap;
    };

    draw_field("🆔 Tên Đăng Nhập:", d.account);
    draw_field("💰 Số Dư Hiện Tại:", render::utils::format_number(d.balance) + " VNĐ");
    draw_field("🏆 Tổng Thắng:", render::utils::format_number(d.total_winnings) + " VNĐ");
    draw_field("💸 Tổng Thua:", render::utils::format_number(d.total_losses) + " VNĐ");
    draw_field("💹 Lợi Nhuận Ròng:", render::utils::format_number(d.net_profit) + " VNĐ");
    draw_field("🎮 Số Lượt Chơi:",
        std::to_string(d.total_games) + " Games (" +
        std::to_string(d.total_wins) + "W/" +
        std::to_string(d.total_games - d.total_wins) + "L)");

    // 8. Ti le thang voi progress bar
    {
        std::string label = "📊 Tỉ Lệ Thắng:";
        int lw = TextRenderer::measure_text_width(field_size, label);
        TextRenderer::draw_text_gradient_random(canvas, W, H, info_x, y, field_size, label);

        int bar_x = info_x + lw + 15;
        int bar_y = y - 18;
        int bar_w = 200;
        int bar_h = 20;

        // Nen xam
        TextRenderer::draw_rounded_rect_filled(canvas, W, H,
            bar_x, bar_y, bar_w, bar_h, 5, 200, 200, 200, 255);

        // Phan fill
        int filled_w = bar_w * d.win_rate / 100;
        if (filled_w > 0)
            TextRenderer::draw_rounded_rect_filled(canvas, W, H,
                bar_x, bar_y, filled_w, bar_h, 5, 100, 220, 130, 255);

        // So %
        std::string pct = std::to_string(d.win_rate) + "%";
        TextRenderer::draw_text_on_canvas(canvas, W, H,
            bar_x + bar_w + 10, y, field_size, 255, 255, 255, pct);
        y += gap;
    }

    draw_field("📅 Created Time:", d.created_time);
    draw_field("🎁 Nhận Quà Daily:", d.daily_status);

    // 9. Divider
    y -= 20;
    for (int x = 50; x < W - 50; x++)
        TextRenderer::draw_line(canvas, W, H, x, y, x, y + 1, 255, 255, 255, 2, 255);
    y += 32;

    // 10. Footer
    std::string footer = "Chúc Bạn 8386 | Mãi Đỉnh Mãi Đỉnh";
    int fw = TextRenderer::measure_text_width(28, footer);
    TextRenderer::draw_text_gradient_random(canvas, W, H, (W - fw) / 2, y, 28, footer);

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

} // namespace profile_render
