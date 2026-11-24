#pragma once
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "Gradient.hpp"
#include "Color.hpp"
#include "Utils.hpp"

namespace extra_render {

inline void render_soicau(
    const std::vector<std::pair<int, bool>>& tx_history, // (total 3..18, is_tai)
    const std::vector<bool>& cl_history,
    const std::string& mode,
    const std::string& out_path)
{
    const int W = 940;
    const int H = 480;
    std::vector<uint8_t> cv;
    TextRenderer::init_canvas(cv, W, H, 15, 23, 42);

    {
        render::gradient::LinearGradient bg(0, 0, 0, (float)H);
        bg.add_stop(0.0f, render::color::from_hex("#0F172A"));
        bg.add_stop(1.0f, render::color::from_hex("#1E293B"));
        bg.fill(cv, W, H);
    }

    bool show_cl = (mode == "cl" || mode == "chanle");
    std::string title = show_cl ? "📊 SOI CẦU CHẴN LẺ" : "📊 SOI CẦU TÀI XỈU (3 - 18 ĐIỂM)";
    int tw = TextRenderer::measure_text_width(32, title);
    TextRenderer::draw_text_gradient_random(cv, W, H, (W - tw) / 2, 52, 32, title);

    const int gx = 80, gy = 86, gw = W - 120, gh = 270;
    TextRenderer::draw_rounded_rect_filled(cv, W, H, gx, gy, gw, gh, 14, 10, 15, 30, 220);
    TextRenderer::draw_rounded_rect_outline(cv, W, H, gx, gy, gw, gh, 14, 2, 71, 85, 105, 220);

    if (!show_cl) {
        // Tài/Xỉu line chart (3..18)
        for (int p = 3; p <= 18; p += 3) {
            float t = (float)(18 - p) / 15.0f;
            int yy = gy + 20 + (int)(t * (gh - 40));
            TextRenderer::draw_line(cv, W, H, gx + 10, yy, gx + gw - 10, yy, 51, 65, 85, 1, 160);
            std::string lbl = std::to_string(p);
            TextRenderer::draw_text_on_canvas(cv, W, H, gx - 38, yy + 6, 18, 148, 163, 184, lbl);
        }
        // Threshold line at 10.5
        int y10 = gy + 20 + (int)((18.0f - 10.5f) / 15.0f * (gh - 40));
        TextRenderer::draw_line(cv, W, H, gx + 10, y10, gx + gw - 10, y10, 234, 179, 8, 2, 160);

        int n = std::min(20, (int)tx_history.size());
        int start = (int)tx_history.size() - n;
        int tai_cnt = 0, xiu_cnt = 0;
        for (auto& h : tx_history) { if (h.second) tai_cnt++; else xiu_cnt++; }

        int prev_x = -1, prev_y = -1;
        for (int i = 0; i < n; i++) {
            int total = std::clamp(tx_history[start + i].first, 3, 18);
            bool is_tai = tx_history[start + i].second;
            int px = gx + 35 + (n > 1 ? (i * (gw - 70) / (n - 1)) : (gw - 70) / 2);
            float t = (float)(18 - total) / 15.0f;
            int py = gy + 20 + (int)(t * (gh - 40));

            if (prev_x >= 0) {
                TextRenderer::draw_line(cv, W, H, prev_x, prev_y, px, py, 148, 163, 184, 2, 210);
            }
            unsigned char r = is_tai ? 245 : 56;
            unsigned char g = is_tai ? 158 : 189;
            unsigned char b = is_tai ? 11  : 248;
            TextRenderer::draw_circle_filled(cv, W, H, px, py, 12, 255, 255, 255, 220);
            TextRenderer::draw_circle_filled(cv, W, H, px, py, 10, r, g, b, 255);
            std::string ts = std::to_string(total);
            int tsw = TextRenderer::measure_text_width(14, ts);
            TextRenderer::draw_text_on_canvas(cv, W, H, px - tsw / 2, py + 5, 14, 15, 23, 42, ts);
            prev_x = px; prev_y = py;
        }
        if (n == 0) {
            std::string emp = "Chưa có dữ liệu phiên Tài Xỉu nào";
            int ew = TextRenderer::measure_text_width(24, emp);
            TextRenderer::draw_text_on_canvas(cv, W, H, gx + (gw - ew) / 2, gy + gh / 2, 24, 148, 163, 184, emp);
        }

        std::string st = "Tổng phiên: " + std::to_string(tx_history.size())
                       + "   |   🟠 Tài: " + std::to_string(tai_cnt)
                       + "   |   🔵 Xỉu: " + std::to_string(xiu_cnt);
        int sw = TextRenderer::measure_text_width(24, st);
        TextRenderer::draw_text_on_canvas(cv, W, H, (W - sw) / 2, H - 55, 24, 241, 245, 249, st);
    } else {
        int n = std::min(20, (int)cl_history.size());
        int start = (int)cl_history.size() - n;
        int c_cnt = 0, l_cnt = 0;
        for (bool b : cl_history) { if (b) c_cnt++; else l_cnt++; }

        int cols = 10;
        for (int i = 0; i < n; i++) {
            int row = i / cols, col = i % cols;
            int px = gx + 55 + col * ((gw - 110) / 9);
            int py = gy + 80 + row * 100;
            bool ev = cl_history[start + i];
            TextRenderer::draw_circle_filled(cv, W, H, px, py, 24, 255, 255, 255, 220);
            TextRenderer::draw_circle_filled(cv, W, H, px, py, 21, ev ? 56 : 239, ev ? 189 : 68, ev ? 248 : 68, 255);
            std::string lb = ev ? "C" : "L";
            int lw = TextRenderer::measure_text_width(22, lb);
            TextRenderer::draw_text_on_canvas(cv, W, H, px - lw / 2, py + 8, 22, 255, 255, 255, lb);
        }
        if (n == 0) {
            std::string emp = "Chưa có dữ liệu phiên Chẵn Lẻ nào";
            int ew = TextRenderer::measure_text_width(24, emp);
            TextRenderer::draw_text_on_canvas(cv, W, H, gx + (gw - ew) / 2, gy + gh / 2, 24, 148, 163, 184, emp);
        }

        std::string st = "Tổng phiên: " + std::to_string(cl_history.size())
                       + "   |   🔵 Chẵn: " + std::to_string(c_cnt)
                       + "   |   🔴 Lẻ: " + std::to_string(l_cnt);
        int sw = TextRenderer::measure_text_width(24, st);
        TextRenderer::draw_text_on_canvas(cv, W, H, (W - sw) / 2, H - 55, 24, 241, 245, 249, st);
    }

    TextRenderer::save_canvas(cv, W, H, out_path);
}

inline void render_vietlott_result(
    const std::vector<int>& main_nums,
    int extra_num,
    bool is_jackpot,
    const std::string& subtitle,
    const std::string& out_path)
{
    const int W = 860;
    const int H = 290;
    std::vector<uint8_t> cv;
    TextRenderer::init_canvas(cv, W, H, 26, 35, 126);

    {
        render::gradient::LinearGradient bg(0, 0, 0, (float)H);
        bg.add_stop(0.0f, render::color::from_hex(is_jackpot ? "#78350F" : "#1E1B4B"));
        bg.add_stop(1.0f, render::color::from_hex(is_jackpot ? "#451A03" : "#0F172A"));
        bg.fill(cv, W, H);
    }

    std::string ttl = is_jackpot ? "💥 JACKPOT VIETLOTT 6/55 💥" : "🎱 KẾT QUẢ VIETLOTT 6/55 🎱";
    int tw = TextRenderer::measure_text_width(34, ttl);
    TextRenderer::draw_text_gradient_random(cv, W, H, (W - tw) / 2, 54, 34, ttl);

    const int radius = 38;
    const int spacing = 104;
    const int startX = (W - spacing * 6) / 2;
    const int cy = 148;

    std::vector<int> all = main_nums;
    all.push_back(extra_num);
    for (int i = 0; i < (int)all.size() && i < 7; i++) {
        int x = startX + i * spacing;
        bool is_extra = (i == 6);
        TextRenderer::draw_circle_filled(cv, W, H, x + 2, cy + 4, radius, 0, 0, 0, 110);
        TextRenderer::draw_circle_filled(cv, W, H, x, cy, radius + 3, 255, 255, 255, 235);
        if (is_extra)
            TextRenderer::draw_circle_filled(cv, W, H, x, cy, radius, 250, 204, 21, 255);
        else
            TextRenderer::draw_circle_filled(cv, W, H, x, cy, radius, 220, 38, 38, 255);

        std::string ns = (all[i] < 10 ? "0" : "") + std::to_string(all[i]);
        int nw = TextRenderer::measure_text_width(32, ns);
        if (is_extra)
            TextRenderer::draw_text_on_canvas(cv, W, H, x - nw / 2, cy + 11, 32, 15, 23, 42, ns);
        else
            TextRenderer::draw_text_on_canvas(cv, W, H, x - nw / 2, cy + 11, 32, 255, 255, 255, ns);
    }

    int sw = TextRenderer::measure_text_width(24, subtitle);
    TextRenderer::draw_text_on_canvas(cv, W, H, (W - sw) / 2, H - 36, 24, 226, 232, 240, subtitle);

    TextRenderer::save_canvas(cv, W, H, out_path);
}

inline void render_caro_board(
    const char board[15][15],
    int last_r, int last_c,
    const std::string& p1_name,
    const std::string& p2_name,
    int current_turn,
    const std::string& status_msg,
    const std::string& out_path)
{
    const int CELL = 42;
    const int PAD_L = 54, PAD_T = 112, PAD_R = 30, PAD_B = 56;
    const int GRID = 15 * CELL;
    const int W = PAD_L + GRID + PAD_R;
    const int H = PAD_T + GRID + PAD_B;

    std::vector<uint8_t> cv;
    TextRenderer::init_canvas(cv, W, H, 15, 23, 42);
    TextRenderer::fill_linear_gradient(cv, W, H, 0, H, 15, 23, 42, 30, 41, 59);

    std::string ttl = "♟️ CỜ CARO 15x15";
    int tw = TextRenderer::measure_text_width(30, ttl);
    TextRenderer::draw_text_gradient_random(cv, W, H, (W - tw) / 2, 42, 30, ttl);

    std::string vs = "❌ " + p1_name + "   VS   ⭕ " + p2_name;
    int vw = TextRenderer::measure_text_width(22, vs);
    TextRenderer::draw_text_on_canvas(cv, W, H, (W - vw) / 2, 76, 22, 226, 232, 240, vs);

    // Board wood surface
    TextRenderer::draw_rounded_rect_shadow(cv, W, H, PAD_L - 8, PAD_T - 8, GRID + 16, GRID + 16, 12, 12, 110);
    TextRenderer::draw_rounded_rect_filled(cv, W, H, PAD_L - 6, PAD_T - 6, GRID + 12, GRID + 12, 10, 238, 206, 154, 255);

    // Column headers 0..14 & Row headers 0..14
    for (int i = 0; i < 15; i++) {
        std::string lbl = std::to_string(i);
        int lw = TextRenderer::measure_text_width(16, lbl);
        TextRenderer::draw_text_on_canvas(cv, W, H, PAD_L + i * CELL + (CELL - lw) / 2, PAD_T - 14, 16, 203, 213, 225, lbl);
        TextRenderer::draw_text_on_canvas(cv, W, H, PAD_L - 16 - lw, PAD_T + i * CELL + CELL / 2 + 6, 16, 203, 213, 225, lbl);
    }

    // Grid lines
    for (int i = 0; i <= 15; i++) {
        TextRenderer::draw_line(cv, W, H, PAD_L, PAD_T + i * CELL, PAD_L + GRID, PAD_T + i * CELL, 120, 82, 45, 1, 200);
        TextRenderer::draw_line(cv, W, H, PAD_L + i * CELL, PAD_T, PAD_L + i * CELL, PAD_T + GRID, 120, 82, 45, 1, 200);
    }

    // Pieces
    for (int r = 0; r < 15; r++) {
        for (int c = 0; c < 15; c++) {
            int cx = PAD_L + c * CELL + CELL / 2;
            int cy = PAD_T + r * CELL + CELL / 2;
            if (r == last_r && c == last_c) {
                TextRenderer::draw_rect_filled(cv, W, H, PAD_L + c * CELL + 2, PAD_T + r * CELL + 2,
                                               CELL - 4, CELL - 4, 250, 204, 21, 95);
            }
            if (board[r][c] == 'X') {
                int off = 11;
                TextRenderer::draw_line(cv, W, H, cx - off, cy - off, cx + off, cy + off, 220, 38, 38, 4, 255);
                TextRenderer::draw_line(cv, W, H, cx + off, cy - off, cx - off, cy + off, 220, 38, 38, 4, 255);
            } else if (board[r][c] == 'O') {
                TextRenderer::draw_circle_filled(cv, W, H, cx, cy, 14, 37, 99, 235, 255);
                TextRenderer::draw_circle_filled(cv, W, H, cx, cy, 9, 238, 206, 154, 255);
            }
        }
    }

    std::string foot = status_msg.empty()
        ? ("Lượt: " + std::string(current_turn == 1 ? "❌ " + p1_name : "⭕ " + p2_name) + "  |  Gõ: <dòng> <cột> (VD: 7 7)")
        : status_msg;
    int fw = TextRenderer::measure_text_width(20, foot);
    TextRenderer::draw_text_on_canvas(cv, W, H, (W - fw) / 2, H - 18, 20, 253, 224, 71, foot);

    TextRenderer::save_canvas(cv, W, H, out_path);
}

inline void render_group_info(
    const std::string& name,
    const std::string& gid,
    int member_count,
    int admin_count,
    const std::string& owner_name,
    const std::string& created_str,
    const std::string& desc,
    bool lock_chat,
    bool join_appr,
    const std::string& avt_key,
    const std::string& out_path)
{
    std::vector<std::string> desc_lines = desc.empty()
        ? std::vector<std::string>{}
        : TextRenderer::wrap_text(22, desc, 820);
    if (desc_lines.size() > 4) desc_lines.resize(4);

    const int W = 940;
    const int H = 370 + (desc_lines.empty() ? 0 : (int)desc_lines.size() * 32 + 34);
    std::vector<uint8_t> cv;
    TextRenderer::init_canvas(cv, W, H, 17, 24, 39);
    TextRenderer::fill_linear_gradient(cv, W, H, 0, H, 37, 99, 235, 17, 24, 39);

    TextRenderer::draw_rounded_rect_shadow(cv, W, H, 24, 24, W - 48, H - 48, 24, 12, 85);
    TextRenderer::draw_rounded_rect_filled(cv, W, H, 24, 24, W - 48, H - 48, 24, 15, 23, 42, 185);
    TextRenderer::draw_rounded_rect_outline(cv, W, H, 24, 24, W - 48, H - 48, 24, 2, 96, 165, 250, 180);

    const int avt_cx = 160, avt_cy = 175, avt_r = 82;
    TextRenderer::draw_circle_outline_rainbow(cv, W, H, avt_cx, avt_cy, avt_r + 6, 8);
    TextRenderer::draw_circle_filled(cv, W, H, avt_cx, avt_cy, avt_r, 30, 41, 59, 255);
    if (!avt_key.empty()) {
        ImageRGBA* im = AssetManager::get(avt_key);
        if (im && im->valid()) TextRenderer::draw_circle_image(cv, W, H, im, avt_cx, avt_cy, avt_r);
    }

    auto name_lines = TextRenderer::wrap_text(24, name, 240);
    for (size_t i = 0; i < name_lines.size() && i < 2; i++) {
        int nw = TextRenderer::measure_text_width(24, name_lines[i]);
        TextRenderer::draw_text_shadow(cv, W, H, avt_cx - nw / 2, avt_cy + avt_r + 42 + (int)i * 28,
                                       24, 255, 255, 255, name_lines[i]);
    }

    std::string ttl = "📋 THÔNG TIN NHÓM";
    int tw = TextRenderer::measure_text_width(34, ttl);
    TextRenderer::draw_text_gradient_random(cv, W, H, (W - tw) / 2, 68, 34, ttl);

    int ix = 320, y = 122, lh = 40;
    auto row = [&](const std::string& l, const std::string& v) {
        int lw = TextRenderer::measure_text_width(23, l);
        TextRenderer::draw_text_gradient_random(cv, W, H, ix, y, 23, l);
        TextRenderer::draw_text_on_canvas(cv, W, H, ix + lw + 10, y, 23, 241, 245, 249, v);
        y += lh;
    };
    row("🆔 ID Nhóm:", gid);
    row("👥 Thành viên:", std::to_string(member_count) + " (Phó nhóm: " + std::to_string(admin_count) + ")");
    row("👑 Trưởng nhóm:", owner_name);
    row("📅 Ngày tạo:", created_str);
    row("🔒 Khóa chat:", lock_chat ? "Đang bật" : "Đang tắt");
    row("🆕 Duyệt thành viên:", join_appr ? "Đang bật" : "Tắt");

    if (!desc_lines.empty()) {
        int dy = 368;
        TextRenderer::draw_line(cv, W, H, 56, dy - 20, W - 56, dy - 20, 96, 165, 250, 1, 140);
        for (const auto& dl : desc_lines) {
            int dw = TextRenderer::measure_text_width(22, dl);
            TextRenderer::draw_text_on_canvas(cv, W, H, (W - dw) / 2, dy + 12, 22, 186, 230, 253, dl);
            dy += 32;
        }
    }

    TextRenderer::save_canvas(cv, W, H, out_path);
}

inline void render_event_banner(
    bool is_welcome,
    const std::string& user_name,
    const std::string& group_name,
    const std::string& avt_key,
    const std::string& out_path)
{
    const int W = 960;
    const int H = 300;
    std::vector<uint8_t> cv;
    TextRenderer::init_canvas(cv, W, H, 20, 24, 48);

    {
        render::gradient::LinearGradient bg(0, 0, (float)W, (float)H);
        if (is_welcome) {
            bg.add_stop(0.0f, render::color::from_hex("#0F172A"));
            bg.add_stop(0.5f, render::color::from_hex("#064E3B"));
            bg.add_stop(1.0f, render::color::from_hex("#1E293B"));
        } else {
            bg.add_stop(0.0f, render::color::from_hex("#1E1B4B"));
            bg.add_stop(0.5f, render::color::from_hex("#31102F"));
            bg.add_stop(1.0f, render::color::from_hex("#0F172A"));
        }
        bg.fill(cv, W, H);
    }

    TextRenderer::draw_rounded_rect_filled(cv, W, H, 22, 22, W - 44, H - 44, 24, 15, 23, 42, 175);
    TextRenderer::draw_rounded_rect_outline(cv, W, H, 22, 22, W - 44, H - 44, 24, 2,
                                            is_welcome ? 52 : 244, is_welcome ? 211 : 114, is_welcome ? 153 : 182, 210);

    const int avt_cx = 140, avt_cy = H / 2, avt_r = 76;
    TextRenderer::draw_circle_outline_rainbow(cv, W, H, avt_cx, avt_cy, avt_r + 5, 7);
    TextRenderer::draw_circle_filled(cv, W, H, avt_cx, avt_cy, avt_r, 30, 41, 59, 255);
    if (!avt_key.empty()) {
        ImageRGBA* im = AssetManager::get(avt_key);
        if (im && im->valid()) TextRenderer::draw_circle_image(cv, W, H, im, avt_cx, avt_cy, avt_r);
    }

    std::string h1 = is_welcome ? "🎉 CHÀO MỪNG THÀNH VIÊN MỚI" : "👋 TẠM BIỆT THÀNH VIÊN";
    TextRenderer::draw_text_gradient_random(cv, W, H, 250, 102, 32, h1);

    auto u_lines = TextRenderer::wrap_text(34, user_name, 650);
    TextRenderer::draw_text_shadow(cv, W, H, 250, 158, 34, 255, 255, 255,
                                   u_lines.empty() ? user_name : u_lines[0]);

    std::string sub = is_welcome
        ? ("Chào mừng bạn gia nhập nhóm " + group_name + "!")
        : ("Đã rời khỏi nhóm " + group_name + ". Hẹn gặp lại!");
    auto s_lines = TextRenderer::wrap_text(22, sub, 650);
    for (size_t i = 0; i < s_lines.size() && i < 2; i++) {
        TextRenderer::draw_text_on_canvas(cv, W, H, 250, 206 + (int)i * 30, 22, 203, 213, 225, s_lines[i]);
    }

    TextRenderer::save_canvas(cv, W, H, out_path);
}

} // namespace extra_render
