#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "TextRenderer.hpp"
#include "SystemInfo.hpp"
#include "AssetManager.hpp"
#include "Gradient.hpp"
#include "Color.hpp"
#include "stb_image_write.h"

namespace bot_info_render {

    inline int measure_text(const std::string& text, int size) {
        return TextRenderer::measure_text_width(size, text);
    }

    inline void draw_bar(std::vector<uint8_t>& cv, int W, int H, int x, int y, int w, int h,
                         int percent, uint8_t r, uint8_t g, uint8_t b) {
        percent = std::clamp(percent, 0, 100);
        TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, w, h, h / 2, 45, 52, 72, 255);
        int fillW = (w * percent) / 100;
        if (fillW > 0) {
            fillW = std::max(fillW, h);
            TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, fillW, h, h / 2, r, g, b, 255);
        }
    }

    inline void render(const std::string& bot_name, const SystemDetails& d, const std::string& out_path) {
        const int W = 1040;
        const int H = 1140;
        std::vector<uint8_t> cv;
        TextRenderer::init_canvas(cv, W, H, 15, 20, 35);

        {
            render::gradient::LinearGradient bg(0, 0, 0, (float)H);
            bg.add_stop(0.0f, render::color::from_hex("#0F172A"));
            bg.add_stop(0.5f, render::color::from_hex("#16203A"));
            bg.add_stop(1.0f, render::color::from_hex("#0B1120"));
            bg.fill(cv, W, H);
        }

        int y = 30;
        const int boxW = W - 80;
        const int label_x = 90;
        const int val_right_x = W - 90;

        auto draw_row = [&](const std::string& label, const std::string& val, int ry) {
            TextRenderer::draw_text_on_canvas(cv, W, H, label_x, ry, 23, 180, 195, 220, label);
            int valW = measure_text(val, 23);
            int valX = std::max(label_x + measure_text(label, 23) + 20, val_right_x - valW);
            TextRenderer::draw_text_on_canvas(cv, W, H, valX, ry, 23, 245, 248, 255, val);
        };

        // === CARD 1: OVERVIEW ===
        TextRenderer::draw_rounded_rect_shadow(cv, W, H, 40, y, boxW, 240, 22, 12, 85);
        TextRenderer::draw_rounded_rect_filled(cv, W, H, 40, y, boxW, 240, 22, 26, 34, 56, 230);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, 40, y, boxW, 240, 22, 2, 70, 130, 230, 180);

        std::string t1 = "📊 Tổng Quan Hệ Thống";
        int t1x = (W - measure_text(t1, 36)) / 2;
        TextRenderer::draw_text_gradient_random(cv, W, H, t1x, y + 54, 36, t1);
        TextRenderer::draw_line(cv, W, H, 80, y + 74, W - 80, y + 74, 70, 130, 230, 1, 140);

        draw_row("Tên Bot:", bot_name, y + 114);
        draw_row("Thời gian hoạt động:", d.bot_uptime, y + 160);
        draw_row("Bộ nhớ bot sử dụng:", d.bot_ram, y + 206);

        y += 265;

        // === CARD 2: PERFORMANCE ===
        TextRenderer::draw_rounded_rect_shadow(cv, W, H, 40, y, boxW, 505, 22, 12, 85);
        TextRenderer::draw_rounded_rect_filled(cv, W, H, 40, y, boxW, 505, 22, 26, 34, 56, 230);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, 40, y, boxW, 505, 22, 2, 130, 95, 235, 180);

        std::string t2 = "⚡ Hiệu Năng & Phần Cứng";
        int t2x = (W - measure_text(t2, 36)) / 2;
        TextRenderer::draw_text_gradient_random(cv, W, H, t2x, y + 54, 36, t2);
        TextRenderer::draw_line(cv, W, H, 80, y + 74, W - 80, y + 74, 130, 95, 235, 1, 140);

        int ry = y + 114;
        draw_row("Hệ điều hành:", d.os_name, ry); ry += 46;
        draw_row("Uptime OS:", d.uptime_os, ry); ry += 46;
        draw_row("CPU Model:", d.cpu_model, ry); ry += 46;

        draw_row("CPU Usage:", d.cpu_usage, ry);
        draw_bar(cv, W, H, label_x, ry + 12, val_right_x - label_x, 12, d.cpu_percent, 56, 189, 248);
        ry += 58;

        draw_row("Tiến trình:", d.processes, ry); ry += 46;

        std::string ramText = d.ram_used + " / " + d.ram_total + " (" + std::to_string(d.ram_percent) + "%, Free " + d.ram_free + ")";
        draw_row("RAM Usage:", ramText, ry);
        draw_bar(cv, W, H, label_x, ry + 12, val_right_x - label_x, 12, d.ram_percent, 168, 85, 247);
        ry += 58;

        std::string diskText = d.disk_used + " / " + d.disk_total + " (" + std::to_string(d.disk_percent) + "%, Free " + d.disk_free + ")";
        draw_row("Disk Usage (C:):", diskText, ry);
        draw_bar(cv, W, H, label_x, ry + 12, val_right_x - label_x, 12, d.disk_percent, 34, 197, 94);

        y += 530;

        // === CARD 3: NETWORK ===
        TextRenderer::draw_rounded_rect_shadow(cv, W, H, 40, y, boxW, 285, 22, 12, 85);
        TextRenderer::draw_rounded_rect_filled(cv, W, H, 40, y, boxW, 285, 22, 26, 34, 56, 230);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, 40, y, boxW, 285, 22, 2, 45, 212, 191, 180);

        std::string t3 = "🌐 Kết Nối Mạng";
        int t3x = (W - measure_text(t3, 36)) / 2;
        TextRenderer::draw_text_gradient_random(cv, W, H, t3x, y + 54, 36, t3);
        TextRenderer::draw_line(cv, W, H, 80, y + 74, W - 80, y + 74, 45, 212, 191, 1, 140);

        ry = y + 114;
        draw_row("Giao diện mạng:", d.net_name.empty() ? "Ethernet / Wi-Fi" : d.net_name, ry); ry += 46;
        draw_row("Thiết bị:", d.net_desc.empty() ? "Network Adapter" : d.net_desc, ry); ry += 46;
        draw_row("Giao thức:", "IPv4 / WebSocket Zalo", ry); ry += 46;
        draw_row("Lưu lượng:", d.net_sent + " (Gửi) / " + d.net_recv + " (Nhận)", ry);

        TextRenderer::save_canvas(cv, W, H, out_path);
    }
}
