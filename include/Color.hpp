#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace render::color {

struct RGBA {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    RGBA() = default;
    RGBA(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
};

// Parse "#RRGGBB", "RRGGBB", "#RGB", "rgb(...)" don gian
RGBA from_hex(const std::string& hex);

// h: 0..360, s: 0..100, l: 0..100  (giong CSS hsl)
RGBA from_hsl(float h, float s, float l, uint8_t a = 255);
RGBA from_hsv(float h, float s, float v, uint8_t a = 255);

std::string to_hex(RGBA c);         // "#RRGGBB"
RGBA lerp(RGBA a, RGBA b, float t); // noi suy tuyen tinh

// Tuong duong getRandomColor / getRandomBrightColor / getRandomRainbowColor
RGBA get_random();
RGBA get_random_bright();
RGBA get_random_rainbow();

// ===== Pixel ops tren framebuffer RGBA (W * H * 4) =====
// Alpha blend kieu source-over (nhu ctx.fillStyle + globalAlpha)
void blend_pixel(std::vector<uint8_t>& canvas, int W, int H,
                 int x, int y, RGBA c);

// Ghi de hoan toan (khong blend) - nhu putImageData
void set_pixel(std::vector<uint8_t>& canvas, int W, int H,
               int x, int y, RGBA c);

RGBA get_pixel(const std::vector<uint8_t>& canvas, int W, int H,
               int x, int y);

} // namespace render::color