#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct TextStyle {
    int width        = 820;
    int padding      = 32;
    int title_size   = 28;
    int text_size    = 20;
    int line_height  = 30;
    int card_radius  = 16;

    unsigned char bg_r  = 18,  bg_g  = 22,  bg_b  = 38;
    unsigned char bg2_r = 12,  bg2_g = 14,  bg2_b = 26;

    unsigned char card_r = 28, card_g = 34, card_b = 58;

    unsigned char title_r = 255, title_g = 215, title_b = 90;
    unsigned char fg_r    = 235, fg_g    = 238, fg_b    = 248;

    unsigned char accent_r = 110, accent_g = 160, accent_b = 255;

    std::string font_path       = "assets/BeVietnamPro-Bold.ttf";
    std::string emoji_font_path = "assets/TwemojiMozilla.ttf";
};

struct ImageRGBA;

class TextRenderer {
public:
    static bool init(const std::string& font_path);
    static bool init_emoji(const std::string& emoji_path);
    static bool init_alt_font(const std::string& alt_font_path);

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

    static void draw_rounded_rect_shadow(std::vector<uint8_t>& canvas, int W, int H,
                                         int x, int y, int rw, int rh, int radius,
                                         int blur_px = 14, unsigned char max_alpha = 90);

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

    static void draw_text_outlined(std::vector<uint8_t>& canvas, int W, int H,
                                   int x, int y, int size,
                                   unsigned char r, unsigned char g, unsigned char b,
                                   unsigned char or_, unsigned char og, unsigned char ob,
                                   int outline, const std::string& text);

    static void draw_text_alt_outlined(std::vector<uint8_t>& canvas, int W, int H,
                                       int x, int y, int size,
                                       unsigned char r1, unsigned char g1, unsigned char b1,
                                       unsigned char r2, unsigned char g2, unsigned char b2,
                                       unsigned char or_, unsigned char og, unsigned char ob,
                                       int outline, const std::string& text);

    static int measure_text_width(int size, const std::string& text);
    static int measure_alt_text_width(int size, const std::string& text);
    static std::vector<std::string> wrap_text(int size, const std::string& text, int max_width);

    static void draw_image_rotated(std::vector<uint8_t>& canvas, int W, int H,
                                   const ImageRGBA* img,
                                   int cx, int cy, int size, float angle_rad);

    static void draw_image_on_canvas(std::vector<uint8_t>& canvas, int W, int H,
                                     const ImageRGBA* img,
                                     int x, int y, int dst_w, int dst_h);

    static void draw_image_aspect_fit(std::vector<uint8_t>& canvas, int W, int H,
                                      const ImageRGBA* img,
                                      int x, int y, int box_w, int box_h);

    static void draw_image_aspect_cover(std::vector<uint8_t>& canvas, int W, int H,
                                        const ImageRGBA* img,
                                        int x, int y, int box_w, int box_h);

    static void draw_text_gradient(std::vector<uint8_t>& canvas, int W, int H,
                                   int x, int y, int size,
                                   unsigned char r1, unsigned char g1, unsigned char b1,
                                   unsigned char r2, unsigned char g2, unsigned char b2,
                                   const std::string& text);

    static void draw_text_gradient_random(std::vector<uint8_t>& canvas, int W, int H,
                                          int x, int y, int size,
                                          const std::string& text);

    static void get_random_gradient(unsigned char& r1, unsigned char& g1, unsigned char& b1,
                                    unsigned char& r2, unsigned char& g2, unsigned char& b2);

    static bool save_canvas(const std::vector<uint8_t>& canvas, int W, int H,
                            const std::string& path);
};
