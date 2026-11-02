#include "Gradient.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace render::gradient {

using render::color::RGBA;
using render::color::lerp;

LinearGradient::LinearGradient(float x0, float y0, float x1, float y1)
    : x0_(x0), y0_(y0), x1_(x1), y1_(y1) {}

void LinearGradient::add_stop(float offset, RGBA color) {
    stops_.push_back({ offset, color });
}

void LinearGradient::clear() { stops_.clear(); }

RGBA LinearGradient::sample(float t) const {
    if (stops_.empty()) return RGBA(0, 0, 0, 255);
    if (t <= stops_.front().offset) return stops_.front().color;
    if (t >= stops_.back().offset)  return stops_.back().color;
    for (size_t i = 0; i + 1 < stops_.size(); i++) {
        const auto& a = stops_[i];
        const auto& b = stops_[i + 1];
        if (t >= a.offset && t <= b.offset) {
            float span = b.offset - a.offset;
            float local = (span > 1e-6f) ? (t - a.offset) / span : 0.0f;
            return lerp(a.color, b.color, local);
        }
    }
    return stops_.back().color;
}

RGBA LinearGradient::sample_at(float px, float py) const {
    float dx = x1_ - x0_;
    float dy = y1_ - y0_;
    float len2 = dx*dx + dy*dy;
    if (len2 < 1e-6f) return sample(0.0f);
    float t = ((px - x0_) * dx + (py - y0_) * dy) / len2;
    return sample(t);
}

void LinearGradient::fill(std::vector<uint8_t>& canvas, int W, int H) const {
    fill_rect(canvas, W, H, 0, 0, W, H);
}

void LinearGradient::fill_rect(std::vector<uint8_t>& canvas, int W, int H,
                                int rx, int ry, int rw, int rh) const {
    for (int y = ry; y < ry + rh; y++) {
        if (y < 0 || y >= H) continue;
        for (int x = rx; x < rx + rw; x++) {
            if (x < 0 || x >= W) continue;
            render::color::set_pixel(canvas, W, H, x, y,
                                      sample_at((float)x, (float)y));
        }
    }
}

RadialGradient::RadialGradient(float cx, float cy, float radius)
    : cx_(cx), cy_(cy), radius_(radius) {}

void RadialGradient::add_stop(float offset, RGBA color) {
    stops_.push_back({ offset, color });
}

RGBA RadialGradient::sample(float t) const {
    if (stops_.empty()) return RGBA(0, 0, 0, 255);
    if (t <= stops_.front().offset) return stops_.front().color;
    if (t >= stops_.back().offset)  return stops_.back().color;
    for (size_t i = 0; i + 1 < stops_.size(); i++) {
        const auto& a = stops_[i];
        const auto& b = stops_[i + 1];
        if (t >= a.offset && t <= b.offset) {
            float span = b.offset - a.offset;
            float local = (span > 1e-6f) ? (t - a.offset) / span : 0.0f;
            return lerp(a.color, b.color, local);
        }
    }
    return stops_.back().color;
}

void RadialGradient::fill(std::vector<uint8_t>& canvas, int W, int H) const {
    float r2 = radius_ * radius_;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float dx = x - cx_;
            float dy = y - cy_;
            float d2 = dx*dx + dy*dy;
            float t = (r2 > 1e-6f) ? std::sqrt(d2 / r2) : 1.0f;
            render::color::set_pixel(canvas, W, H, x, y, sample(t));
        }
    }
}

namespace presets {

LinearGradient pink_white(float width) {
    LinearGradient g(0, 0, width, 0);
    g.add_stop(0.0f, render::color::from_hex("#FFC0CB"));
    g.add_stop(0.5f, render::color::from_hex("#FFB6C1"));
    g.add_stop(1.0f, render::color::from_hex("#FFFFFF"));
    return g;
}

LinearGradient blue_black(float width) {
    LinearGradient g(0, 0, width, 0);
    g.add_stop(0.0f, render::color::from_hex("#000051"));
    g.add_stop(0.5f, render::color::from_hex("#000051"));
    g.add_stop(1.0f, render::color::from_hex("#000000"));
    return g;
}

LinearGradient dark(float width) {
    LinearGradient g(0, 0, width, 0);
    g.add_stop(0.0f, render::color::from_hex("#000000"));
    g.add_stop(1.0f, render::color::from_hex("#434343"));
    return g;
}

LinearGradient random_rainbow(float width) {
    static const char* rainbow[] = {
        "#FF0000", "#FF7F00", "#FFFF00", "#00FF00",
        "#0000FF", "#4B0082", "#9400D3"
    };
    std::vector<std::string> cols(rainbow, rainbow + 7);
    std::shuffle(cols.begin(), cols.end(), std::mt19937(std::random_device{}()));
    LinearGradient g(0, 0, width, 0);
    for (size_t i = 0; i < cols.size(); i++) {
        g.add_stop((float)i / (float)(cols.size() - 1),
                   render::color::from_hex(cols[i]));
    }
    return g;
}

LinearGradient random_bright(float width) {
    LinearGradient g(0, 0, width, 0);
    for (float i = 0.0f; i <= 1.0f; i += 0.25f) {
        g.add_stop(i, render::color::get_random_bright());
    }
    return g;
}

} // namespace presets
} // namespace render::gradient