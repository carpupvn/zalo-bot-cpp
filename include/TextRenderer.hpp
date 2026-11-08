#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct TextStyle {
    int width       = 1200;
    int padding     = 50;
    int title_size  = 54;
    int text_size   = 34;
    int line_height = 54;

    unsigned char bg_r = 18,  bg_g = 18,  bg_b = 28;
    unsigned char bg2_r = 40, bg2_g = 20, bg2_b = 70;

    unsigned char fg_r = 235, fg_g = 235, fg_b = 245;
    unsigned char accent_r = 88, accent_g = 166, accent_b = 255;
    unsigned char title_r = 255, title_g = 200, title_b = 80;

    unsigned char card_r = 30, card_g = 30, card_b = 45;
    int card_radius = 24;

    std::string font_path       = "assets/BeVietnamPro-Bold.ttf";
    std::string emoji_font_path = "assets/TwemojiMozilla.ttf";
};

struct ImageRGBA;

class TextRenderer {
public:
    static bool init(const std::string& font_path);
    static bool init_emoji(const std::string& emoji_path);

    static bool render(const std::string& title,
                       const std::vector<std::string>& lines,
                       const std::string& output_path,
                       const TextStyle& style);

    static void init_canvas(std::vector<uint8_t>& canvas, int W, int H,
                            unsigned char r, unsigned char g, unsigned char b);

    static void fill_linear_gradient(std::vector<uint8_t>& canvas, int W, int H,
                                      int y_start, int y_end,
                                      unsigned char r1, unsigned char g1, unsigned char b1,
                                      unsigned char r2, unsigned char g2, unsigned char b2);

    static void draw_rect_filled(std::vector<uint8_t>& canvas, int W, int H,
                                 int x, int y, int rw, int rh,
                                 unsigned char r, unsigned char g, unsigned char b,
                                 unsigned char a = 255);

    static void draw_rounded_rect_filled(std::vector<uint8_t>& canvas, int W, int H,
                                          int x, int y, int rw, int rh, int radius,
                                          unsigned char r, unsigned char g, unsigned char b,
                                          unsigned char a = 255);

    static void draw_rounded_rect_outline(std::vector<uint8_t>& canvas, int W, int H,
                                           int x, int y, int rw, int rh, int radius,
                                           int thickness,
                                           unsigned char r, unsigned char g, unsigned char b,
                                           unsigned char a = 255);

    static void draw_circle_filled(std::vector<uint8_t>& canvas, int W, int H,
                                    int cx, int cy, int radius,
                                    unsigned char r, unsigned char g, unsigned char b,
                                    unsigned char a = 255);

    static void draw_circle_image(std::vector<uint8_t>& canvas, int W, int H,
                                   const ImageRGBA* img,
                                   int cx, int cy, int radius);

    static void draw_circle_outline_rainbow(std::vector<uint8_t>& canvas, int W, int H,
                                             int cx, int cy, int radius, int thickness);

    static void draw_line(std::vector<uint8_t>& canvas, int W, int H,
                          int x1, int y1, int x2, int y2,
                          unsigned char r, unsigned char g, unsigned char b,
                          int thickness = 1, unsigned char a = 255);

    static void blur_region(std::vector<uint8_t>& canvas, int W, int H, int strength);

    static void darken_region(std::vector<uint8_t>& canvas, int W, int H,
                               int x, int y, int rw, int rh, unsigned char alpha);

    static void draw_text_on_canvas(std::vector<uint8_t>& canvas, int W, int H,
                                    int x, int y, int size,
                                    unsigned char r, unsigned char g, unsigned char b,
                                    const std::string& text);

    static void draw_text_on_canvas_baseline(std::vector<uint8_t>& canvas, int W, int H,
                                             int x, int y_baseline, int size,
                                             unsigned char r, unsigned char g, unsigned char b,
                                             const std::string& text);

    static void draw_text_shadow(std::vector<uint8_t>& canvas, int W, int H,
                                 int x, int y, int size,
                                 unsigned char r, unsigned char g, unsigned char b,
                                 const std::string& text);

    static int measure_text_width(int size, const std::string& text);

    static void draw_image_rotated(std::vector<uint8_t>& canvas, int W, int H,
                                    const ImageRGBA* img,
                                    int cx, int cy, int size, float angle_rad);

    static void draw_image_on_canvas(std::vector<uint8_t>& canvas, int W, int H,
                                     const ImageRGBA* img,
                                     int x, int y, int dst_w, int dst_h);

    static void draw_text_gradient(std::vector<uint8_t>& canvas, int W, int H,
                                    int x, int y, int size,
                                    unsigned char r1, unsigned char g1, unsigned char b1,
                                    unsigned char r2, unsigned char g2, unsigned char b2,
                                    const std::string& text);

    // Vẽ text với random rainbow gradient (5 stop, giống getRandomGradient trong bot JS)
    static void draw_text_gradient_random(std::vector<uint8_t>& canvas, int W, int H,
                                           int x, int y, int size,
                                           const std::string& text);

    static void get_random_gradient(unsigned char& r1, unsigned char& g1, unsigned char& b1,
                                     unsigned char& r2, unsigned char& g2, unsigned char& b2);

    static bool save_canvas(const std::vector<uint8_t>& canvas, int W, int H,
                            const std::string& path);
};
