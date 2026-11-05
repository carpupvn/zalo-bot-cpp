#pragma once
#include <string>
#include <vector>
#include "TextRenderer.hpp"
#include "SystemInfo.hpp"
#include "AssetManager.hpp"
#include "stb_image_write.h"
#include "stb_truetype.h"

namespace bot_info_render {
    // Hàm phụ trợ: Đo chiều rộng thực tế của chữ (dùng chung font với TextRenderer)
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
        if (!font_loaded) return (int)(text.length() * size * 0.5); // Fallback
        
        float scale = stbtt_ScaleForPixelHeight(&font, (float)size);
        int width = 0;
        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
        
        // Đo độ rộng
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

    inline void draw_bar(std::vector<uint8_t>& cv, int W, int H, int x, int y, int w, int h, int percent, uint8_t r, uint8_t g, uint8_t b) {
        TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, w, h, h/2, 50, 50, 50, 255);
        int fillW = (w * percent) / 100;
        if (fillW > 0) TextRenderer::draw_rounded_rect_filled(cv, W, H, x, y, fillW, h, h/2, r, g, b, 255);
    }

    inline void render(const std::string& bot_name, const SystemDetails& d, const std::string& out_path) {
        int W = 1000; int H = 1150;
        std::vector<uint8_t> cv; TextRenderer::init_canvas(cv, W, H, 20, 20, 25);
        
        int y = 30, boxW = W - 80;
        int label_x = 100;
        int val_right_x = W - 100; // Lề phải
        
        // === KHUNG 1: OVERVIEW ===
        TextRenderer::draw_rounded_rect_filled(cv, W, H, 40, y, boxW, 250, 20, 35, 35, 35, 200);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, 40, y, boxW, 250, 20, 2, 80, 80, 80, 255);
        std::string t1 = "Tổng Quan - Overview";
        int t1x = (W - measure_text(t1, 40)) / 2;
        TextRenderer::draw_text_gradient_random(cv, W, H, t1x, y + 50, 40, t1);
        
        auto draw_row = [&](const std::string& label, const std::string& val, int ry) {
            TextRenderer::draw_text_on_canvas(cv, W, H, label_x, ry, 24, 200, 200, 200, label);
            int valW = measure_text(val, 24);
            int valX = val_right_x - valW;
            TextRenderer::draw_text_on_canvas(cv, W, H, valX, ry, 24, 255, 255, 255, val);
        };
        
        draw_row("Tên Bot:", bot_name, y + 110);
        draw_row("Thời gian hoạt động:", d.bot_uptime, y + 160);
        draw_row("Bộ nhớ bot sử dụng:", d.bot_ram, y + 210);
        
        y += 270;
        
        // === KHUNG 2: PERFORMANCE ===
        TextRenderer::draw_rounded_rect_filled(cv, W, H, 40, y, boxW, 480, 20, 35, 35, 35, 200);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, 40, y, boxW, 480, 20, 2, 80, 80, 80, 255);
        std::string t2 = "Information & Performance";
        int t2x = (W - measure_text(t2, 40)) / 2;
        TextRenderer::draw_text_gradient_random(cv, W, H, t2x, y + 50, 40, t2);
        
        int ry = y + 110;
        draw_row("Operating System:", d.os_name, ry); ry += 45;
        draw_row("Uptime OS:", d.uptime_os, ry); ry += 45;
        draw_row("CPU Model:", d.cpu_model, ry); ry += 45;
        
        draw_row("CPU Usage:", d.cpu_usage, ry);
        draw_bar(cv, W, H, label_x, ry + 15, val_right_x - label_x, 12, d.ram_percent, 255, 200, 50); // Lấy tạm % RAM cho thanh bar vì đã có text %
        ry += 70;
        
        draw_row("Processes:", d.processes, ry); ry += 45;
        
        std::string ramText = d.ram_used + " / " + d.ram_total + " (Free " + d.ram_free + ")";
        draw_row("RAM Usage:", ramText, ry);
        draw_bar(cv, W, H, label_x, ry + 15, val_right_x - label_x, 12, d.ram_percent, 50, 150, 255);
        ry += 70;
        
        std::string diskText = d.disk_used + " / " + d.disk_total + " (Free " + d.disk_free + ")";
        draw_row("Disk Usage:", diskText, ry);
        draw_bar(cv, W, H, label_x, ry + 15, val_right_x - label_x, 12, d.disk_percent, 50, 255, 100);
        
        y += 500;
        
        // === KHUNG 3: NETWORK ===
        TextRenderer::draw_rounded_rect_filled(cv, W, H, 40, y, boxW, 280, 20, 35, 35, 35, 200);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, 40, y, boxW, 280, 20, 2, 80, 80, 80, 255);
        std::string t3 = "Network Activity";
        int t3x = (W - measure_text(t3, 40)) / 2;
        TextRenderer::draw_text_gradient_random(cv, W, H, t3x, y + 50, 40, t3);
        
        ry = y + 110;
        draw_row("Interface Name:", "Ethernet Instance 0", ry); ry += 45;
        draw_row("Driver Name:", "Red Hat VirtIO Ethernet Adapter", ry); ry += 45;
        draw_row("Protocol Type:", "Giao Thức Mạng Ảo", ry); ry += 45;
        draw_row("Total Activity Traffic:", d.net_sent + " (Sent) / " + d.net_recv + " (Received)", ry);

        stbi_write_png(out_path.c_str(), W, H, 4, cv.data(), W * 4);
    }
}
