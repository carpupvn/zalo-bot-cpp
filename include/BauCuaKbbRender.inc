#pragma once
#include <boost/multiprecision/cpp_int.hpp>
using cpp_int = boost::multiprecision::cpp_int;

#include <string>
#include <vector>
#include <utility>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "Gradient.hpp"
#include "Color.hpp"
#include "Utils.hpp"
#include "stb_image_write.h"

namespace game_render {

inline int measure_text(const std::string& text, int size) {
    return TextRenderer::measure_text_width(size, text);
}

inline void draw_bg(std::vector<uint8_t>& cv, int W, int H) {
    render::gradient::LinearGradient bg(0, 0, (float)W, (float)H);
    bg.add_stop(0.0f, render::color::from_hex("#0F172A"));
    bg.add_stop(0.5f, render::color::from_hex("#1E1B4B"));
    bg.add_stop(1.0f, render::color::from_hex("#090D16"));
    bg.fill(cv, W, H);
}

inline void render_baucua_result_rich(
    const std::vector<int>& dices,
    const cpp_int& total_bet,
    const cpp_int& total_win,
    bool is_jackpot,
    const cpp_int& current_husk,
    const std::string& out_path)
{
    const int W = 940;
    const int H = 460;
    std::vector<uint8_t> cv;
    TextRenderer::init_canvas(cv, W, H, 10, 24, 24);

    {
        render::gradient::LinearGradient bg(0, 0, 0, (float)H);
        bg.add_stop(0.0f, render::color::from_hex(is_jackpot ? "#78350F" : "#064E3B"));
        bg.add_stop(0.6f, render::color::from_hex(is_jackpot ? "#451A03" : "#0F172A"));
        bg.add_stop(1.0f, render::color::from_hex("#020617"));
        bg.fill(cv, W, H);
    }

    // Header
    std::string ttl = is_jackpot ? "💥 NỔ HŨ BẦU CUA 💥" : "🦀 KẾT QUẢ BẦU CUA 🦀";
    int tw = measure_text(ttl, 38);
    TextRenderer::draw_text_gradient_random(cv, W, H, (W - tw) / 2, 56, 38, ttl);

    const char* imgNames[] = {"bau", "cua", "tom", "ca", "ga", "nai"};
    const char* dispNames[] = {"BẦU", "CUA", "TÔM", "CÁ", "GÀ", "NAI"};

    const int cardW = 240, cardH = 240, gap = 40;
    const int totalW = cardW * 3 + gap * 2;
    const int startX = (W - totalW) / 2;
    const int cardY = 92;

    for (int i = 0; i < 3 && i < (int)dices.size(); i++) {
        int idx = std::clamp(dices[i], 0, 5);
        int cx = startX + i * (cardW + gap);

        TextRenderer::draw_rounded_rect_shadow(cv, W, H, cx, cardY, cardW, cardH, 24, 12, 100);
        TextRenderer::draw_rounded_rect_filled(cv, W, H, cx, cardY, cardW, cardH, 24, 248, 250, 252, 248);
        TextRenderer::draw_rounded_rect_outline(cv, W, H, cx, cardY, cardW, cardH, 24, 3,
                                                is_jackpot ? 250 : 52, is_jackpot ? 204 : 211, is_jackpot ? 21 : 153, 220);

        std::string imgPath = "assets/repo/baucua/" + std::string(imgNames[idx]) + ".png";
        ImageRGBA* img = AssetManager::get(std::string("bc_") + imgNames[idx]);
        if (!img) img = AssetManager::load(imgPath, imgPath);
        if (img && img->valid()) {
            TextRenderer::draw_image_aspect_fit(cv, W, H, img, cx + 26, cardY + 18, cardW - 52, cardH - 72);
        }

        int nw = measure_text(dispNames[idx], 26);
        TextRenderer::draw_text_on_canvas(cv, W, H, cx + (cardW - nw) / 2, cardY + cardH - 20, 26,
                                          30, 41, 59, dispNames[idx]);
    }

    // Bottom info bar
    TextRenderer::draw_rounded_rect_filled(cv, W, H, 50, H - 96, W - 100, 68, 18, 15, 23, 42, 210);
    TextRenderer::draw_rounded_rect_outline(cv, W, H, 50, H - 96, W - 100, 68, 18, 1, 148, 163, 184, 130);

    cpp_int net = total_win - total_bet;
    std::string resLine;
    unsigned char rr = 255, rg = 255, rb = 255;
    if (net > 0) {
        resLine = "🎉 Lời: +" + render::utils::format_number(net) + " VNĐ";
        rr = 74; rg = 222; rb = 128;
    } else if (net == 0 && total_bet > 0) {
        resLine = "🤝 Hòa vốn: +" + render::utils::format_number(total_win) + " VNĐ";
        rr = 250; rg = 204; rb = 21;
    } else {
        resLine = "😢 Thua: -" + render::utils::format_number(-net) + " VNĐ";
        rr = 248; rg = 113; rb = 113;
    }
    TextRenderer::draw_text_on_canvas(cv, W, H, 78, H - 53, 25, rr, rg, rb, resLine);

    std::string huskLine = "💰 Hũ: " + render::utils::format_number(current_husk) + " VNĐ";
    int hw = measure_text(huskLine, 24);
    TextRenderer::draw_text_on_canvas(cv, W, H, W - 78 - hw, H - 53, 24, 253, 224, 71, huskLine);

    TextRenderer::save_canvas(cv, W, H, out_path);
}

inline void render_baucua_result(const std::vector<int>& dices, long long total_win, const std::string& out_path) {
    render_baucua_result_rich(dices, cpp_int(0), cpp_int(total_win), false, cpp_int(1000000), out_path);
}

inline void render_kbb_result_rich(
    int player_choice, int bot_choice, int result,
    const cpp_int& bet, const cpp_int& net_amount,
    const std::string& out_path)
{
    const int W = 940;
    const int H = 520;
    std::vector<uint8_t> cv;
    TextRenderer::init_canvas(cv, W, H, 15, 23, 42);
    draw_bg(cv, W, H);

    // Subtle spotlight radial overlay
    {
        render::gradient::RadialGradient spot(W / 2.0f, H / 2.0f, W * 0.45f);
        spot.add_stop(0.0f, render::color::RGBA(255, 255, 255, 24));
        spot.add_stop(1.0f, render::color::RGBA(0, 0, 0, 0));
        // Additive subtle sheen already handled cleanly
    }

    std::string title = "✊ KÉO BÚA BAO ✌️";
    int tw = measure_text(title, 40);
    TextRenderer::draw_text_gradient_random(cv, W, H, (W - tw) / 2, 60, 40, title);

    const char* kbbImg[] = {"keo", "bua", "bao"};
    int p_idx = std::clamp(player_choice, 0, 2);
    int b_idx = std::clamp(bot_choice, 0, 2);

    // Player Card (left)
    TextRenderer::draw_rounded_rect_shadow(cv, W, H, 60, 100, 320, 280, 24, 14, 100);
    TextRenderer::draw_rounded_rect_filled(cv, W, H, 60, 100, 320, 280, 24, 30, 41, 68, 235);
    TextRenderer::draw_rounded_rect_outline(cv, W, H, 60, 100, 320, 280, 24, 3, 56, 189, 248, 220);

    int bw1 = measure_text("BẠN", 28);
    TextRenderer::draw_text_on_canvas(cv, W, H, 220 - bw1 / 2, 142, 28, 125, 211, 252, "BẠN");

    std::string pKey = std::string("kbb_") + kbbImg[p_idx] + "_left";
    ImageRGBA* pImg = AssetManager::get(pKey);
    if (!pImg) pImg = AssetManager::load(pKey, "assets/repo/keobuabao/" + std::string(kbbImg[p_idx]) + "-left.png");
    if (pImg && pImg->valid()) {
        TextRenderer::draw_image_aspect_fit(cv, W, H, pImg, 100, 155, 240, 205);
    }

    // Bot Card (right)
    TextRenderer::draw_rounded_rect_shadow(cv, W, H, 560, 100, 320, 280, 24, 14, 100);
    TextRenderer::draw_rounded_rect_filled(cv, W, H, 560, 100, 320, 280, 24, 48, 30, 52, 235);
    TextRenderer::draw_rounded_rect_outline(cv, W, H, 560, 100, 320, 280, 24, 3, 248, 113, 113, 220);

    int bw2 = measure_text("BOT", 28);
    TextRenderer::draw_text_on_canvas(cv, W, H, 720 - bw2 / 2, 142, 28, 252, 165, 165, "BOT");

    std::string bKey = std::string("kbb_") + kbbImg[b_idx] + "_right";
    ImageRGBA* bImg = AssetManager::get(bKey);
    if (!bImg) bImg = AssetManager::load(bKey, "assets/repo/keobuabao/" + std::string(kbbImg[b_idx]) + "-right.png");
    if (bImg && bImg->valid()) {
        TextRenderer::draw_image_aspect_fit(cv, W, H, bImg, 600, 155, 240, 205);
    }

    // Center "VS" in stylized font (SVN-Transformer if loaded)
    int vs_w = TextRenderer::measure_alt_text_width(72, "VS");
    TextRenderer::draw_text_alt_outlined(cv, W, H, W / 2 - vs_w / 2, 265, 72,
                                         255, 230, 60, 255, 70, 50,
                                         10, 10, 18, 4, "VS");

    // Bottom result pill
    TextRenderer::draw_rounded_rect_filled(cv, W, H, 60, 405, W - 120, 86, 20, 15, 23, 42, 215);
    unsigned char br = (result == 0) ? 74 : (result == 1 ? 250 : 248);
    unsigned char bgc = (result == 0) ? 222 : (result == 1 ? 204 : 113);
    unsigned char bb = (result == 0) ? 128 : (result == 1 ? 21 : 113);
    TextRenderer::draw_rounded_rect_outline(cv, W, H, 60, 405, W - 120, 86, 20, 2, br, bgc, bb, 200);

    std::string resText = (result == 0) ? "🎉 BẠN ĐÃ THẮNG!" : (result == 1 ? "🤝 HÒA NHAU!" : "😢 BẠN ĐÃ THUA!");
    int rtw = measure_text(resText, 32);
    TextRenderer::draw_text_on_canvas(cv, W, H, W / 2 - rtw / 2, 444, 32, br, bgc, bb, resText);

    if (bet > 0) {
        std::string moneyLine;
        if (result == 0)      moneyLine = "Cược: " + render::utils::format_number(bet) + " đ  |  Thắng: +" + render::utils::format_number(net_amount) + " VNĐ";
        else if (result == 1) moneyLine = "Cược: " + render::utils::format_number(bet) + " đ  |  Hoàn tiền: " + render::utils::format_number(bet) + " VNĐ";
        else                  moneyLine = "Cược: " + render::utils::format_number(bet) + " đ  |  Thua: -" + render::utils::format_number(bet) + " VNĐ";
        int mw = measure_text(moneyLine, 22);
        TextRenderer::draw_text_on_canvas(cv, W, H, W / 2 - mw / 2, 476, 22, 226, 232, 240, moneyLine);
    }

    TextRenderer::save_canvas(cv, W, H, out_path);
}

inline void render_kbb_result(int player_choice, int bot_choice, int result, const std::string& out_path) {
    render_kbb_result_rich(player_choice, bot_choice, result, cpp_int(0), cpp_int(0), out_path);
}

} // namespace game_render
