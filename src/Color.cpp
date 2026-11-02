#include "Color.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <cstdio>

namespace render::color {

static std::mt19937& rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

RGBA from_hex(const std::string& hexIn) {
    std::string s = hexIn;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() == 3) {
        std::string full;
        for (char c : s) { full += c; full += c; }
        s = full;
    }
    if (s.size() != 6) return RGBA(0, 0, 0, 255);
    int r = hex_nibble(s[0]) * 16 + hex_nibble(s[1]);
    int g = hex_nibble(s[2]) * 16 + hex_nibble(s[3]);
    int b = hex_nibble(s[4]) * 16 + hex_nibble(s[5]);
    if (r < 0 || g < 0 || b < 0) return RGBA(0, 0, 0, 255);
    return RGBA((uint8_t)r, (uint8_t)g, (uint8_t)b, 255);
}

RGBA from_hsl(float h, float s, float l, uint8_t a) {
    h = std::fmod(h, 360.0f); if (h < 0) h += 360.0f;
    s /= 100.0f; l /= 100.0f;
    float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;
    float rf, gf, bf;
    if      (h <  60) { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }
    return RGBA(
        (uint8_t)std::round((rf + m) * 255.0f),
        (uint8_t)std::round((gf + m) * 255.0f),
        (uint8_t)std::round((bf + m) * 255.0f), a);
}

RGBA from_hsv(float h, float s, float v, uint8_t a) {
    h = std::fmod(h, 360.0f); if (h < 0) h += 360.0f;
    float c = v * s;
    float x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1));
    float m = v - c;
    float rf, gf, bf;
    if      (h <  60) { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }
    return RGBA((uint8_t)std::round((rf+m)*255),
                (uint8_t)std::round((gf+m)*255),
                (uint8_t)std::round((bf+m)*255), a);
}

std::string to_hex(RGBA c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return std::string(buf);
}

RGBA lerp(RGBA a, RGBA b, float t) {
    if (t < 0) t = 0; if (t > 1) t = 1;
    auto L = [&](uint8_t x, uint8_t y) {
        return (uint8_t)std::round(x + (y - x) * t);
    };
    return RGBA(L(a.r, b.r), L(a.g, b.g), L(a.b, b.b), L(a.a, b.a));
}

RGBA get_random() {
    static const char* hex = "0123456789ABCDEF";
    std::uniform_int_distribution<int> d(0, 15);
    char buf[8] = "#";
    for (int i = 0; i < 6; i++) buf[1 + i] = hex[d(rng())];
    return from_hex(buf);
}

RGBA get_random_bright() {
    static const int hues[] = {60, 120, 180, 200};
    std::uniform_int_distribution<int> dh(0, 3);
    std::uniform_int_distribution<int> ds(95, 100);
    std::uniform_int_distribution<int> dl(80, 85);
    return from_hsl((float)hues[dh(rng())], (float)ds(rng()), (float)dl(rng()));
}

RGBA get_random_rainbow() {
    static const RGBA rainbow[] = {
        RGBA(255,   0,   0), RGBA(255, 127,   0), RGBA(255, 255,   0),
        RGBA(  0, 255,   0), RGBA(  0,   0, 255), RGBA( 75,   0, 130),
        RGBA(148,   0, 211),
    };
    std::uniform_int_distribution<int> d(0, 6);
    return rainbow[d(rng())];
}

void blend_pixel(std::vector<uint8_t>& canvas, int W, int H,
                 int x, int y, RGBA c) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    int idx = (y * W + x) * 4;
    float af = c.a / 255.0f;
    canvas[idx + 0] = (uint8_t)(c.r * af + canvas[idx + 0] * (1 - af));
    canvas[idx + 1] = (uint8_t)(c.g * af + canvas[idx + 1] * (1 - af));
    canvas[idx + 2] = (uint8_t)(c.b * af + canvas[idx + 2] * (1 - af));
    canvas[idx + 3] = 255;
}

void set_pixel(std::vector<uint8_t>& canvas, int W, int H,
               int x, int y, RGBA c) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    int idx = (y * W + x) * 4;
    canvas[idx + 0] = c.r;
    canvas[idx + 1] = c.g;
    canvas[idx + 2] = c.b;
    canvas[idx + 3] = c.a;
}

RGBA get_pixel(const std::vector<uint8_t>& canvas, int W, int H,
               int x, int y) {
    if (x < 0 || x >= W || y < 0 || y >= H) return RGBA(0, 0, 0, 0);
    int idx = (y * W + x) * 4;
    return RGBA(canvas[idx + 0], canvas[idx + 1], canvas[idx + 2], canvas[idx + 3]);
}

} // namespace render::color