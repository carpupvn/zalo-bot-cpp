#include "Background.hpp"
#include "Color.hpp"
#include "Gradient.hpp"
#include "Shape.hpp"
#include <cmath>
#include <random>
#include <ctime>

namespace render::background {

using render::color::RGBA;
using render::color::blend_pixel;

static std::mt19937& rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}

void animated(std::vector<uint8_t>& canvas, int W, int H) {
    double time = (double)std::time(nullptr) * 0.001;
    for (int i = 0; i < 100; i++) {
        double x = std::sin(i * 0.1 + time) * W * 0.5 + W * 0.5;
        double y = std::cos(i * 0.1 + time) * H * 0.5 + H * 0.5;
        double radius = std::sin(i * 0.05 + time) * 2 + 3;
        double alpha = 0.1 + std::sin((double)i) * 0.05;
        if (alpha < 0) alpha = 0;
        int cx = (int)x, cy = (int)y, r = (int)radius;
        RGBA c(255, 255, 255, (uint8_t)(alpha * 255));
        for (int py = cy - r; py <= cy + r; py++) {
            for (int px = cx - r; px <= cx + r; px++) {
                int dx = px - cx, dy = py - cy;
                if (dx*dx + dy*dy <= r*r)
                    blend_pixel(canvas, W, H, px, py, c);
            }
        }
    }
}

static void particles(std::vector<uint8_t>& canvas, int W, int H, int count) {
    std::uniform_real_distribution<float> dist_x(0.0f, (float)W);
    std::uniform_real_distribution<float> dist_y(0.0f, (float)H);
    std::uniform_real_distribution<float> dist_r(2.0f, 5.0f);
    std::uniform_real_distribution<float> dist_a(0.5f, 1.0f);
    for (int i = 0; i < count; i++) {
        int x = (int)dist_x(rng());
        int y = (int)dist_y(rng());
        int r = (int)dist_r(rng());
        uint8_t a = (uint8_t)(dist_a(rng()) * 255);
        RGBA c(255, 255, 255, a);
        for (int py = y - r; py <= y + r; py++)
            for (int px = x - r; px <= x + r; px++) {
                int dx = px - x, dy = py - y;
                if (dx*dx + dy*dy <= r*r)
                    blend_pixel(canvas, W, H, px, py, c);
            }
    }
}

void gradient_green(std::vector<uint8_t>& canvas, int W, int H) {
    render::gradient::LinearGradient g(0, 0, (float)W, (float)H);
    g.add_stop(0.0f, render::color::from_hex("#0097b2"));
    g.add_stop(1.0f, render::color::from_hex("#7ed957"));
    g.fill(canvas, W, H);
    particles(canvas, W, H, 50);
}

void gradient_green_next(std::vector<uint8_t>& canvas, int W, int H) {
    render::gradient::LinearGradient g(0, 0, (float)W, (float)H);
    g.add_stop(0.0f, render::color::from_hex("#7ed957"));
    g.add_stop(1.0f, render::color::from_hex("#0097b2"));
    g.fill(canvas, W, H);
    particles(canvas, W, H, 50);
}

void dynamic_gradient(std::vector<uint8_t>& canvas, int W, int H) {
    static const char* cols[] = {
        "#6a3093", "#a044ff", "#5f2c82", "#49a09d", "#5f2c82"
    };
    render::gradient::LinearGradient g(0, 0, (float)W, (float)H);
    for (int i = 0; i < 5; i++) {
        g.add_stop((float)i / 4.0f, render::color::from_hex(cols[i]));
    }
    g.fill(canvas, W, H);
    particles(canvas, W, H, 50);
}

} // namespace render::background