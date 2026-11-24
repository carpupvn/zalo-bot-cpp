#include <windows.h>
#include <shellapi.h>
#include "WordChain.hpp"
#include "VietnameseUtils.hpp"
#include <deque>
#include <unordered_map>
#include <mutex>
#include "AssetManager.hpp"
#include "CommandHandler.hpp"
#include "NongTraiManager.hpp"
#include "random.h"

// Global pointer - set tu main.cpp
NongTraiManager* g_farm_mgr = nullptr;
#include "RuntimeState.hpp"
#include "TextRenderer.hpp"
#include "Color.hpp"
#include "Gradient.hpp"
#include "Shape.hpp"
#include "EmojiRenderer.hpp"
#include "Utils.hpp"
#include "GameRender.inc"
#include "TaiXiuRender.inc"
#include "AdminReply.inc"
#include <boost/multiprecision/cpp_int.hpp>
#include <filesystem>
#include "ProfileRender.inc"
#include "BotInfoRender.inc"
#include "NongTraiRender.inc"
#include "ShopRender.inc"
#include "BauCuaKbbRender.inc"
#include "ExtraRender.inc"
#include "SystemInfo.hpp"
#include <random>
#include <ctime>
#include "json.hpp"
#include "paths.hpp"
#include "SafeMemory.hpp"
#include <sstream>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cctype>
#include <atomic>
#include <set>
#include <thread>
#include <chrono>
#include <cmath>

namespace {
    WordChainEngine& getWordChain() {
        static WordChainEngine wc;
        static std::once_flag flag;
        std::call_once(flag, []() {
            wc.loadDictionary(paths::data("dictionary.txt").c_str());
        });
        return wc;
    }
}
using json = nlohmann::json;

// Thread-local context: moi thread co 1 con tro toi entry rieng trong map.
// Con tro la POD -> khong co destructor -> KHONG bi bug MinGW thread_local std::string.
static std::unordered_map<std::thread::id, CommandHandler::Context> g_ctx_map;
static std::mutex g_ctx_map_mtx;

CommandHandler::Context& CommandHandler::ctx() {
    thread_local CommandHandler::Context* local = nullptr;
    if (!local) {
        std::lock_guard<std::mutex> lk(g_ctx_map_mtx);
        local = &g_ctx_map[std::this_thread::get_id()];
    }
    return *local;
}

// Hàm cắt chuỗi an toàn, không cắt giữa các ký tự UTF-8 (như emoji)
static std::string safe_utf8_truncate(const std::string& str, size_t max_bytes) {
    if (str.length() <= max_bytes) return str;
    size_t len = max_bytes;
    // Lùi lại nếu đang cắt ngay giữa một ký tự UTF-8 (byte có dạng 10xxxxxx)
    while (len > 0 && (static_cast<unsigned char>(str[len]) & 0xC0) == 0x80) {
        len--;
    }
    return str.substr(0, len);
}






// ============================================================
// Port từ bot JS (help.js) — render ảnh help
// ============================================================
static std::mt19937& game_rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}

static int roll_dice_mt(int sides) {
    if (sides <= 0) return 0;
    std::uniform_int_distribution<int> d(1, sides);
    return d(game_rng());
}

static cpp_int parse_money(const std::string& s_in, cpp_int max_val = -1) {
    std::string s = s_in;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    
    std::string num_part = "";
    std::string suffix_part = "";
    
    // Tách phần số và phần chữ cái (k, m, b)
    for (char c : s) {
        if (std::isdigit(c) || c == '.' || c == ',') {
            num_part += c;
        } else if (c == 'k' || c == 'm' || c == 'b') {
            suffix_part += c;
        }
    }
    
    cpp_int multiplier = 1;
    if (!suffix_part.empty()) {
        // Nhân dồn các hậu tố: kb = 1000 * 1,000,000,000
        for (char c : suffix_part) {
            if (c == 'k') multiplier *= 1000;
            else if (c == 'm') multiplier *= 1000000;
            else if (c == 'b') multiplier *= 1000000000;
        }
    } else {
        // Không có hậu tố, xóa dấu chấm/phẩy phân cách hàng nghìn (vd: 1.000.000)
        num_part.erase(std::remove_if(num_part.begin(), num_part.end(), [](char c){ return c == '.' || c == ','; }), num_part.end());
    }
    
    cpp_int final_res = 0;
    if (num_part.empty()) return 0;
    
    if (suffix_part.empty()) {
        try { final_res = cpp_int(num_part); } catch (...) { return 0; }
    } else {
        // Xử lý dấu thập phân (vd: 1.5m)
        std::replace(num_part.begin(), num_part.end(), ',', '.');
        size_t dot_pos = num_part.find('.');
        
        if (dot_pos == std::string::npos) {
            try { final_res = cpp_int(num_part) * multiplier; } catch (...) { return 0; }
        } else {
            std::string int_str = num_part.substr(0, dot_pos);
            std::string frac_str = num_part.substr(dot_pos + 1);
            if (int_str.empty()) int_str = "0";
            
            try {
                final_res = cpp_int(int_str) * multiplier;
                if (!frac_str.empty()) {
                    cpp_int frac_val = cpp_int(frac_str);
                    cpp_int frac_div = 1;
                    for (size_t i = 0; i < frac_str.length(); ++i) frac_div *= 10;
                    final_res += (frac_val * multiplier) / frac_div;
                }
            } catch (...) { return 0; }
        }
    }
    
    if (max_val >= 0 && final_res > max_val) return max_val;
    return final_res;
}

static cpp_int parse_bet_amount(const std::string& s, const cpp_int& balance) {
    if (s.empty()) return cpp_int(-1);
    std::string t;
    for (char c : s) t += (char)std::tolower(static_cast<unsigned char>(c));
    if (t == "all" || t == "allin" || t == "het" || t == "hết") {
        return (balance > 0) ? balance : cpp_int(-1);
    }
    if (!t.empty() && t.back() == 0x25) {
        try {
            int pct = std::stoi(t.substr(0, t.size() - 1));
            if (pct <= 0 || pct > 100) return cpp_int(-1);
            return balance * pct / 100;
        } catch (...) { return cpp_int(-1); }
    }
    return parse_money(s, cpp_int(-1));
}

static std::string money_str(const cpp_int& n) {
    std::string s = n.convert_to<std::string>();
    bool neg = (!s.empty() && s[0] == '-');
    std::string digits = neg ? s.substr(1) : s;
    std::string out;
    int cnt = 0;
    for (int i = (int)digits.size() - 1; i >= 0; i--) {
        out.push_back(digits[i]);
        cnt++;
        if (cnt % 3 == 0 && i > 0) out.push_back('.');
    }
    if (neg) out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

static std::vector<bool> g_chanle_history;
static std::vector<bool> g_taixiu_history;

// ============================================================
// Debug mode state
// ============================================================
static bool g_debug_mode = false;
static bool g_debug_saved_bot = false;
static bool g_debug_saved_game = false;
static std::string g_debug_uid = "";
static const long long DEBUG_BANK_LL = 1000000000LL;
// ============================================================
// Taixiu session state (per-thread)
// ============================================================
struct TaixiuPlayer {
    std::string uid;
    std::string name;
    std::string bet_type;  // "tai" hoac "xiu"
    cpp_int amount = 0;
};

struct TaixiuSession {
    bool active = false;
    std::time_t start_time = 0;
    std::time_t end_time = 0;      // start + 60s
    std::time_t last_update = 0;    // cho update 10s
    std::time_t idle_since = 0;     // khi khong co player
    std::vector<TaixiuPlayer> players;

    // Track tin nhan "waiting" de undo truoc khi gui update moi (moi 10s)
    std::string waiting_msg_id;
    std::string waiting_cli_msg_id;
};

static std::map<std::string, TaixiuSession> g_taixiu_sessions;
static std::vector<std::pair<int, bool>> g_taixiu_history_pts; // (total 3..18, is_tai)
static std::mutex g_taixiu_mtx;

// Husks - load tu data/husks.json
static std::map<std::string, cpp_int> g_husks;
static std::mutex g_husks_mtx;

// ============================================================
// Spam / Scold background
// ============================================================
static std::atomic<bool> g_spam_active{false};
static std::atomic<bool> g_scold_active{false};
static std::vector<std::string> g_scold_list;

static int levenshtein(const std::string& s1, const std::string& s2) {
    int m = s1.size(), n = s2.size();
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));
    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;
    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            if (s1[i - 1] == s2[j - 1]) dp[i][j] = dp[i - 1][j - 1];
            else dp[i][j] = std::min({dp[i - 1][j - 1], dp[i - 1][j], dp[i][j - 1]}) + 1;
        }
    }
    return dp[m][n];
}
struct MiniGameState {
    std::string type; // "guessNumber", "wordChain", "wordGuess"
    int targetNumber = 0;
    int attempts = 0;
    int range = 0;
    int range_min = 1;
    int range_max = 1000;
    int maxAttempts = 0;
    std::string lastPhrase = "";
    std::string targetWord = "";
    bool active = false;
};
static std::map<std::string, MiniGameState> g_mini_games;
static void push_history(std::vector<bool>& h, bool v) {
    h.push_back(v);
    if (h.size() > 8) h.erase(h.begin());
}

static void draw_history_dots(std::vector<uint8_t>& canvas, int W, int H,
                               int x, int y, const std::vector<bool>& hist) {
    TextRenderer::draw_text_on_canvas(canvas, W, H, x, y + 3, 16, 180, 190, 210, "Lich su:");
    int cx0 = x + 75;
    int cy  = y + 10;
    for (size_t i = 0; i < hist.size() && i < 8; i++) {
        int cx = cx0 + (int)i * 20;
        if (hist[i]) {
            TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, 7, 100, 230, 130, 255);
        } else {
            TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, 7, 80, 80, 110, 255);
        }
    }
}

static void render_game_image(
    const std::string& title,        // "CHAN" / "LE" / "TAI" / "XIU"
    const std::string& dice_keys_csv,// "dice_4" hoặc "dice_1,dice_5,dice_3"
    int total,                       // dice value or sum
    const std::string& label_total,  // "Tong"
    bool won,
    cpp_int bet, cpp_int payout,
    cpp_int bal_before, cpp_int bal_after,
    const std::string& history_label,
    const std::vector<bool>& history,
    const std::string& out_path)
{
    const int W = 780;
    const int H = 230;

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 13, 27, 42);

    render::gradient::LinearGradient bg(0, 0, (float)W, (float)H);
    bg.add_stop(0.0f, render::color::from_hex("#0d1b2a"));
    bg.add_stop(1.0f, render::color::from_hex("#1b263b"));
    bg.fill(canvas, W, H);

    // Box dice bên trái
    TextRenderer::draw_rounded_rect_filled(canvas, W, H, 25, 25, 180, 180, 20,
                                             255, 255, 255, 25);
    TextRenderer::draw_rounded_rect_outline(canvas, W, H, 25, 25, 180, 180, 20,
                                              2, 120, 150, 200, 200);

    // Dice PNG — parse csv
    std::vector<std::string> keys;
    std::string cur;
    for (char c : dice_keys_csv) {
        if (c == ',') { if (!cur.empty()) keys.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) keys.push_back(cur);

    int n = (int)keys.size();
    if (n == 1) {
        ImageRGBA* im = AssetManager::get(keys[0]);
        if (im && im->valid())
            TextRenderer::draw_image_on_canvas(canvas, W, H, im, 50, 50, 130, 130);
    } else if (n == 2) {
        for (int i = 0; i < 2; i++) {
            ImageRGBA* im = AssetManager::get(keys[i]);
            if (im && im->valid()) {
                int px = (i == 0) ? 45 : 105;
                int py = (i == 0) ? 45 : 105;
                TextRenderer::draw_image_on_canvas(canvas, W, H, im, px, py, 90, 90);
            }
        }
    } else {
        // 3 dice — xếp chồng
        for (int i = 0; i < n && i < 3; i++) {
            ImageRGBA* im = AssetManager::get(keys[i]);
            if (im && im->valid()) {
                int px = 50 + i * 40;
                int py = 60 + (i == 1 ? 20 : 0);
                TextRenderer::draw_image_on_canvas(canvas, W, H, im, px, py, 80, 80);
            }
        }
    }

    // Kết quả lớn (màu xanh nếu thắng, hồng nếu thua)
    unsigned char tr = won ? 100 : 255;
    unsigned char tg = won ? 230 : 140;
    unsigned char tb = won ? 130 : 140;
    TextRenderer::draw_text_shadow(canvas, W, H, 230, 65, 46, tr, tg, tb, title);

    // Tổng
    TextRenderer::draw_text_on_canvas(canvas, W, H, 230, 125, 22, 255, 255, 255,
        label_total + ": " + std::to_string(total));

    // Thắng / thua
    if (won) {
        TextRenderer::draw_text_on_canvas(canvas, W, H, 230, 160, 22, 255, 200, 80,
            "Thắng: +" + money_str(payout) + " VNĐ");
    } else {
        TextRenderer::draw_text_on_canvas(canvas, W, H, 230, 160, 22, 255, 120, 120,
            "Thua: -" + money_str(bet) + " VNĐ");
    }

    // Số dư
    TextRenderer::draw_text_on_canvas(canvas, W, H, 230, 190, 18, 180, 200, 230,
        "Số dư: " + money_str(bal_before) + " -> " + money_str(bal_after) + " VNĐ");

    // Lịch sử
    draw_history_dots(canvas, W, H, 490, 145, history);

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

struct HelpRow {
    std::string icon;
    std::string cmd;
    std::string desc;
    bool implemented = true;   // false = badge "[sắp có]"
};

struct HelpSection {
    std::string header;        // "" = không hiện header
    std::vector<HelpRow> rows;
};

static void render_help_image(
    const std::string& title,
    const std::vector<HelpSection>& sections,
    const std::string& footer_left,
    const std::string& footer_right,
    const std::string& out_path)
{
    const int W = 920;
    const int PAD = 28;
    const int INNER_X = PAD + 26;
    const int TITLE_SIZE = 32;
    const int HEADER_SIZE = 24;
    const int TEXT_SIZE = 22;
    const int ICON_SIZE = 26;
    const int ICON_GAP = 10;
    const int ROW_STEP = 38;
    const int SUB_STEP = 30;
    const int MAX_RIGHT = W - PAD - 24;

    struct RowLayout {
        int text_x = 0;
        std::string icon;
        std::string cmd;
        int cmd_w = 0;
        std::vector<std::string> desc_lines;
        bool implemented = true;
    };
    struct SecLayout {
        std::string header;
        std::vector<RowLayout> rows;
    };

    std::vector<SecLayout> layout;
    int y = PAD + 68;
    for (const auto& sec : sections) {
        SecLayout sl;
        sl.header = sec.header;
        if (!sec.header.empty()) y += 44;
        for (const auto& r : sec.rows) {
            RowLayout rl;
            rl.icon = r.icon;
            rl.cmd = r.cmd;
            rl.implemented = r.implemented;
            rl.text_x = INNER_X + (r.icon.empty() ? 0 : (ICON_SIZE + ICON_GAP));
            rl.cmd_w = TextRenderer::measure_text_width(TEXT_SIZE, r.cmd);
            std::string full_desc = "  ->  " + r.desc + (r.implemented ? "" : " [sắp có]");
            int avail_first = MAX_RIGHT - (rl.text_x + rl.cmd_w);
            if (avail_first >= 180 && TextRenderer::measure_text_width(TEXT_SIZE, full_desc) <= avail_first) {
                rl.desc_lines.push_back(full_desc);
                y += ROW_STEP;
            } else if (avail_first >= 240) {
                auto wrapped = TextRenderer::wrap_text(TEXT_SIZE, full_desc, avail_first);
                if (wrapped.size() <= 1) {
                    rl.desc_lines = std::move(wrapped);
                    y += ROW_STEP;
                } else {
                    rl.desc_lines.push_back(wrapped[0]);
                    std::string rest;
                    for (size_t k = 1; k < wrapped.size(); ++k) {
                        if (k > 1) rest += " ";
                        rest += wrapped[k];
                    }
                    auto rest_lines = TextRenderer::wrap_text(TEXT_SIZE, rest, MAX_RIGHT - (rl.text_x + 24));
                    for (auto& ln : rest_lines) rl.desc_lines.push_back(ln);
                    y += ROW_STEP + (int)rest_lines.size() * SUB_STEP;
                }
            } else {
                rl.desc_lines = TextRenderer::wrap_text(TEXT_SIZE, "-> " + r.desc + (r.implemented ? "" : " [sắp có]"), MAX_RIGHT - (rl.text_x + 24));
                y += ROW_STEP + (int)rl.desc_lines.size() * SUB_STEP;
            }
            sl.rows.push_back(std::move(rl));
        }
        y += 18;
        layout.push_back(std::move(sl));
    }
    int H = std::max(460, y + PAD + 48);

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 15, 23, 42);
    render::gradient::LinearGradient bg(0, 0, (float)W, (float)H);
    bg.add_stop(0.0f, render::color::from_hex("#1e3a8a"));
    bg.add_stop(0.55f, render::color::from_hex("#0f172a"));
    bg.add_stop(1.0f, render::color::from_hex("#020617"));
    bg.fill(canvas, W, H);

    // Outer glass card
    TextRenderer::draw_rounded_rect_shadow(canvas, W, H, PAD, PAD, W - 2 * PAD, H - 2 * PAD, 22, 10, 90);
    TextRenderer::draw_rounded_rect_filled(canvas, W, H, PAD, PAD, W - 2 * PAD, H - 2 * PAD, 22, 17, 24, 39, 235);
    TextRenderer::draw_rounded_rect_outline(canvas, W, H, PAD, PAD, W - 2 * PAD, H - 2 * PAD, 22, 2, 96, 165, 250, 150);

    // Title centered
    int tw = TextRenderer::measure_text_width(TITLE_SIZE, title);
    TextRenderer::draw_text_gradient_random(canvas, W, H, std::max(INNER_X, (W - tw) / 2), PAD + 46, TITLE_SIZE, title);
    TextRenderer::draw_line(canvas, W, H, INNER_X, PAD + 62, W - INNER_X, PAD + 62, 96, 165, 250, 2, 120);

    int yy = PAD + 98;
    for (const auto& sl : layout) {
        if (!sl.header.empty()) {
            TextRenderer::draw_text_on_canvas(canvas, W, H, INNER_X, yy, HEADER_SIZE, 250, 204, 21, sl.header);
            yy += 42;
        }
        for (const auto& r : sl.rows) {
            if (!r.icon.empty()) {
                EmojiRenderer::draw(canvas, W, H, INNER_X, yy - 21, ICON_SIZE, r.icon);
            }
            TextRenderer::draw_text_on_canvas(canvas, W, H, r.text_x, yy, TEXT_SIZE, 103, 232, 249, r.cmd);
            if (!r.desc_lines.empty()) {
                bool on_same_line = r.desc_lines[0].rfind("  ->", 0) == 0;
                if (on_same_line) {
                    TextRenderer::draw_text_on_canvas(canvas, W, H, r.text_x + r.cmd_w, yy, TEXT_SIZE, 226, 232, 240, r.desc_lines[0]);
                    for (size_t k = 1; k < r.desc_lines.size(); ++k) {
                        yy += SUB_STEP;
                        TextRenderer::draw_text_on_canvas(canvas, W, H, r.text_x + 24, yy, TEXT_SIZE, 203, 213, 225, r.desc_lines[k]);
                    }
                    yy += ROW_STEP;
                } else {
                    for (size_t k = 0; k < r.desc_lines.size(); ++k) {
                        yy += SUB_STEP;
                        TextRenderer::draw_text_on_canvas(canvas, W, H, r.text_x + 24, yy, TEXT_SIZE, 203, 213, 225, r.desc_lines[k]);
                    }
                    yy += ROW_STEP;
                }
            } else {
                yy += ROW_STEP;
            }
        }
        yy += 16;
    }

    TextRenderer::draw_line(canvas, W, H, INNER_X, H - PAD - 46, W - INNER_X, H - PAD - 46, 148, 163, 184, 1, 90);
    if (!footer_left.empty()) {
        TextRenderer::draw_text_on_canvas(canvas, W, H, INNER_X, H - PAD - 18, 18, 148, 163, 184, footer_left);
    }
    if (!footer_right.empty()) {
        int fw = TextRenderer::measure_text_width(18, footer_right);
        TextRenderer::draw_text_on_canvas(canvas, W, H, W - INNER_X - fw, H - PAD - 18, 18, 125, 211, 252, footer_right);
    }

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

// ============================================================
// /gay helpers - Render đẹp, layout giống /info, nền LGBT mờ
// ============================================================
// /gay helpers - Layout can doi, tieu de "Phieu chung nhan do gay"
// ============================================================
// /gay helpers - Phieu chung nhan do gay
// ============================================================
static bool inside_rounded_rect_gay(int px, int py, int x, int y, int w, int h, int radius) {
    if (px < x || px >= x + w || py < y || py >= y + h) return false;
    int cx, cy;
    if (px < x + radius && py < y + radius) { cx = x + radius; cy = y + radius; }
    else if (px >= x + w - radius && py < y + radius) { cx = x + w - radius - 1; cy = y + radius; }
    else if (px < x + radius && py >= y + h - radius) { cx = x + radius; cy = y + h - radius - 1; }
    else if (px >= x + w - radius && py >= y + h - radius) { cx = x + w - radius - 1; cy = y + h - radius - 1; }
    else return true;
    int dx = px - cx, dy = py - cy;
    return (dx * dx + dy * dy) <= (radius * radius);
}

static void render_gay_meter(const std::string& name, const std::string& avatar_key,
                              int percent, const std::string& out_path) {
    const int W = 1100;
    const int H = 820;
    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 15, 10, 30);

    // ==== Background: LGBT flag (sang, khong blur) ====
    struct RGB { unsigned char r, g, b; };
    RGB lgbt[] = {
        {228, 3, 3}, {255, 140, 0}, {255, 237, 0},
        {0, 128, 38}, {0, 77, 255}, {117, 7, 135}
    };
    for (int y = 0; y < H; y++) {
        float t = (float)y / (float)(H - 1) * 5.0f;
        int i = (int)t;
        if (i > 4) i = 4;
        float local = t - (float)i;
        unsigned char r = (unsigned char)(lgbt[i].r + (lgbt[i+1].r - lgbt[i].r) * local);
        unsigned char g = (unsigned char)(lgbt[i].g + (lgbt[i+1].g - lgbt[i].g) * local);
        unsigned char b = (unsigned char)(lgbt[i].b + (lgbt[i+1].b - lgbt[i].b) * local);
        for (int x = 0; x < W; x++) {
            int idx = (y * W + x) * 4;
            canvas[idx + 0] = r;
            canvas[idx + 1] = g;
            canvas[idx + 2] = b;
            canvas[idx + 3] = 255;
        }
    }
    TextRenderer::darken_region(canvas, W, H, 0, 0, W, H, 45);

    // ==== Card: semi-transparent, de lo mau co ====
    int card_x = 30, card_y = 130, card_w = W - 60, card_h = H - 170, card_r = 40;
    for (int l = 4; l >= 1; l--) {
        int off = l * 5;
        unsigned char a = (unsigned char)(20 + l * 12);
        TextRenderer::draw_rounded_rect_filled(canvas, W, H,
                                                 card_x + off, card_y + off, card_w, card_h, card_r,
                                                 0, 0, 0, a);
    }
    TextRenderer::draw_rounded_rect_filled(canvas, W, H, card_x, card_y, card_w, card_h, card_r,
                                             20, 22, 48, 195);
    TextRenderer::draw_rounded_rect_outline(canvas, W, H, card_x, card_y, card_w, card_h, card_r,
                                              3, 255, 100, 210, 240);

    // ==== Title (1 dong, chu hoa, tren card) ====
    std::string title = "PHIẾU CHỨNG NHẬN ĐỘ GAY";
    int title_size = 42;
    int title_w = TextRenderer::measure_text_width(title_size, title);
    int title_x = (W - title_w) / 2;
    TextRenderer::draw_text_shadow(canvas, W, H, title_x, 82, title_size,
                                    255, 255, 255, title);

    // ==== Avatar (trai) ====
    int avt_cx = 250, avt_cy = 440, avt_r = 130;
    for (int g = 3; g >= 1; g--) {
        TextRenderer::draw_circle_filled(canvas, W, H, avt_cx, avt_cy, avt_r + g * 6,
                                           255, 80, 200, (unsigned char)(30 + (3 - g) * 20));
    }
    TextRenderer::draw_circle_filled(canvas, W, H, avt_cx, avt_cy, avt_r, 60, 60, 80, 255);
    ImageRGBA* avt = AssetManager::get(avatar_key);
    if (avt && avt->valid()) {
        TextRenderer::draw_circle_image(canvas, W, H, avt, avt_cx, avt_cy, avt_r);
    }
    TextRenderer::draw_circle_outline_rainbow(canvas, W, H, avt_cx, avt_cy, avt_r + 4, 6);

    // ==== Ten duoi avatar ====
    std::string name_disp = name;
    if (name_disp.size() > 18) name_disp = name_disp.substr(0, 15) + "...";
    int name_size = 38;
    int name_w = TextRenderer::measure_text_width(name_size, name_disp);
    int name_x = avt_cx - name_w / 2;
    if (name_x < 50) name_x = 50;
    TextRenderer::draw_text_shadow(canvas, W, H, name_x, 660, name_size,
                                    255, 255, 255, name_disp);

    // ==== Ben phai ====
    int info_x = 500;

    unsigned char pr, pg, pb;
    if (percent < 15)       { pr = 100; pg = 255; pb = 150; }
    else if (percent < 30)  { pr = 100; pg = 220; pb = 200; }
    else if (percent < 50)  { pr = 255; pg = 220; pb = 80;  }
    else if (percent < 70)  { pr = 255; pg = 180; pb = 80;  }
    else if (percent < 85)  { pr = 255; pg = 150; pb = 200; }
    else if (percent < 100) { pr = 255; pg = 100; pb = 180; }
    else                    { pr = 200; pg = 100; pb = 255; }

    // Label "Do gay da do:"
    TextRenderer::draw_text_on_canvas(canvas, W, H, info_x, 290, 38,
                                       240, 240, 255, "Độ gay đã đo:");

    // Big %
    std::string pct_str = std::to_string(percent) + "%";
    int pct_size = 150;
    TextRenderer::draw_text_shadow(canvas, W, H, info_x, 460, pct_size, pr, pg, pb, pct_str);

    // Progress bar
    int bar_x = info_x, bar_y = 540, bar_w = 560, bar_h = 44;
    int bar_r = bar_h / 2;
    TextRenderer::draw_rounded_rect_filled(canvas, W, H, bar_x, bar_y, bar_w, bar_h, bar_r,
                                             22, 18, 38, 255);
    TextRenderer::draw_rounded_rect_outline(canvas, W, H, bar_x, bar_y, bar_w, bar_h, bar_r,
                                              2, 255, 80, 200, 100);

    int fill_w = (bar_w * percent) / 100;
    for (int px = 0; px < fill_w; px++) {
        float t = (float)px / (float)(bar_w - 1);
        unsigned char r, g, b;
        if (t < 0.5f) {
            float u = t * 2.0f;
            r = (unsigned char)(100 + (255 - 100) * u);
            g = (unsigned char)(255 + (220 - 255) * u);
            b = (unsigned char)(180 + (80 - 180) * u);
        } else {
            float u = (t - 0.5f) * 2.0f;
            r = 255;
            g = (unsigned char)(220 + (100 - 220) * u);
            b = (unsigned char)(80 + (200 - 80) * u);
        }
        for (int py = 0; py < bar_h; py++) {
            int abs_x = bar_x + px;
            int abs_y = bar_y + py;
            if (inside_rounded_rect_gay(abs_x, abs_y, bar_x, bar_y, bar_w, bar_h, bar_r)) {
                int idx = (abs_y * W + abs_x) * 4;
                canvas[idx + 0] = r;
                canvas[idx + 1] = g;
                canvas[idx + 2] = b;
                canvas[idx + 3] = 255;
            }
        }
    }

    // Label
    std::string label;
    if (percent < 15)       label = "THẲNG RỒI";
    else if (percent < 30)  label = "HƠI THẲNG";
    else if (percent < 50)  label = "BÌNH THƯỜNG";
    else if (percent < 70)  label = "HƠI GAY";
    else if (percent < 85)  label = "GAY VÃI";
    else if (percent < 100) label = "GAY NẶNG";
    else                    label = "TRÙM GAY";

    int lbl_size = 48;
    int lbl_w = TextRenderer::measure_text_width(lbl_size, label);
    int lbl_x = bar_x + (bar_w - lbl_w) / 2;
    TextRenderer::draw_text_shadow(canvas, W, H, lbl_x, 700, lbl_size, pr, pg, pb, label);

    TextRenderer::save_canvas(canvas, W, H, out_path);
}
void CommandHandler::load_scold_list() {
    g_scold_list.clear();
    std::ifstream f(paths::data("scold.txt").c_str());
    if (!f.good()) {
        std::cout << "[Scold] Không tìm thấy data/scold.txt\n";
        return;
    }
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first && line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            line = line.substr(3);
        }
        first = false;
        if (line.empty()) continue;
        g_scold_list.push_back(line);
    }
    std::cout << "[Scold] Loaded " << g_scold_list.size() << " cau\n";
}
// ============================================================
// Husks - load/save data/husks.json
// ============================================================
void CommandHandler::load_husks() {
    std::lock_guard<std::mutex> lk(g_husks_mtx);
    econ.set_husk("chan_le", cpp_int(1000000));
    econ.set_husk("tai_xiu", cpp_int(1000000));
    econ.set_husk("bau_cua", cpp_int(1000000));
    econ.set_husk("vietlott", cpp_int(50000000LL));
    std::ifstream f(paths::data("husks.json").c_str());
    if (!f.good()) {
        std::cout << "[Husks] Chưa có data/husks.json, dùng mặc định\n";
        return;
    }
    try {
        json j;
        f >> j;
        for (auto& [k, v] : j.items()) {
            cpp_int val = 0;
            if (v.is_string()) {
                try { val = cpp_int(v.get<std::string>()); } catch (...) { val = 0; }
            } else if (v.is_number_integer()) {
                val = cpp_int(v.get<long long>());
            }
            if (val > 0) {
                g_husks[k] = val;
                econ.set_husk(k, val);
            }
        }
        std::cout << "[Husks] Loaded " << j.size() << " hu\n";
    } catch (const std::exception& e) {
        std::cout << "[Husks] Load loi: " << e.what() << ", dung mac dinh\n";
    }
}

void CommandHandler::save_husks() {
    std::lock_guard<std::mutex> lk(g_husks_mtx);
    try {
        json j = json::object();
        const char* keys[] = {"chan_le", "tai_xiu", "bau_cua", "vietlott"};
        for (const char* k : keys) {
            cpp_int v = econ.get_husk(k);
            if (v <= 0) v = (std::string(k) == "vietlott") ? cpp_int(50000000LL) : cpp_int(1000000);
            g_husks[k] = v;
            j[k] = v.convert_to<std::string>();
        }
        std::ofstream f(paths::data("husks.json").c_str());
        if (!f.good()) {
            std::cout << "[Husks] Không ghi được data/husks.json\n";
            return;
        }
        f << j.dump(2);
    } catch (const std::exception& e) {
        std::cout << "[Husks] Save loi: " << e.what() << "\n";
    }
}
CommandHandler::CommandHandler(ZaloApi& api, PermissionManager& perm, EconomyManager& econ,
                               GameManager& game, GroupManager& group,
                               AccountManager& account)
    : api(api), perm(perm), econ(econ), game(game), group(group), account(account) {
    load_bot_info();
    load_scold_list();
    load_husks();
    register_commands();
}

void CommandHandler::load_user_economy_impl(const std::string& uid, const std::string& username) {
    AccountEconomyData d;
    if (account.load_economy(username, d)) {
        econ.set_full_data(uid, d.wallet, d.bank, d.debt, d.games_played, d.games_won, d.total_won, d.total_lost);
        econ.set_last_daily(uid, (std::time_t)d.last_daily);
        std::cout << "[Handler] Loaded economy for " << username
                  << ": wallet=" << d.wallet << " bank=" << d.bank << "\n";
    } else {
        econ.set_full_data(uid, 0, 0, 0, 0, 0, 0, 0);
    }
}

void CommandHandler::save_user_economy_impl(const std::string& uid) {
    std::string un = account.get_username(uid);
    if (un.empty()) return;
    UserEconomy e = econ.get_user_economy(uid);
    AccountEconomyData d;
    d.wallet = e.wallet;
    d.bank   = e.bank;
    d.debt   = e.debt;
    d.games_played = e.games_played;
    d.games_won    = e.games_won;
    d.total_won    = e.total_won;
    d.total_lost   = e.total_lost;
    d.last_daily   = (long long)econ.get_last_daily(uid);
    account.save_economy(un, d);
}

void CommandHandler::load_bot_info() {
    try {
        std::ifstream f(paths::file("bot_info.json"));
        if (f.good()) {
            json j; f >> j;
            bot_name_ = j.value("name", "Zalo Bot");
            std::cout << "[Handler] Bot name: " << bot_name_ << "\n";
            return;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Handler] Lỗi đọc bot_info.json: " << e.what() << "\n";
    }
    std::cout << "[Handler] Dùng tên mặc định: " << bot_name_ << "\n";
}

void CommandHandler::cache_user_name(const std::string& uid, const std::string& name) {
    if (uid.empty() || name.empty()) return;
    std::lock_guard<std::mutex> lk(cache_mtx_);
    user_name_cache_[uid] = name;
}

std::string CommandHandler::format_user(const std::string& uid) {
    {
        std::lock_guard<std::mutex> lk(cache_mtx_);
        auto it = user_name_cache_.find(uid);
        if (it != user_name_cache_.end()) return it->second;
    }
    if (uid.size() > 8) return uid.substr(0, 4) + "..." + uid.substr(uid.size() - 4);
    return uid;
}

std::vector<std::string> CommandHandler::parse_command(const std::string& message) {
    std::vector<std::string> args;
    std::istringstream iss(message);
    std::string token;
    while (iss >> token) args.push_back(token);
    return args;
}

void CommandHandler::reply_text(const std::string& msg, const std::string& thread_id,
                                int thread_type) {
    std::string sender = ctx().sender_name.empty() ? "bạn" : ctx().sender_name;
    std::string header = "[" + bot_name_ + "]\n";
    std::string full = header + msg;
    bool is_self_chat = (thread_id == ctx().sender_id);
    bool can_mention = (thread_type == 1)
                    && !ctx().sender_id.empty()
                    && !ctx().sender_name.empty()
                    && !is_self_chat;
    if (can_mention) {
        std::string mt = "@" + sender;
        api.send_message_with_mention_async(mt + " " + full, thread_id, thread_type,
                                             ctx().sender_id, 0, (int)mt.size());
    } else {
        api.send_message_async(full, thread_id, thread_type);
    }
}

void CommandHandler::reply_image(const std::string& title, const std::string& msg,
                                  const std::string& thread_id, int thread_type) {
    if (!g_use_image_mode) { reply_text(msg, thread_id, thread_type); return; }
    static std::atomic<int> counter{0};
    std::string out = paths::cache("reply_") + std::to_string(counter++) + ".png";
    const int W = 1040;
    const int PAD = 38;
    const int INNER_PAD = 36;
    const int TITLE_SIZE = 38;
    const int TEXT_SIZE = 28;
    const int LINE_H = 42;
    const int MAX_TEXT_W = W - 2 * PAD - 2 * INNER_PAD;

    std::vector<std::string> lines;
    {
        std::istringstream iss(msg);
        std::string raw_line;
        while (std::getline(iss, raw_line)) {
            if (!raw_line.empty() && raw_line.back() == '\r') raw_line.pop_back();
            if (raw_line.empty()) {
                lines.push_back("");
                continue;
            }
            auto wrapped = TextRenderer::wrap_text(TEXT_SIZE, raw_line, MAX_TEXT_W);
            for (auto& wline : wrapped) lines.push_back(std::move(wline));
        }
    }
    if (lines.empty()) lines.push_back(" ");

    int card_x = PAD, card_y = PAD;
    int card_w = W - 2 * PAD;
    int header_h = 84;
    int body_h = (int)lines.size() * LINE_H + 34;
    int card_h = header_h + body_h;
    int H = std::max(260, card_h + 2 * PAD);

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 15, 23, 42);
    TextRenderer::fill_linear_gradient(canvas, W, H, 0, H, 30, 27, 75, 15, 23, 42);

    TextRenderer::draw_rounded_rect_shadow(canvas, W, H, card_x, card_y, card_w, card_h, 24, 12, 95);
    TextRenderer::draw_rounded_rect_filled(canvas, W, H, card_x, card_y, card_w, card_h, 24, 17, 24, 39, 242);
    TextRenderer::draw_rounded_rect_outline(canvas, W, H, card_x, card_y, card_w, card_h, 24, 2, 139, 92, 246, 190);

    TextRenderer::draw_text_shadow(canvas, W, H, card_x + INNER_PAD, card_y + 52, TITLE_SIZE, 250, 204, 21, title);
    int div_y = card_y + header_h - 10;
    TextRenderer::draw_line(canvas, W, H, card_x + INNER_PAD, div_y, card_x + card_w - INNER_PAD, div_y, 139, 92, 246, 2, 160);

    int y = div_y + 42;
    for (const auto& line : lines) {
        if (!line.empty()) {
            TextRenderer::draw_text_on_canvas(canvas, W, H, card_x + INNER_PAD, y, TEXT_SIZE, 241, 245, 249, line);
        }
        y += LINE_H;
    }

    if (TextRenderer::save_canvas(canvas, W, H, out)) {
        api.send_message_with_image_async(title, out, thread_id, thread_type);
        std::cout << "[Handler] Đã gửi ảnh: " << out << "\n";
    } else {
        reply_text(msg, thread_id, thread_type);
    }
}

// ==== Session helpers ====
static void save_session_to_disk(const std::string& uid, const std::string& username) {
    try {
        nlohmann::json j;
        std::ifstream f("data/sessions.json");
        if (f.good()) f >> j;
        f.close();
        j[uid] = username;
        std::ofstream out("data/sessions.json");
        out << j.dump(2);
    } catch (...) {}
}

static void remove_session_from_disk(const std::string& uid) {
    try {
        nlohmann::json j;
        std::ifstream f("data/sessions.json");
        if (f.good()) f >> j;
        f.close();
        j.erase(uid);
        std::ofstream out("data/sessions.json");
        out << j.dump(2);
    } catch (...) {}
}
// ========================

void CommandHandler::register_commands() {



    // Load auto state
    std::ifstream in_auto(paths::data("auto_state.json").c_str());
    if (in_auto.good()) {
        try {
            json j; in_auto >> j;
            for (auto& el : j.items()) {
                if (el.value().contains("bot") && el.value()["bot"].get<bool>()) set_bot_for_thread(el.key(), true);
                if (el.value().contains("game") && el.value()["game"].get<bool>()) set_game_for_thread(el.key(), true);
            }
        } catch (...) {}
    }
    in_auto.close();
    // ===== NONG TRAI =====
    // ===== NONG TRAI =====
    commands["nongtrai"] = [this](const std::vector<std::string>& args,
                                  const std::string& uid,
                                  const std::string& tid,
                                  int tt, bool is_self) {
        (void)is_self;
        if (!g_farm_mgr) { reply_text("❌ Nông trại chưa sẵn sàng.", tid, tt); return; }
        std::string sub = (args.size() > 1) ? args[1] : "help";
        std::transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        auto join_args = [&](int from) -> std::string {
            std::string s;
            for (int i = from; i < (int)args.size(); ++i) {
                if (i > from) s += " ";
                s += args[i];
            }
            return s;
        };

        std::string out;
        if (sub == "farm" || sub == "check" || sub == "xem") {
            static std::atomic<int> farmCtr{0};
            std::string imgPath = paths::cache("nongtrai_") + std::to_string(farmCtr++) + ".png";
            auto& farm = g_farm_mgr->get_or_create(uid);
            g_farm_mgr->refresh_soil(farm);
            nong_trai_render::render_farm(farm, imgPath);
            api.send_image_async(imgPath, tid, tt);
            return;
        } else if (sub == "help" || sub == "huongdan") {
            std::string h = "🌾 HƯỚNG DẪN NÔNG TRẠI 🌾\n\n";
            h += "1. /nongtrai farm - Xem trạng thái nông trại\n";
            h += "2. /nongtrai cuocdat [các slot đất] - Cuốc đất\n";
            h += "3. /nongtrai tuoinuoc [các slot đất] - Tưới nước\n";
            h += "4. /nongtrai gieohat [mã hạt giống] [các slot đất] - Trồng hạt giống\n";
            h += "5. /nongtrai use [mã SP] [các slot đất] - Sử dụng vật phẩm\n";
            h += "6. /nongtrai thuhoach [các slot đất] - Thu hoạch\n";
            h += "7. /nongtrai shop - Xem cửa hàng\n";
            h += "8. /nongtrai buy [mã SP] [SL] - Mua vật phẩm\n";
            h += "9. /nongtrai kho - Xem vật phẩm trong kho\n";
            h += "10. /nongtrai sell [mã SP] [SL] - Bán sản phẩm\n\n";
            h += "Lưu ý:\n";
            h += "- Đất sẽ thiếu nước sau 4h Không tưới\n";
            h += "- Đất sẽ khô cằn sau 8h Không tưới\n";
            h += "- Đất khô cằn cần cuốc trước khi tưới\n";
            h += "- Dùng lệnh ntr nếu bn thấy nongtrai là quá dài!!!";

            static std::atomic<int> bgCtr{0};
            std::string bgPath = paths::cache("nongtrai_bg_") + std::to_string(bgCtr++) + ".png";
            nong_trai_render::render_farm_bg(bgPath);
            
            std::vector<std::tuple<std::string,int,int>> empty;
            api.send_message_with_image_mentions_async(h, bgPath, tid, tt, empty);
            return;
        } else if (sub == "shop") {
            static std::atomic<int> shopCtr{0};
            std::string imgPath = paths::cache("shop_") + std::to_string(shopCtr++) + ".png";
            auto& farm = g_farm_mgr->get_or_create(uid);
            long long landPrice = g_farm_mgr->calculate_land_price((int)farm.plots.size());
            shop_render::render_shop(landPrice, imgPath);
            api.send_image_async(imgPath, tid, tt);
            return;
        } else if (sub == "buy") {
            if (args.size() < 3) { reply_text("ℹ️ Cú pháp: nongtrai buy <mã> [số lượng]", tid, tt); return; }
            int qty = (args.size() > 3) ? std::atoi(args[3].c_str()) : 1;
            out = g_farm_mgr->cmd_buy(uid, args[2], qty);
        } else if (sub == "sell") {
            if (args.size() < 3) { reply_text("ℹ️ Cú pháp: nongtrai sell <mã> [số lượng|all]", tid, tt); return; }
            int qty = 1;
            if (args.size() > 3) {
                std::string a = args[3];
                std::transform(a.begin(), a.end(), a.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                qty = (a == "all") ? -1 : std::atoi(a.c_str());
            }
            out = g_farm_mgr->cmd_sell(uid, args[2], qty);
        } else if (sub == "kho" || sub == "bag" || sub == "tui") {
            out = g_farm_mgr->cmd_bag(uid);
        } else if (sub == "gieohat") {
            if (args.size() < 4) { reply_text("ℹ️ Cú pháp: nongtrai gieohat <mã> <các ô đất (vd: 0 1)>", tid, tt); return; }
            out = g_farm_mgr->cmd_gieohat(uid, args[2], NongTraiManager::parse_slots(join_args(3)));
        } else if (sub == "tuoinuoc") {
            if (args.size() < 3) { reply_text("ℹ️ Cú pháp: nongtrai tuoinuoc <các ô đất (vd: 0 1)>", tid, tt); return; }
            out = g_farm_mgr->cmd_tuoinuoc(uid, NongTraiManager::parse_slots(join_args(2)));
        } else if (sub == "cuocdat") {
            if (args.size() < 3) { reply_text("ℹ️ Cú pháp: nongtrai cuocdat <các ô đất (vd: 0 1)>", tid, tt); return; }
            out = g_farm_mgr->cmd_cuocdat(uid, NongTraiManager::parse_slots(join_args(2)));
        } else if (sub == "thuhoach") {
            if (args.size() < 3) { reply_text("ℹ️ Cú pháp: nongtrai thuhoach <các ô đất (vd: 0 1)>", tid, tt); return; }
            out = g_farm_mgr->cmd_thuhoach(uid, NongTraiManager::parse_slots(join_args(2)));
        } else if (sub == "use") {
            if (args.size() < 4) { reply_text("ℹ️ Cú pháp: nongtrai use <mã> <các ô đất>", tid, tt); return; }
            out = g_farm_mgr->cmd_use(uid, args[2], NongTraiManager::parse_slots(join_args(3)));
        } else {
            out = "❌ Lệnh không hợp lệ. Gõ 'nongtrai help' để xem hướng dẫn.";
        }
        if (!out.empty()) {
            reply_text(out, tid, tt);
            g_farm_mgr->set_dirty();
        }
    };
    commands["ntr"] = commands["nongtrai"];



    commands["image"] = [this](const auto& args, const auto& uid,
                                const auto& tid, int tt, bool) {
        if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin!", tid, tt); return; }
        bool en = (args.size() > 1 && args[1] == "on");
        g_use_image_mode = en;
        reply_text(en ? "✅ Bật chế độ ảnh" : "✅ Tắt chế độ ảnh", tid, tt);
    };

            commands["help"] = [this](const auto& args, const auto& uid,
                              const auto& tid, int tt, bool) {
        int page = 1;
        if (args.size() > 1) {
            try { page = std::stoi(args[1]); } catch (...) { page = 1; }
        }
        bool is_admin = perm.is_admin(uid);
        int max_page = is_admin ? 4 : 3;
        if (page < 1) page = 1;
        if (page > max_page) page = max_page;

        std::string status = std::string("Bot: ") + (get_bot_for_thread(tid) ? "ON" : "OFF")
                           + " | Game: " + (get_game_for_thread(tid) ? "ON" : "OFF")
                           + " | Prefix: " + prefix;

        std::string title;
        std::vector<HelpSection> sections;

        if (page == 1) {
            title = "🌟 DANH SÁCH LỆNH BOT 🌟";
            {
                HelpSection sec;
                sec.header = "📌 Thông tin & Giải trí:";
                sec.rows = {
                    {"📖", prefix + "help [1-4]",        "Xem danh sách lệnh theo trang", true},
                    {"📰", prefix + "info [@user]",      "Thông tin tài khoản Zalo",      true},
                    {"🏠", prefix + "group",             "Thông tin nhóm hiện tại",       true},
                    {"🤖", prefix + "detail",            "Xem thông số hệ thống bot",     true},
                    {"🔖", prefix + "command [admin]",   "Danh sách lệnh dạng chữ",       true},
                    {"🌐", prefix + "dich [ngôn ngữ] <text>", "Dịch văn bản (hoặc reply)",true},
                    {"🌤️", prefix + "thoitiet <thành phố>",   "Xem thời tiết hiện tại",   true},
                    {"🗣️", prefix + "voice <nội dung>",  "Chuyển văn bản thành giọng nói",true},
                };
                sections.push_back(std::move(sec));
            }
            {
                HelpSection sec;
                sec.header = "🖼️ Ảnh & Nội dung vui:";
                sec.rows = {
                    {"📸", prefix + "girl | boy | anime | cosplay", "Gửi ảnh ngẫu nhiên", true},
                    {"📜", prefix + "cadao | thinh | tarot | truyencuoi", "Giải trí văn bản", true},
                    {"🏳️‍🌈", prefix + "gay [@user]",     "Đo độ gay vui nhộn",          true},
                    {"🎨", prefix + "sticker <id>",      "Gửi sticker Zalo theo ID",      true},
                };
                sections.push_back(std::move(sec));
            }
        } else if (page == 2) {
            title = "💰 TÀI KHOẢN & KINH TẾ 💰";
            {
                HelpSection sec;
                sec.header = "💳 Ví & Ngân hàng:";
                sec.rows = {
                    {"💳", prefix + "mycard | sodu",     "Thẻ tài khoản & thống kê game", true},
                    {"🏦", prefix + "bank",              "Xem số dư ví, bank và nợ",      true},
                    {"💰", prefix + "nap <số>",          "Nạp từ ví cá nhân vào bank",    true},
                    {"💸", prefix + "rut <số>",          "Rút tiền từ bank về ví",        true},
                    {"📤", prefix + "chuyen <@u> <số>",  "Chuyển tiền cho người khác",    true},
                    {"📥", prefix + "vay <số> | tra <số>","Vay / trả nợ ngân hàng",       true},
                    {"🎁", prefix + "daily",             "Điểm danh nhận 50.000 VNĐ",     true},
                    {"👑", prefix + "top [n]",           "Bảng xếp hạng đại gia",         true},
                };
                sections.push_back(std::move(sec));
            }
            {
                HelpSection sec;
                sec.header = "🔐 Phiên đăng nhập & Tiện ích:";
                sec.rows = {
                    {"📝", prefix + "signup <u> <p>",    "Đăng ký tài khoản (IB riêng)",  true},
                    {"🔐", prefix + "login <u> <p>",     "Đăng nhập tài khoản",           true},
                    {"🚪", prefix + "logout | whoami",   "Đăng xuất / xem UID & quyền",   true},
                    {"📱", prefix + "qr <nội dung>",     "Tạo mã QR dạng ảnh",            true},
                };
                sections.push_back(std::move(sec));
            }
        } else if (page == 3) {
            title = "🎮 TRÒ CHƠI & NÔNG TRẠI 🎮";
            {
                HelpSection sec;
                sec.header = "🎲 Cá cược & Mini-game:";
                sec.rows = {
                    {"🎲", prefix + "taixiu <tài|xỉu> <n>", "Tài xỉu theo phiên 60s",     true},
                    {"⚪", prefix + "chanle <c|l> <n>",     "Chẵn lẻ 3 xúc xắc + nổ hũ",  true},
                    {"🦀", prefix + "baucua <con> <n>",     "Bầu cua (nhiều cửa 1 lượt)", true},
                    {"✊", prefix + "keobuabao <cửa> <n>",  "Oẳn tù tì với Bot",          true},
                    {"🎟️", prefix + "vietlott <6 số|auto> <n>", "Xổ số Vietlott 6/55",   true},
                    {"📈", prefix + "soicau [tx|cl]",       "Biểu đồ soi cầu Tài Xỉu/CL", true},
                    {"♟️", prefix + "caro <@u|bot> [n]",    "Cờ caro 15x15 (gõ <dòng> <cột>)", true},
                    {"🔢", prefix + "doanso [min] [max]",   "Đoán số bí mật",             true},
                    {"📝", prefix + "noitu [từ|cancel]",    "Nối từ tiếng Việt",          true},
                    {"🔤", prefix + "doantu [cancel]",      "Đoán từ tiếng Việt",         true},
                    {"🌾", prefix + "nongtrai (ntr)",       "Nông trại & cửa hàng",       true},
                    {"🏆", prefix + "rank | topchat",       "Top tương tác tin nhắn nhóm",true},
                };
                sections.push_back(std::move(sec));
            }
        } else {
            title = "👮 LỆNH QUẢN TRỊ ADMIN 👮";
            {
                HelpSection sec;
                sec.header = "🛡️ Bảo vệ & Quản lý nhóm:";
                sec.rows = {
                    {"🛡️", prefix + "groupprotect",               "Bảng điều khiển bảo vệ", true},
                    {"🚫", prefix + "groupprotect ban <@u>",      "Kick thành viên vi phạm",true},
                    {"🔒", prefix + "groupprotect lock|approve on|off","Khóa chat / duyệt TV",true},
                    {"🔇", prefix + "mute <@u> [phút] | unmute",  "Cấm / bỏ cấm chat nhóm", true},
                    {"👢", prefix + "kick <@u> | warn <@u>",      "Kick hoặc cảnh cáo",     true},
                    {"♻️", prefix + "undo",                       "Thu hồi tin nhắn bot",   true},
                };
                sections.push_back(std::move(sec));
            }
            {
                HelpSection sec;
                sec.header = "⚙️ Cấu hình Bot & Tài chính:";
                sec.rows = {
                    {"✅", prefix + "on | off | bot | game",      "Bật/tắt bot & game nhóm",true},
                    {"🖼️", prefix + "image on|off",               "Bật/tắt trả lời bằng ảnh",true},
                    {"💰", prefix + "setmoney | buff | nerf | sethu","Chỉnh tiền & hũ game",true},
                    {"👑", prefix + "setad | listadmin | setprefix","Quản lý admin & prefix",true},
                    {"🐛", prefix + "debug | txend | exit",       "Debug / kết phiên TX / tắt",true},
                };
                sections.push_back(std::move(sec));
            }
        }

        std::string nav = "Trang " + std::to_string(page) + "/" + std::to_string(max_page);
        if (page > 1) nav = "<< " + prefix + "help " + std::to_string(page - 1) + "  |  " + nav;
        if (page < max_page) nav = nav + "  |  " + prefix + "help " + std::to_string(page + 1) + " >>";

        static std::atomic<int> ctr{0};
        std::string out = paths::cache("help_") + std::to_string(ctr++) + ".png";
        render_help_image(title, sections, status, nav, out);
        api.send_image_async(out, tid, tt);
        std::cout << "[Handler] help page " << page << ": " << out << "\n";
    };

    commands["signup"] = [this](const auto& args, const auto& uid,
                                const auto& tid, int tt, bool) {
        if (args.size() < 3) { reply_text("❌ Cú pháp: signup <user> <pass>", tid, tt); return; }
        if (account.is_logged_in(uid)) { reply_text("❌ Đã đăng nhập: " + account.get_username(uid), tid, tt); return; }
        std::string err = account.register_account(args[1], args[2]);
        if (!err.empty()) { reply_text("❌ " + err, tid, tt); return; }
        err = account.login_user(args[1], args[2], uid);
        if (err.empty()) {
            econ.set_full_data(uid, 0, 0, 0, 0, 0);
            save_session_to_disk(uid, args[1]);
            reply_text("✅ Đăng ký thành công: " + args[1], tid, tt);
        } else {
            reply_text("❌ " + err, tid, tt);
        }
    };

    commands["login"] = [this](const auto& args, const auto& uid,
                               const auto& tid, int tt, bool) {
        if (args.size() < 3) { reply_text("❌ Cú pháp: login <user> <pass>", tid, tt); return; }
        if (account.is_logged_in(uid)) { reply_text("❌ Đã đăng nhập: " + account.get_username(uid), tid, tt); return; }
        std::string err = account.login_user(args[1], args[2], uid);
        if (err.empty()) {
            load_user_economy_impl(uid, args[1]);
            save_session_to_disk(uid, args[1]);
            reply_text("✅ Xin chào " + args[1], tid, tt);
        } else {
            reply_text("❌ " + err, tid, tt);
        }
    };

    commands["logout"] = [this](const auto&, const auto& uid,
                                const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) { reply_text("❌ Chưa đăng nhập", tid, tt); return; }
        std::string un = account.get_username(uid);
        save_user_economy_impl(uid);
        remove_session_from_disk(uid);
        account.logout(uid);
        reply_text("✅ Đã đăng xuất: " + un, tid, tt);
    };

    commands["whoami"] = [this](const auto&, const auto& uid,
                                const auto& tid, int tt, bool) {
        Role r = perm.get_user_role(uid);
        std::string rs = "MEMBER";
        if (r == Role::HOST) rs = "HOST";
        else if (r == Role::SUPER_ADMIN) rs = "SUPER ADMIN";
        else if (r == Role::BOT_ADMIN) rs = "ADMIN";
        else if (r == Role::GROUP_ADMIN) rs = "GROUP ADMIN";
        std::string acc = account.is_logged_in(uid) ? account.get_username(uid) : "(chưa đăng nhập)";
        reply_text("🪪 UID: " + uid + "\n🎖️ Role: " + rs + "\n👤 Account: " + acc, tid, tt);
    };

        
    commands["detail"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        (void)args; (void)uid;
        SystemDetails sd = get_sys_details(GetCurrentProcessId());
        static std::atomic<int> ctr{0};
        std::string out = paths::cache("detail_") + std::to_string(ctr++) + ".png";
        bot_info_render::render(get_bot_name(), sd, out);
        api.send_image_async(out, tid, tt);
        std::cout << "[Handler] detail: " << out << "\n";
    };
        auto mycard = [this](const auto& args, const auto& uid,
                      const auto& tid, int tt, bool) {
        std::string target = uid;
        if (args.size() > 1 && !args[1].empty()) target = args[1];
        if ((!account.is_logged_in(target) && !g_debug_mode)) {
            if (target == uid) reply_text("❌ Cần đăng nhập trước", tid, tt);
            else reply_text("❌ " + format_user(target) + " chưa đăng nhập", tid, tt);
            return;
        }
        std::string un = account.get_username(target);
        UserEconomy ed = econ.get_user_economy(target);
        profile_render::PlayerCardData d;
        d.name    = format_user(target);
        d.account = un;
        d.balance = ed.bank;
        d.total_winnings = ed.total_won;
        d.total_losses   = ed.total_lost;
        d.net_profit     = ed.total_won - ed.total_lost;
        d.total_games    = ed.games_played;
        d.total_wins     = ed.games_won;
        d.win_rate = (ed.games_played > 0)
            ? (100 * ed.games_won / ed.games_played) : 0;
        d.created_time = "Ẩn";
        d.daily_status = econ.has_claimed_daily(target) ? "Đã nhận quà hôm nay" : "Chưa nhận quà";
        d.is_online     = true;
        d.is_active     = true;
        d.is_active_pc  = false;
        d.is_active_web = false;
        std::string avt_path = paths::cache("avt_") + target + ".png";
        std::string avt_url = "";
        
        // Kiểm tra xem đã có file trong cache chưa
        std::ifstream _ft(avt_path, std::ios::binary);
        bool has_cache = _ft.good();
        _ft.close();
        
        if (!has_cache) {
            // Nếu chưa có, gọi API Zalo để lấy link Avatar
            auto resp = api.get_user_info(target);
            try {
                if (resp.contains("result") && resp["result"].is_object()) {
                    auto& r = resp["result"];
                    if (r.contains("changed_profiles") && r["changed_profiles"].is_object()) {
                        auto& cp = r["changed_profiles"];
                        if (cp.contains(target) && cp[target].is_object()) {
                            auto& u = cp[target];
                            if (u.contains("avatar") && u["avatar"].is_string()) avt_url = u["avatar"].get<std::string>();
                        } else if (!cp.empty()) {
                            for (auto& [k, v] : cp.items()) {
                                if (v.is_object() && v.contains("avatar") && v["avatar"].is_string()) {
                                    avt_url = v.value("avatar", std::string(""));
                                    break;
                                }
                            }
                        }
                    }
                }
            } catch (...) {}
            
            // Nếu lấy được URL thì tiến hành tải ảnh về cache
            if (!avt_url.empty()) {
                api.download_image(avt_url, avt_path); 
            }
        }
        
        // Nạp vào AssetManager để vẽ (dù có cache cũ hay vừa tải xong)
        std::ifstream _ft2(avt_path, std::ios::binary);
        if (_ft2.good()) {
            _ft2.close();
            AssetManager::load("avt_" + target, avt_path);
            d.avatar_key = "avt_" + target;
        }
        static std::atomic<int> ctr{0};
        std::string out = paths::cache("mycard_") + std::to_string(ctr++) + ".png";
        profile_render::render_player_card(d, out);
        api.send_image_async(out, tid, tt);
        std::cout << "[Handler] mycard: " << out << "\n";
    };

    commands["doanso"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        (void)uid;
        if (args.size() > 1 && (args[1] == "cancel" || args[1] == "end" || args[1] == "stop")) {
            if (g_mini_games.erase(tid)) reply_text("\U0001F6D1 \u0110\u00E3 hu\u1EF7 v\u00E1n \u0111o\u00E1n s\u1ED1.", tid, tt);
            else                         reply_text("\u26A0\uFE0F Ch\u01B0a c\u00F3 v\u00E1n \u0111o\u00E1n s\u1ED1.", tid, tt);
            return;
        }
        if (args.size() > 1 && (args[1] == "help" || args[1] == "?")) {
            reply_text(
                "\U0001F522 \u0110O\u00C1N S\u1ED0 - H\u01AF\u1EDANG D\u1EAAN\n\n"
                "/doanso <min> <max>  \u2014 b\u1EAFt \u0111\u1EA7u (VD: /doanso 1 100)\n"
                "/doanso <max>        \u2014 1 \u2192 max (VD: /doanso 100)\n"
                "/doanso              \u2014 m\u1EB7c \u0111\u1ECBnh 1 \u2192 1000\n"
                "/doanso cancel|end   \u2014 hu\u1EF7 v\u00E1n\n"
                "/doanso help         \u2014 h\u01B0\u1EDBng d\u1EABn n\u00E0y\n\n"
                "Lu\u1EADt:\n"
                "\u2022 Bot ch\u1ECDn 1 s\u1ED1 b\u00ED m\u1EADt trong ph\u1EA1m vi\n"
                "\u2022 G\u00F5 s\u1ED1 \u0111\u1EC3 \u0111o\u00E1n (kh\u00F4ng c\u1EA7n prefix)\n"
                "\u2022 Bot: \U0001F4C8 Cao h\u01A1n / \U0001F4C9 Th\u1EA5p h\u01A1n / \U0001F389 \u0110\u00FAng\n"
                "\u2022 S\u1ED1 l\u01B0\u1EE3t = 5 + floor(range^0.4), t\u1ED1i \u0111a 30",
                tid, tt);
            return;
        }
        if (g_mini_games.count(tid)) {
            reply_text("\u26A0\uFE0F \u0110ang c\u00F3 tr\u00F2 ch\u01A1i kh\u00E1c trong nh\u00F3m. G\u00F5 /doanso cancel \u0111\u1EC3 hu\u1EF7.", tid, tt);
            return;
        }

        int minV = 1, maxV = 1000;
        if (args.size() >= 3) {
            try {
                minV = std::stoi(args[1]);
                maxV = std::stoi(args[2]);
            } catch (...) {
                reply_text("\u274C C\u00FA ph\u00E1p: /doanso <min> <max>\nV\u00ED d\u1EE5: /doanso 1 100", tid, tt);
                return;
            }
        } else if (args.size() == 2) {
            try { maxV = std::stoi(args[1]); } catch (...) {
                reply_text("\u274C C\u00FA ph\u00E1p: /doanso <min> <max>\nV\u00ED d\u1EE5: /doanso 1 100", tid, tt);
                return;
            }
            minV = 1;
        }

        if (minV < 1 || maxV < 1 || minV >= maxV) {
            reply_text("\u274C Ph\u1EA1m vi kh\u00F4ng h\u1EE3p l\u1EC7. C\u1EA7n 1 \u2264 min < max \u2264 1.000.000.", tid, tt);
            return;
        }
        if (maxV > 1000000) {
            reply_text("\u274C Max qu\u00E1 l\u1EDBn. Gi\u1EDBi h\u1EA1n t\u1ED1i \u0111a 1.000.000.", tid, tt);
            return;
        }

        int range = maxV - minV + 1;
        MiniGameState s;
        s.type = "guessNumber";
        s.range_min = minV;
        s.range_max = maxV;
        s.range = range;
        s.targetNumber = myRandom::RanInt(minV, maxV);
        s.maxAttempts = 5 + (int)std::floor(std::pow(range, 0.4));
        if (s.maxAttempts > 30) s.maxAttempts = 30;
        if (s.maxAttempts < 5)  s.maxAttempts = 5;
        s.active = true;
        g_mini_games[tid] = s;

        reply_text(
            "\U0001F522 \u0110O\u00C1N S\u1ED0\n"
            "Ph\u1EA1m vi: " + std::to_string(minV) + " \u2192 " + std::to_string(maxV) + "\n"
            "B\u1EA1n c\u00F3 " + std::to_string(s.maxAttempts) + " l\u01B0\u1EE3t.\n"
            "G\u00F5 s\u1ED1 b\u1EA5t k\u1EF3 \u0111\u1EC3 \u0111o\u00E1n!",
            tid, tt);
    };

    commands["doantu"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (args.size() > 1 && args[1] == "cancel") {
            if (g_mini_games.count(tid) && g_mini_games[tid].type == "wordGuess") {
                std::string ans = g_mini_games[tid].targetWord;
                g_mini_games.erase(tid);
                reply_text("Đã hủy trò chơi. Từ cần đoán là: " + ans, tid, tt);
            } else reply_text("Không có trò chơi Đoán Từ nào đang diễn ra.", tid, tt);
            return;
        }
        if (g_mini_games.count(tid)) { reply_text("Đang có trò chơi khác diễn ra trong nhóm này.", tid, tt); return; }
        
        std::vector<std::string> words = {"zalo", "chatbot", "trò chơi", "lập trình", "thông minh nhân tạo", "mạng xã hội", "công nghệ", "internet", "điện thoại", "máy tính"};
        MiniGameState s;
        s.type = "wordGuess";
        s.targetWord = words[myRandom::RanInt(0, words.size() - 1)];
        s.active = true;
        g_mini_games[tid] = s;
        reply_text("Trò chơi Đoán Từ đã bắt đầu! Hãy đoán từ có " + std::to_string(s.targetWord.length()) + " chữ cái.", tid, tt);
    };
    commands["mycard"] = mycard;
    commands["sodu"] = mycard;

    commands["info"] = [this](const auto& args, const auto& uid,
                              const auto& tid, int tt, bool) {
        std::string target = (args.size() > 1) ? args[1] : uid;
        bool valid = !target.empty() && std::all_of(target.begin(), target.end(),
                                                     [](unsigned char c){ return std::isdigit(c); });
        if (!valid) { reply_text("❌ UID không hợp lệ: " + target, tid, tt); return; }

        auto resp = api.get_user_info(target);
        if (resp.contains("error")) { reply_text("❌ Lỗi: " + resp.value("error", "unknown"), tid, tt); return; }

        json u = json::object();
        try {
            if (resp.contains("result") && resp["result"].is_object()) {
                auto& r = resp["result"];
                if (r.contains("changed_profiles") && r["changed_profiles"].is_object()) {
                    auto& cp = r["changed_profiles"];
                    if (cp.contains(target) && cp[target].is_object()) u = cp[target];
                    else if (!cp.empty()) {
                        for (auto& [k, v] : cp.items()) {
                            if (v.is_object()) { u = v; break; }
                        }
                    }
                }
            }
        } catch (...) {}

        if (u.empty()) { reply_text("❌ Không có dữ liệu profile", tid, tt); return; }

        auto gs = [&](const char* k, const char* def="Ẩn") -> std::string {
            if (u.contains(k) && u[k].is_string()) {
                std::string v = u[k].get<std::string>();
                if (!v.empty()) return v;
            }
            return def;
        };
        auto gi = [&](const char* k, long long d=0) -> long long {
            if (u.contains(k) && u[k].is_number()) return u[k].get<long long>();
            return d;
        };

        std::string name = gs("displayName", "");
        if (name.empty()) name = gs("zaloName", "");
        if (name.empty()) name = format_user(target);

        std::string zname = gs("zaloName");
        std::string uname = gs("username");
        std::string phone = gs("phoneNumber");
        std::string avt_url = gs("avatar", "");
        std::string cov_url = gs("cover", "");
        int g_val = (int)gi("gender", -1);
        std::string gender = g_val == 0 ? "Nam" : (g_val == 1 ? "Nữ" : "Ẩn");

        auto ts_to_date = [](long long ts) -> std::string {
            if (ts <= 0) return "Ẩn";
            time_t t = (time_t)ts;
            struct tm* tm_info = localtime(&t);
            char buf[32];
            strftime(buf, sizeof(buf), "%H:%M %d/%m/%Y", tm_info);
            return std::string(buf);
        };
        std::string created = ts_to_date(gi("createdTs", 0));
        std::string lastact = ts_to_date(gi("lastActionTime", 0) / 1000);

        std::string status = gs("status", "");
        for (auto& c : status) if (c == '\n' || c == '\r') c = ' ';

        std::string avt_path = paths::cache("avt_") + target + ".png";
        std::string cov_path = paths::cache("cov_") + target + ".png";

        ImageRGBA* avt_img = AssetManager::get("avt_" + target);
        if (!avt_img && !avt_url.empty()) {
            std::ifstream t(avt_path);
            if (!t.good()) api.download_image(avt_url, avt_path);
            t.close();
            avt_img = AssetManager::load("avt_" + target, avt_path);
        }

        ImageRGBA* cov_img = AssetManager::get("cov_" + target);
        if (!cov_img && !cov_url.empty()) {
            std::ifstream t(cov_path);
            if (!t.good()) api.download_image(cov_url, cov_path);
            t.close();
            cov_img = AssetManager::load("cov_" + target, cov_path);
        }

        static std::atomic<int> ctr{0};
        std::string out = paths::cache("info_") + std::to_string(ctr++) + ".png";

        const int W = 1100;
        const int H = 760;
        std::vector<uint8_t> canvas;
        TextRenderer::init_canvas(canvas, W, H, 15, 23, 42);

        if (cov_img && cov_img->valid()) {
            TextRenderer::draw_image_aspect_cover(canvas, W, H, cov_img, 0, 0, W, H);
            TextRenderer::blur_region(canvas, W, H, 6);
            TextRenderer::darken_region(canvas, W, H, 0, 0, W, H, 165);
        } else {
            TextRenderer::fill_linear_gradient(canvas, W, H, 0, H, 37, 99, 235, 15, 23, 42);
        }

        TextRenderer::draw_rounded_rect_shadow(canvas, W, H, 28, 108, W - 56, H - 136, 28, 12, 95);
        TextRenderer::draw_rounded_rect_filled(canvas, W, H, 28, 108, W - 56, H - 136, 28, 17, 24, 39, 215);
        TextRenderer::draw_rounded_rect_outline(canvas, W, H, 28, 108, W - 56, H - 136, 28, 2, 139, 92, 246, 210);

        std::string ttl = "THÔNG TIN NGƯỜI DÙNG";
        int title_w = TextRenderer::measure_text_width(48, ttl);
        TextRenderer::draw_text_shadow(canvas, W, H, (W - title_w) / 2, 68, 48, 250, 204, 21, ttl);

        // Left profile column
        int avt_cx = 215;
        int avt_cy = 305;
        int avt_r = 122;
        TextRenderer::draw_circle_filled(canvas, W, H, avt_cx, avt_cy, avt_r, 51, 65, 85, 255);
        if (avt_img && avt_img->valid()) TextRenderer::draw_circle_image(canvas, W, H, avt_img, avt_cx, avt_cy, avt_r);
        TextRenderer::draw_circle_outline_rainbow(canvas, W, H, avt_cx, avt_cy, avt_r + 4, 6);

        int dot_x = avt_cx + (int)(avt_r * 0.72);
        int dot_y = avt_cy + (int)(avt_r * 0.72);
        TextRenderer::draw_circle_filled(canvas, W, H, dot_x, dot_y, 20, 255, 255, 255, 255);
        TextRenderer::draw_circle_filled(canvas, W, H, dot_x, dot_y, 14, 34, 197, 94, 255);

        auto name_lines = TextRenderer::wrap_text(34, name, 330);
        if (name_lines.size() > 2) {
            name_lines.resize(2);
            name_lines[1] = safe_utf8_truncate(name_lines[1], 18) + "...";
        }
        int ny = avt_cy + avt_r + 54;
        for (const auto& nl : name_lines) {
            int nw = TextRenderer::measure_text_width(34, nl);
            TextRenderer::draw_text_shadow(canvas, W, H, avt_cx - nw / 2, ny, 34, 255, 255, 255, nl);
            ny += 42;
        }
        std::string uid_tag = "UID: " + target;
        int uidw = TextRenderer::measure_text_width(20, uid_tag);
        TextRenderer::draw_text_on_canvas(canvas, W, H, avt_cx - uidw / 2, ny + 4, 20, 148, 163, 184, uid_tag);

        // Vertical separator
        TextRenderer::draw_line(canvas, W, H, 405, 145, 405, H - 150, 148, 163, 184, 1, 70);

        // Right info rows
        int info_x = 440;
        int info_y = 195;
        int lh = 64;
        int sz = 30;
        struct Row { std::string icon; std::string label; std::string value; };
        std::vector<Row> rows = {
            {"📛", "Zalo Name: ", safe_utf8_truncate(zname, 32)},
            {"🏷️", "Username: ", safe_utf8_truncate(uname, 32)},
            {"📞", "Điện thoại: ", safe_utf8_truncate(phone, 24)},
            {"⚧",  "Giới tính: ", gender},
            {"📅", "Tạo TK: ", created},
            {"⏰", "HĐ cuối: ", lastact},
        };
        int yy = info_y;
        for (const auto& r : rows) {
            TextRenderer::draw_text_on_canvas(canvas, W, H, info_x, yy, sz, 148, 163, 184, r.icon + " " + r.label);
            int lw = TextRenderer::measure_text_width(sz, r.icon + " " + r.label);
            TextRenderer::draw_text_on_canvas(canvas, W, H, info_x + lw, yy, sz, 248, 250, 252, r.value);
            yy += lh;
        }

        if (!status.empty()) {
            TextRenderer::draw_line(canvas, W, H, 65, H - 132, W - 65, H - 132, 139, 92, 246, 2, 150);
            auto st_lines = TextRenderer::wrap_text(26, "💬 " + status, W - 150);
            if (st_lines.size() > 2) st_lines.resize(2);
            int sy = H - 88;
            for (const auto& sl : st_lines) {
                TextRenderer::draw_text_on_canvas(canvas, W, H, 70, sy, 26, 186, 230, 253, sl);
                sy += 34;
            }
        }

        if (TextRenderer::save_canvas(canvas, W, H, out)) {
            api.send_image_async(out, tid, tt);
            std::cout << "[Handler] info: " << out << "\n";
        } else {
            reply_text("❌ Render thất bại", tid, tt);
        }
    };

    commands["nap"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) { reply_text("❌ Cần đăng nhập", tid, tt); return; }
        if (args.size() < 2) { reply_text("❌ Cú pháp: /nap <số tiền>. VD: /nap 100k", tid, tt); return; }
        cpp_int wallet_before = econ.get_wallet(uid);
        cpp_int bank_before   = econ.get_bank(uid);
        cpp_int a = parse_bet_amount(args[1], wallet_before);
        if (a <= 0) { reply_text("❌ Số tiền không hợp lệ. VD: 10k, 5m, 1b, all, 50%", tid, tt); return; }
        if (wallet_before < a) {
            reply_text("Số dư trong ví chỉ có " + money_str(wallet_before) + " VNĐ.\n❌❌❌", tid, tt); return;
        }
        econ.deposit(uid, a);
        cpp_int wallet_after = econ.get_wallet(uid);
        cpp_int bank_after   = econ.get_bank(uid);
        std::string msg;
        msg += "🔄 Giao dịch nạp tiền thành công!";
        msg += "\n\n💰 Số tiền nạp: " + money_str(a) + " VNĐ";
        msg += "\n\n📊 Biến động số dư:";
        msg += "\n💼 Ví cá nhân:";
        msg += "\n- Trước: " + money_str(wallet_before) + " VNĐ";
        msg += "\n- Sau: " + money_str(wallet_after) + " VNĐ";
        msg += "\n\n🤖 Tài khoản Bot Zalo:";
        msg += "\n- Trước: " + money_str(bank_before) + " VNĐ";
        msg += "\n- Sau: " + money_str(bank_after) + " VNĐ";
        msg += "\n✅✅✅";
        reply_text(msg, tid, tt);
    };
    commands["rut"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) { reply_text("❌ Cần đăng nhập", tid, tt); return; }
        if (args.size() < 2) { reply_text("❌ Cú pháp: /rut <số tiền>. VD: /rut 100k", tid, tt); return; }
        cpp_int wallet_before = econ.get_wallet(uid);
        cpp_int bank_before   = econ.get_bank(uid);
        cpp_int a = parse_bet_amount(args[1], bank_before);
        if (a <= 0) { reply_text("❌ Số tiền không hợp lệ. VD: 10k, 5m, 1b, all, 50%", tid, tt); return; }
        if (bank_before < a) {
            reply_text("Số dư VNĐ trong tài khoản Bot Zalo chỉ có " + money_str(bank_before) + " VNĐ.\n❌❌❌", tid, tt); return;
        }
        econ.withdraw(uid, a);
        cpp_int wallet_after = econ.get_wallet(uid);
        cpp_int bank_after   = econ.get_bank(uid);
        std::string msg;
        msg += "🔄 Giao dịch rút tiền thành công!";
        msg += "\n\n💰 Số tiền rút: " + money_str(a) + " VNĐ";
        msg += "\n\n📊 Biến động số dư:";
        msg += "\n🤖 Tài khoản Bot Zalo:";
        msg += "\n- Trước: " + money_str(bank_before) + " VNĐ";
        msg += "\n- Sau: " + money_str(bank_after) + " VNĐ";
        msg += "\n\n💼 Ví cá nhân:";
        msg += "\n- Trước: " + money_str(wallet_before) + " VNĐ";
        msg += "\n- Sau: " + money_str(wallet_after) + " VNĐ";
        msg += "\n✅✅✅";
        reply_text(msg, tid, tt);
    };
    commands["command"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        std::string sub = (args.size() > 1) ? args[1] : "";
        std::transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (sub == "admin") {
            if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin mới xem được lệnh này.", tid, tt); return; }
            std::string msg = "🛠️ LỆNH ADMIN:\n/on, /off, /bot, /game, /debug, /image\n/groupprotect, /mute, /unmute, /kick, /warn\n/setad, /listadmin, /setprefix, /setmoney, /sethu, /buff, /nerf, /txend, /undo, /boost";
            reply_text(msg, tid, tt);
        } else {
            std::string msg = "📖 DANH SÁCH LỆNH:\n";
            msg += "🎮 Game: /taixiu (tx), /chanle (cl), /baucua, /keobuabao (kbb), /vietlott, /soicau, /caro, /doanso, /noitu, /doantu\n";
            msg += "🌾 Nông trại: /nongtrai (ntr)\n";
            msg += "💰 Kinh tế: /bank, /nap, /rut, /vay, /tra, /chuyen, /daily, /top\n";
            msg += "👤 Thông tin: /signup, /login, /logout, /whoami, /info, /mycard, /group, /detail, /rank\n";
            msg += "✨ Tiện ích: /dich, /thoitiet, /voice, /qr, /sticker, /girl, /boy, /anime, /cosplay, /cadao, /tarot, /thinh, /truyencuoi, /gay\n";
            msg += "\n👉 Gõ /help [1-4] để xem menu ảnh đẹp | /command admin để xem lệnh quản trị.";
            reply_text(msg, tid, tt);
        }
    };

    static std::map<std::string, int> g_doanso_targets;
    // /noitu — Word Chain (text co dau, bot di truoc)
    commands["noitu"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        (void)args;
        auto& wc = getWordChain();
        if (!wc.isLoaded()) {
            reply_text("\u274C Kh\u00F4ng n\u1EA1p \u0111\u01B0\u1EE3c t\u1EEB \u0111i\u1EC3n (data/dictionary.txt).", tid, tt);
            return;
        }

        std::string raw = ctx().msg_text;
        size_t sp = raw.find_first_of(" \t\n");
        std::string rest = (sp == std::string::npos) ? "" : raw.substr(sp + 1);
        size_t b = rest.find_first_not_of(" \t\r\n");
        if (b != std::string::npos) rest = rest.substr(b); else rest.clear();
        size_t e = rest.find_last_not_of(" \t\r\n");
        if (e != std::string::npos) rest = rest.substr(0, e + 1); else rest.clear();
        std::string rest_norm = vi::normalizeWord(rest);

        if (rest_norm == "cancel" || rest_norm == "huy" || rest_norm == "end" || rest_norm == "stop") {
            if (wc.cancelGame(tid)) reply_text("\U0001F524 \u0110\u00E3 hu\u1EF7 v\u00E1n n\u1ED1i t\u1EEB.", tid, tt);
            else                    reply_text("\U0001F524 Ch\u01B0a c\u00F3 v\u00E1n n\u00E0o.", tid, tt);
            return;
        }

        if (rest_norm == "stats" || rest_norm == "info") {
            auto& d = wc.dictionary();
            std::string msg = "\U0001F4CA N\u1ED0I T\u1EEA - TH\u1ED0NG K\u00CA\n";
            msg += "T\u1ED5ng t\u1EEB: " + std::to_string(d.size()) + "\n";
            msg += "Th\u00F4ng d\u1EE5ng: " + std::to_string(d.commonCount()) + "\n";
            msg += "Kh\u00F3/hi\u1EBFm: " + std::to_string(d.size() - d.commonCount()) + "\n";
            auto* st = wc.getState(tid);
            if (st) {
                msg += "\nV\u00E1n hi\u1EC7n t\u1EA1i: " + std::to_string(st->chain_length) + " t\u1EEB\n";
                msg += "T\u1EEB cu\u1ED1i: " + st->last_phrase + "\n";
                if (!st->last_phrase.empty()) {
                    auto syl = vi::splitSyllables(st->last_phrase);
                    msg += "C\u1EA7n b\u1EAFt \u0111\u1EA7u: " + syl.back();
                }
            } else {
                msg += "\nCh\u01B0a c\u00F3 v\u00E1n.";
            }
            reply_text(msg, tid, tt);
            return;
        }

        if (rest_norm == "help") {
            reply_text(
                "\U0001F524 N\u1ED0I T\u1EEA - H\u01AF\u1EDANG D\u1EAAN\n\n"
                "/noitu \u2014 b\u1EAFt \u0111\u1EA7u v\u00E1n m\u1EDBi (bot m\u1EDF m\u00E0n)\n"
                "/noitu <c\u1EE5m t\u1EEB> \u2014 \u0111\u00E1nh (2 \u00E2m ti\u1EBFt)\n"
                "/noitu cancel \u2014 hu\u1EF7 v\u00E1n\n"
                "/noitu stats \u2014 th\u1ED1ng k\u00EA\n"
                "/noitu help \u2014 tr\u1EE3 gi\u00FAp\n\n"
                "Khi v\u00E1n \u0111ang ch\u01A1i, ch\u1EC9 c\u1EA7n g\u00F5 c\u1EE5m t\u1EEB (kh\u00F4ng c\u1EA7n /noitu).\n"
                "Lu\u1EADt: n\u1ED1i t\u1EEB cu\u1ED1i c\u1EE7a c\u1EE5m tr\u01B0\u1EDBc \u2192 t\u1EEB \u0111\u1EA7u c\u1EE7a c\u1EE5m m\u1EDBi.\n"
                "C\u1EA7n C\u00D3 D\u1EA4U. VD: \u0103n u\u1ED1ng (kh\u00F4ng ph\u1EA3i an uong).",
                tid, tt);
            return;
        }

        if (rest_norm.rfind("add ", 0) == 0) {
            if (!perm.is_admin(uid)) { reply_text("\u274C Ch\u1EC9 admin bot", tid, tt); return; }
            std::string w = rest_norm.substr(4);
            if (wc.dictionary().addWord(w)) {
                if (wc.saveDictionary()) reply_text("\u2705 \u0110\u00E3 th\u00EAm: " + w, tid, tt);
                else                     reply_text("\u26A0\uFE0F \u0110\u00E3 th\u00EAm nh\u01B0ng kh\u00F4ng ghi \u0111\u01B0\u1EE3c file.", tid, tt);
            } else {
                reply_text("\u274C Kh\u00F4ng th\u00EAm \u0111\u01B0\u1EE3c (sai \u0111\u1ECBnh d\u1EA1ng ho\u1EB7c \u0111\u00E3 t\u1ED3n t\u1EA1i).", tid, tt);
            }
            return;
        }

        if (rest_norm.rfind("remove ", 0) == 0) {
            if (!perm.is_admin(uid)) { reply_text("\u274C Ch\u1EC9 admin bot", tid, tt); return; }
            std::string w = rest_norm.substr(7);
            if (wc.dictionary().removeWord(w)) {
                if (wc.saveDictionary()) reply_text("\u2705 \u0110\u00E3 xo\u00E1: " + w, tid, tt);
                else                     reply_text("\u26A0\uFE0F \u0110\u00E3 xo\u00E1 nh\u01B0ng kh\u00F4ng ghi \u0111\u01B0\u1EE3c file.", tid, tt);
            } else {
                reply_text("\u274C T\u1EEB kh\u00F4ng c\u00F3 trong t\u1EEB \u0111i\u1EC3n.", tid, tt);
            }
            return;
        }

        if (rest_norm == "sort") {
            if (!perm.is_admin(uid)) { reply_text("\u274C Ch\u1EC9 admin bot", tid, tt); return; }
            if (wc.sortDictionary()) reply_text("\u2705 \u0110\u00E3 s\u1EAFp x\u1EBFp l\u1EA1i t\u1EEB \u0111i\u1EC3n.", tid, tt);
            else                     reply_text("\u274C Kh\u00F4ng ghi \u0111\u01B0\u1EE3c file.", tid, tt);
            return;
        }

        if (rest_norm.empty()) {
            if (wc.hasActiveGame(tid)) {
                auto* st = wc.getState(tid);
                auto syl = vi::splitSyllables(st->last_phrase);
                reply_text(
                    "\U0001F524 N\u1ED0I T\u1EEA \u0111ang ch\u01A1i\n"
                    "Chu\u1ED7i: " + std::to_string(st->chain_length) + " t\u1EEB\n"
                    "T\u1EEB cu\u1ED1i: " + st->last_phrase + "\n"
                    "C\u1EA7n b\u1EAFt \u0111\u1EA7u b\u1EB1ng: " + syl.back() + "\n\n"
                    "G\u00F5 c\u1EE5m t\u1EEB ti\u1EBFp theo (kh\u00F4ng c\u1EA7n /noitu).",
                    tid, tt);
            } else {
                PlayOutcome o = wc.startGameBotFirst(tid, uid);
                if (o.result == PlayResult::OK) reply_text(o.bot_reply, tid, tt);
                else reply_text("\u274C " + o.error_msg, tid, tt);
            }
            return;
        }

        if (!wc.hasActiveGame(tid)) wc.startGameBotFirst(tid, uid);

        PlayOutcome outcome = wc.playWord(tid, uid, rest);
        switch (outcome.result) {
            case PlayResult::NOT_ACTIVE:
                reply_text("\U0001F524 Ch\u01B0a c\u00F3 v\u00E1n. G\u00F5 /noitu \u0111\u1EC3 b\u1EAFt \u0111\u1EA7u.", tid, tt); break;
            case PlayResult::WORD_EMPTY:
            case PlayResult::WORD_NOT_TWO_SYLLABLES:
                reply_text("\u274C C\u1EA7n \u0111\u00FAng 2 \u00E2m ti\u1EBFt. VD: \"\u0103n u\u1ED1ng\".", tid, tt); break;
            case PlayResult::WORD_NOT_IN_DICT:
                reply_text("\u274C T\u1EEB kh\u00F4ng c\u00F3 trong t\u1EEB \u0111i\u1EC3n.", tid, tt); break;
            case PlayResult::WORD_WRONG_START:
                reply_text("\u274C Ph\u1EA3i b\u1EAFt \u0111\u1EA7u b\u1EB1ng \"" + outcome.required_start + "\".", tid, tt); break;
            case PlayResult::WORD_ALREADY_USED:
                reply_text("\u274C T\u1EEB n\u00E0y \u0111\u00E3 d\u00F9ng trong v\u00E1n.", tid, tt); break;
            case PlayResult::OK:
            case PlayResult::WIN:
                reply_text(outcome.bot_reply, tid, tt); break;
        }
    };

    commands["bank"] = [this](const auto& args, const auto& uid,
                              const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) {
            reply_text("❌ Cần đăng nhập", tid, tt);
            return;
        }
        if (args.size() < 3) {
            reply_text("🏦 Bank: " + money_str(econ.get_bank(uid)) +
                       "\n💸 Nợ: " + money_str(econ.get_debt(uid)) +
                       "\n\n📖 Cú pháp: /bank <số tiền> @user",
                       tid, tt);
            return;
        }
        auto is_uid = [](const std::string& s) {
            if (s.size() < 10) return false;
            return std::all_of(s.begin(), s.end(), ::isdigit);
        };
        std::string target;
        cpp_int a = 0;
        if (is_uid(args[1])) {
            target = args[1];
            a = parse_money(args[2]);
        } else {
            a = parse_money(args[1]);
            target = args[2];
        }
        if (a <= 0) { reply_text("❌ Số tiền không hợp lệ", tid, tt); return; }
        if (target == uid) { reply_text("❌ Không thể tự chuyển", tid, tt); return; }
        // Check nguoi nhan da dang nhap chua (tranh mat tien khi B login sau)
        if (!account.is_logged_in(target)) {
            reply_text("❌ " + format_user(target) + " chưa đăng nhập. Không thể chuyển tiền.", tid, tt);
            return;
        }
        cpp_int my_before = econ.get_bank(uid);
        if (my_before < a) { reply_text("❌ Không đủ tiền. Bank: " + money_str(my_before), tid, tt); return; }
        cpp_int target_before = econ.get_bank(target);
        if (econ.transfer(uid, target, a)) {
            std::string msg = "💸 Chuyển khoản thành công!";
            msg += "\n\n💰 Số tiền: " + money_str(a) + " VNĐ";
            msg += "\n👤 Người nhận: " + format_user(target);
            msg += "\n\n📊 Biến động số dư:";
            msg += "\n- Bạn: " + money_str(my_before) + " → " + money_str(econ.get_bank(uid));
            msg += "\n- " + format_user(target) + ": " + money_str(target_before) + " → " + money_str(econ.get_bank(target));
            msg += "\n✅✅✅";
            reply_text(msg, tid, tt);
        } else {
            reply_text("❌ Chuyển thất bại", tid, tt);
        }
    };
    // ============================================================
    // /undo â€” thu há»“i tin nháº¯n Ä‘Æ°á»£c reply hoáº·c N tin gáº§n nháº¥t
    // ============================================================
    // ============================================================
    // /boost - admin don sach cache folder
    // ============================================================
    commands["boost"] = [this](const auto& args, const auto& uid,
                                const auto& tid, int tt, bool) {
        (void)args;
        if (!perm.is_admin(uid)) {
            reply_text("\u274C Ch\u1EC9 admin bot d\u00F9ng \u0111\u01B0\u1EE3c", tid, tt);
            return;
        }

        namespace fs = std::filesystem;
        std::string cache_dir = "cache";

        if (!fs::exists(cache_dir) || !fs::is_directory(cache_dir)) {
            reply_text("\U0001F4E6 Cache folder ch\u01B0a t\u1ED3n t\u1EA1i ho\u1EB7c tr\u1ED1ng.", tid, tt);
            return;
        }

        int ok_count = 0;
        int fail_count = 0;
        long long freed_bytes = 0;
        std::string skipped_names;

        try {
            for (auto& entry : fs::directory_iterator(cache_dir)) {
                if (!entry.is_regular_file()) continue;
                std::string name = entry.path().filename().string();
                if (name == ".gitignore") continue;   // giu lai

                auto sz = entry.file_size();
                try {
                    fs::remove(entry.path());
                    ok_count++;
                    freed_bytes += (long long)sz;
                } catch (...) {
                    fail_count++;
                    if (skipped_names.size() < 200) {
                        if (!skipped_names.empty()) skipped_names += ", ";
                        skipped_names += name;
                    }
                }
            }
        } catch (const std::exception& e) {
            reply_text("\u274C L\u1ED7i qu\u00E9t cache: " + std::string(e.what()), tid, tt);
            return;
        }

        double freed_mb = freed_bytes / 1024.0 / 1024.0;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f", freed_mb);

        std::string msg = "\U0001F680 BOOST - D\u1ECDn cache\n\n";
        msg += "\U0001F4C1 \u0110\u00E3 xo\u00E1: " + std::to_string(ok_count) + " file\n";
        msg += "\U0001F4BE Gi\u1EA3i ph\u00F3ng: " + std::string(buf) + " MB\n";
        if (fail_count > 0) {
            msg += "\u26A0\uFE0F B\u1ECF qua: " + std::to_string(fail_count) + " file \u0111ang b\u1ECB lock\n";
        }
        if (ok_count == 0 && fail_count == 0) {
            msg += "\u2728 Cache \u0111\u00E3 s\u1EA1ch, kh\u00F4ng c\u00F3 g\u00EC \u0111\u1EC3 d\u1ECDn.";
        }
        reply_text(msg, tid, tt);
        std::cout << "[Boost] uid=" << uid << " ok=" << ok_count
                  << " fail=" << fail_count << " freed=" << buf << "MB\n";
    };
    commands["undo"] = [this](const auto& args, const auto& uid,
                              const auto& tid, int tt, bool) {
        if (!perm.is_admin(uid)) {
            reply_text("\u274C Ch\u1EC9 admin bot d\u00F9ng \u0111\u01B0\u1EE3c", tid, tt);
            return;
        }
        if (ctx().quote.has_quote) {
            const auto& q = ctx().quote;
            bool is_bot_msg = (q.uid_from == api.get_own_id());
            bool ok = false;
            if (is_bot_msg) {
                auto r = api.undo_message(q.msg_id, q.cli_msg_id, tid, tt);
                ok = r.value("ok", false);
            } else {
                if (tt == 1 && !group.is_bot_admin_in_group(tid)) {
                    reply_text("\u274C Bot ph\u1EA3i l\u00E0 admin nh\u00F3m \u0111\u1EC3 xo\u00E1 tin ng\u01B0\u1EDDi kh\u00E1c", tid, tt);
                    return;
                }
                auto r = api.delete_message(q.msg_id, q.cli_msg_id, q.uid_from, tid, tt);
                ok = r.value("ok", false);
            }
            if (ok) reply_text("\u2705 \u0110\u00E3 xo\u00E1 tin nh\u1EAFn \u0111\u01B0\u1EE3c reply", tid, tt);
            else    reply_text("\u274C Kh\u00F4ng xo\u00E1 \u0111\u01B0\u1EE3c", tid, tt);
            return;
        }
        if (args.size() <= 1) {
            reply_text("\u274C C\u1EA7n reply v\u00E0o tin nh\u1EAFn c\u1EA7n xo\u00E1, ho\u1EB7c d\u00F9ng /undo <n>", tid, tt);
            return;
        }
        auto is_uid = [](const std::string& s) {
            if (s.size() < 10) return false;
            return std::all_of(s.begin(), s.end(), ::isdigit);
        };
        int n = 1;
        std::string target_uid = uid;
        if (is_uid(args[1])) {
            target_uid = args[1];
            if (args.size() > 2) {
                try { n = std::stoi(args[2]); } catch (...) { n = 1; }
            }
        } else {
            try { n = std::stoi(args[1]); } catch (...) { n = 1; }
        }
        if (n < 1) n = 1;
        if (n > 20) n = 20;
        std::vector<CachedMessage> victims;
        {
            std::lock_guard<std::mutex> lk(msgs_mtx_);
            auto it = thread_msgs_.find(tid);
            if (it != thread_msgs_.end()) {
                auto& dq = it->second;
                for (auto rit = dq.rbegin();
                     rit != dq.rend() && (int)victims.size() < n; ++rit) {
                    if (rit->uid_from == target_uid) {
                        victims.push_back(*rit);
                    }
                }
            }
        }
        if (victims.empty()) {
            reply_text("\u274C Kh\u00F4ng t\u00ECm th\u1EA5y tin nh\u1EAFn ph\u00F9 h\u1EE3p trong cache g\u1EA7n \u0111\u00E2y", tid, tt);
            return;
        }
        bool bot_is_self = (target_uid == api.get_own_id());
        if (!bot_is_self && tt == 1 && !group.is_bot_admin_in_group(tid)) {
            reply_text("\u274C Bot ph\u1EA3i l\u00E0 admin nh\u00F3m \u0111\u1EC3 xo\u00E1 tin ng\u01B0\u1EDDi kh\u00E1c", tid, tt);
            return;
        }
        int ok_count = 0;
        for (const auto& v : victims) {
            bool ok = false;
            if (v.uid_from == api.get_own_id()) {
                auto r = api.undo_message(v.msg_id, v.cli_msg_id, tid, tt);
                ok = r.value("ok", false);
            } else {
                auto r = api.delete_message(v.msg_id, v.cli_msg_id, v.uid_from, tid, tt);
                ok = r.value("ok", false);
            }
            if (ok) ok_count++;
        }
        reply_text("\u2705 \u0110\u00E3 xo\u00E1 " + std::to_string(ok_count) + "/" +
                   std::to_string(victims.size()) + " tin", tid, tt);
    };


    // ============================================================
    // /txend - admin force ket thuc phien taixiu cua thread
    // ============================================================
    commands["txend"] = [this](const auto& args, const auto& uid,
                              const auto& tid, int tt, bool) {
        if (!perm.is_admin(uid)) { reply_text("\u274C Ch\u1EC9 admin bot", tid, tt); return; }
        {
            std::lock_guard<std::mutex> lk(g_taixiu_mtx);
            auto it = g_taixiu_sessions.find(tid);
            if (it == g_taixiu_sessions.end() || !it->second.active) {
                reply_text("\u26A0\uFE0F Kh\u00F4ng c\u00F3 phi\u00EAn t\u00E0i x\u1EC9u n\u00E0o", tid, tt);
                return;
            }
        }
        reply_text("\u26A1 Force k\u1EBFt th\u00FAc phi\u00EAn...", tid, tt);
        end_taixiu_session(tid);
    };

    commands["vay"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) { reply_text("❌ Cần đăng nhập", tid, tt); return; }
        if (args.size() < 2) { reply_text("❌ vay <số tiền>", tid, tt); return; }
        cpp_int a = parse_money(args[1]);
        if (econ.borrow(uid, a)) reply_text("✅ Đã vay " + money_str(a), tid, tt);
        else reply_text("❌ Vay không hợp lệ (tối đa 10,000,000,000)", tid, tt);
    };
        commands["tra"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) { reply_text("❌ Cần đăng nhập", tid, tt); return; }
        cpp_int current_debt = econ.get_debt(uid);
        if (current_debt <= 0) { reply_text("✅ Bạn không có khoản nợ nào!", tid, tt); return; }
        
        if (args.size() < 2) { reply_text("❌ Cú pháp: tra <số tiền> hoặc tra all", tid, tt); return; }
        
        cpp_int a = 0;
        std::string arg1 = args[1];
        std::transform(arg1.begin(), arg1.end(), arg1.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        
        if (arg1 == "all") {
            a = current_debt;
        } else {
            a = parse_money(args[1]);
        }
        
        if (a <= 0) { reply_text("❌ Số tiền không hợp lệ", tid, tt); return; }
        if (a > current_debt) { a = current_debt; } // Tự động giới hạn nếu trả thừa
        
        if (econ.repay(uid, a)) {
            cpp_int remaining_debt = econ.get_debt(uid);
            std::string msg = "✅ Đã trả: " + money_str(a);
            msg += "\n📉 Nợ còn lại: " + money_str(remaining_debt);
            reply_text(msg, tid, tt);
        } else {
            reply_text("❌ Trả nợ thất bại", tid, tt);
        }
    };
    commands["chuyen"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) { reply_text("❌ Cần đăng nhập", tid, tt); return; }
        if (args.size() < 3) { reply_text("❌ chuyen <@user> <số tiền>", tid, tt); return; }
        cpp_int a = parse_money(args[2]);
        if (econ.transfer(uid, args[1], a)) reply_text("✅ Đã chuyển " + money_str(a) + " đến " + format_user(args[1]), tid, tt);
        else reply_text("❌ Chuyển thất bại", tid, tt);
    };
    commands["chanle"] = [this](const auto& args, const auto& uid,
                             const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) {
            reply_text("C\u1EA7n \u0111\u0103ng nh\u1EADp. D\u00F9ng /login <user> <pass>", tid, tt);
            return;
        }
        if (args.size() < 3) {
            reply_text("ℹ️ Cú pháp: /chanle <chẵn|lẻ> <tiền cược>\nVí dụ: /cl c 20k, /chanle le 5m", tid, tt);
            return;
        }
        std::string choice = args[1];
        std::transform(choice.begin(), choice.end(), choice.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (choice == "c" || choice == "chan") choice = "chan";
        if (choice == "l" || choice == "le") choice = "le";
        if (choice != "chan" && choice != "le") {
            reply_text("⚠️ Vui lòng chọn chẵn hoặc lẻ (vd: /chanle c 50k)", tid, tt);
            return;
        }
        cpp_int bal0 = econ.get_bank(uid);
        cpp_int bet = parse_bet_amount(args[2], bal0);
        if (bet < 0) {
            reply_text("⚠️ Số tiền cược không hợp lệ. Ví dụ: 10000, 10k, 5m, 1b", tid, tt);
            return;
        }
        if (bet < 1000) { reply_text("C\u01B0\u1EE3c t\u1ED1i thi\u1EC3u 1.000", tid, tt); return; }
        if (bet > 1000000000000000LL) {
            reply_text("⚠️ Cược tối đa 1.000.000.000.000.000 VNĐ", tid, tt); return;
        }
        cpp_int bal_before = bal0;
        if (bal_before < bet) {
            reply_text("❌ Bạn không đủ tiền! Số dư ngân hàng: " + money_str(bal_before) + " VNĐ", tid, tt);
            return;
        }
        econ.place_bet(uid, bet);
        int d1 = roll_dice_mt(6);
        int d2 = roll_dice_mt(6);
        int d3 = roll_dice_mt(6);
        int total = d1 + d2 + d3;
        bool is_even = (total % 2 == 0);
        bool bet_even = (choice == "chan");
        bool won = (is_even == bet_even);
        bool is_triple = (d1 == d2 && d2 == d3);
        bool is_triple_even = is_triple && (d1 % 2 == 0);
        bool is_jackpot = is_triple && ((is_triple_even && bet_even) || (!is_triple_even && !bet_even));
        bool missed_jackpot = is_triple && !is_jackpot;

        cpp_int winnings = 0;
        cpp_int jackpot_win = 0;
        cpp_int cur_jackpot = game.get_chanle_jackpot();

        if (is_jackpot) {
            jackpot_win = cur_jackpot;
            cpp_int max_win = bet * 1000;
            if (jackpot_win > max_win) {
                jackpot_win = max_win;
                game.add_chanle_jackpot(-max_win);
            } else {
                game.set_chanle_jackpot(1000000);
            }
            winnings = bet * 19 / 10 + jackpot_win;
            econ.add_winnings(uid, winnings);
        } else if (won) {
            winnings = bet * 19 / 10;
            econ.add_winnings(uid, winnings);
        } else {
            econ.record_loss(uid, bet);
            int pct = roll_dice_mt(31) + 29;
            cpp_int contrib = bet * pct / 100;
            game.add_chanle_jackpot(contrib);
        }
        save_husks();

        cpp_int net = winnings - bet;
        cpp_int bal_after = bal_before + net;
        game.push_chanle_history(is_even);

        std::vector<int> dice_vec;
        dice_vec.push_back(d1);
        dice_vec.push_back(d2);
        dice_vec.push_back(d3);

        static std::atomic<int> img_ctr{0};
        std::string img_path = paths::cache("chanle_") + std::to_string(img_ctr++) + ".png";

        game_render::render_chanle_image(
            dice_vec, total, choice, bet, is_jackpot,
            game.get_chanle_history(), winnings, img_path);

std::string cap;
        std::string choice_disp = bet_even ? "Chẵn ⚪" : "Lẻ ⚫";
        cap += "[KẾT QUẢ CHẴN LẺ]";
        cap += "\n• Đặt: " + choice_disp;
        cap += "\n• Cược: " + money_str(bet) + " VNĐ";
        if (is_jackpot) {
            cap += "\n🎰 NỔ HŨ! +" + money_str(jackpot_win) + " VNĐ";
        } else if (missed_jackpot) {
            cap += "\n😭 TRƯỢT HŨ! Ba số giống nhau nhưng đặt sai.";
        } else if (net > 0) {
            cap += "\n🎉 THẮNG CƯỢC: +" + money_str(net) + " VNĐ";
        } else {
            cap += "\n💸 THUA CƯỢC: " + money_str(net) + " VNĐ";
        }
        cap += "\n💳 Số dư: " + money_str(bal_before) + " → " + money_str(bal_after) + " VNĐ";
        cap += "\n🎰 HŨ HIỆN TẠI: " + money_str(game.get_chanle_jackpot()) + " VNĐ";
        api.send_message_with_image_async(cap, img_path, tid, tt);
    };

    commands["taixiu"] = [this](const auto& args, const auto& uid,
                             const auto& tid, int tt, bool) {
        if (!account.is_logged_in(uid) && !g_debug_mode) {
            reply_text("C\u1EA7n \u0111\u0103ng nh\u1EADp. D\u00F9ng /login <user> <pass>", tid, tt);
            return;
        }
        if (args.size() < 3) { reply_text("C\u00FA ph\u00E1p: /taixiu <t\u00E0i|x\u1EC9u> <c\u01B0\u1EE3c>", tid, tt); return; }
        std::string choice = args[1];
        std::transform(choice.begin(), choice.end(), choice.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (choice == "t" || choice == "tai") choice = "tai";
        if (choice == "x" || choice == "xiu") choice = "xiu";
        if (choice != "tai" && choice != "xiu") { reply_text("Ch\u1ECDn t\u00E0i ho\u1EB7c x\u1EC9u", tid, tt); return; }
        cpp_int bal = econ.get_bank(uid);
        cpp_int bet = parse_bet_amount(args[2], bal);
        if (bet < 0) { reply_text("S\u1ED1 ti\u1EC1n kh\u00F4ng h\u1EE3p l\u1EC7", tid, tt); return; }
        if (bet < 1000) { reply_text("C\u01B0\u1EE3c t\u1ED1i thi\u1EC3u 1.000", tid, tt); return; }
        if (bal < bet) { reply_text("Kh\u00F4ng \u0111\u1EE7 ti\u1EC1n. Bank: " + money_str(bal), tid, tt); return; }
        if (!econ.place_bet(uid, bet)) { reply_text("Kh\u00F4ng \u0111\u1EB7t c\u01B0\u1EE3c \u0111\u01B0\u1EE3c", tid, tt); return; }
        std::time_t now = std::time(nullptr);
        int remaining = 60;
        bool dup = false;
        {
            std::lock_guard<std::mutex> lk(g_taixiu_mtx);
            auto& sess = g_taixiu_sessions[tid];
            if (!sess.active) {
                sess.active = true;
                sess.start_time = now;
                sess.end_time = now + 60;
                sess.last_update = now;
                sess.players.clear();
                std::cout << "[TX] Session start thread=" << tid << "\n";
            }
            for (auto& p : sess.players) if (p.uid == uid) { dup = true; break; }
            if (!dup) {
                TaixiuPlayer tp;
                tp.uid = uid;
                tp.name = format_user(uid);
                tp.bet_type = choice;
                tp.amount = bet;
                sess.players.push_back(tp);
                remaining = (int)(sess.end_time - now);
            }
        }
        if (dup) { econ.buff(uid, bet); reply_text("B\u1EA1n \u0111\u00E3 c\u01B0\u1EE3c phi\u00EAn n\u00E0y r\u1ED3i", tid, tt); return; }
        g_econ_dirty = true;
        std::string choice_str = (choice == "tai") ? "T\u00E0i" : "X\u1EC9u";
        reply_text("\u0110\u00E3 \u0111\u1EB7t c\u01B0\u1EE3c " + money_str(bet) + " VN\u0110 cho " + choice_str +
                   "\nPhi\u00EAn k\u1EBFt th\u00FAc sau " + std::to_string(remaining) + "s", tid, tt);
    };

    commands["cl"] = commands["chanle"];
    commands["tx"] = commands["taixiu"];

    commands["baucua"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) { reply_text("❌ Cần đăng nhập. Dùng /login <user> <pass>", tid, tt); return; }
        if (args.size() < 3 || args.size() % 2 == 0) {
            reply_text("❌ Cú pháp: /baucua <con> <tiền> [<con> <tiền>...]\nVí dụ: /baucua bau 10k cua 20k", tid, tt);
            return;
        }

        std::map<std::string, int> symMap = {
            {"bau", 0}, {"bầu", 0}, {"cua", 1}, {"tom", 2}, {"tôm", 2},
            {"ca", 3}, {"cá", 3}, {"ga", 4}, {"gà", 4}, {"nai", 5}
        };

        std::vector<std::pair<int, cpp_int>> bets;
        cpp_int total_bet = 0;
        cpp_int bal_avail = econ.get_bank(uid);
        for (size_t i = 1; i + 1 < args.size(); i += 2) {
            std::string con = args[i];
            std::transform(con.begin(), con.end(), con.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (!symMap.count(con)) { reply_text("❌ Con vật không hợp lệ: " + args[i] + "\nChọn: bau, cua, tom, ca, ga, nai", tid, tt); return; }
            cpp_int b = parse_bet_amount(args[i + 1], bal_avail - total_bet);
            if (b < 1000) { reply_text("❌ Cược tối thiểu 1.000 VNĐ mỗi cửa!", tid, tt); return; }
            bets.push_back({symMap[con], b});
            total_bet += b;
        }

        if (total_bet <= 0 || bal_avail < total_bet) {
            reply_text("❌ Không đủ tiền trong Bank! Hiện có: " + money_str(bal_avail) + " VNĐ", tid, tt);
            return;
        }

        cpp_int oldBank = bal_avail;
        econ.place_bet(uid, total_bet);

        std::vector<int> dices = { myRandom::RanInt(0, 5), myRandom::RanInt(0, 5), myRandom::RanInt(0, 5) };
        int count[6] = {0};
        for (int d : dices) count[d]++;

        cpp_int total_win = 0;
        bool isJackpot = (count[dices[0]] == 3);
        cpp_int husk_add = 0;

        for (auto& pair : bets) {
            int con = pair.first;
            cpp_int bet = pair.second;
            if (count[con] > 0) {
                cpp_int win = bet + (bet * 95 * count[con]) / 100;
                total_win += win;
            } else {
                husk_add += bet * 60 / 100;
            }
        }

        cpp_int husk = econ.get_husk("bau_cua");
        if (husk <= 0) { husk = 1000000; econ.set_husk("bau_cua", husk); }
        cpp_int jackpot_win = 0;
        bool hit_jackpot = false;
        if (isJackpot) {
            for (auto& pair : bets) {
                if (pair.first == dices[0]) { hit_jackpot = true; break; }
            }
        }
        if (hit_jackpot) {
            cpp_int max_jackpot = total_bet * 1000;
            jackpot_win = std::min(husk, max_jackpot);
            total_win += jackpot_win;
            if (husk > max_jackpot) econ.add_to_husk("bau_cua", -jackpot_win);
            else econ.set_husk("bau_cua", 1000000);
        } else if (husk_add > 0) {
            econ.add_to_husk("bau_cua", husk_add);
        }

        if (total_win > 0) econ.add_winnings(uid, total_win);
        else econ.record_loss(uid, total_bet);
        save_husks();
        g_econ_dirty = true;

        cpp_int newBank = econ.get_bank(uid);
        std::string playerName = format_user(uid);
        std::string msg = "[ " + playerName + " ] đã đặt:\n";
        const char* symNames[] = {"Bầu", "Cua", "Tôm", "Cá", "Gà", "Nai"};
        for (auto& pair : bets) {
            int con = pair.first;
            cpp_int bet = pair.second;
            bool isWin = count[con] > 0;
            msg += "• " + std::string(symNames[con]) + ": " + money_str(bet) + " VNĐ (" + (isWin ? "Thắng x" + std::to_string(count[con]) : "Trượt") + ")\n";
        }
        msg += "Tổng nhận: " + money_str(total_win) + " VNĐ\n";
        if (hit_jackpot) {
            msg += "💥 NỔ HŨ BẦU CUA! +" + money_str(jackpot_win) + " VNĐ\n";
        }
        cpp_int net_win = total_win - total_bet;
        if (net_win > 0) msg += "🎉 Lợi nhuận: +" + money_str(net_win) + " VNĐ\n";
        else if (net_win == 0) msg += "🤝 Hòa vốn!\n";
        else msg += "😢 Thua lỗ: " + money_str(net_win) + " VNĐ\n";
        msg += "💰 Hũ Bầu Cua: " + money_str(econ.get_husk("bau_cua")) + " VNĐ\n";
        msg += "💳 Số dư: " + money_str(oldBank) + " → " + money_str(newBank) + " VNĐ";

        static std::atomic<int> ctr{0};
        std::string imgPath = paths::cache("baucua_") + std::to_string(ctr++) + ".png";
        game_render::render_baucua_result_rich(dices, total_bet, total_win, hit_jackpot, econ.get_husk("bau_cua"), imgPath);

        std::vector<std::tuple<std::string,int,int>> empty;
        api.send_message_with_image_mentions_async(msg, imgPath, tid, tt, empty);
    };

    commands["keobuabao"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) { reply_text("❌ Cần đăng nhập. Dùng /login <user> <pass>", tid, tt); return; }
        if (args.size() < 3) { reply_text("❌ Cú pháp: /kbb <keo|bua|bao> <tiền>", tid, tt); return; }

        std::string choice = args[1];
        std::transform(choice.begin(), choice.end(), choice.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        int p_choice = -1;
        if (choice == "keo" || choice == "kéo" || choice == "k") p_choice = 0;
        else if (choice == "bua" || choice == "búa" || choice == "b") p_choice = 1;
        else if (choice == "bao" || choice == "bào" || choice == "giay" || choice == "giấy") p_choice = 2;
        else { reply_text("❌ Chọn keo, bua hoặc bao!", tid, tt); return; }

        cpp_int oldBank = econ.get_bank(uid);
        cpp_int bet = parse_bet_amount(args[2], oldBank);
        if (bet < 1000) { reply_text("❌ Cược tối thiểu 1.000 VNĐ!", tid, tt); return; }
        if (oldBank < bet) { reply_text("❌ Không đủ tiền! Bank: " + money_str(oldBank) + " VNĐ", tid, tt); return; }

        econ.place_bet(uid, bet);

        int beats[3] = {2, 0, 1};
        int p_beats = beats[p_choice];
        int p_loses_to = -1;
        for (int i = 0; i < 3; i++) {
            if (beats[i] == p_choice) { p_loses_to = i; break; }
        }

        int randVal = myRandom::RanInt(0, 99);
        int b_choice = 0;
        int result = 0;
        if (randVal < 35) {
            b_choice = p_beats;
            result = 0;
        } else if (randVal < 70) {
            b_choice = p_choice;
            result = 1;
        } else {
            b_choice = p_loses_to;
            result = 2;
        }

        const char* kbbNames[] = {"Kéo", "Búa", "Bao"};
        const char* kbbEmojis[] = {"✌️", "✊", "🖐️"};

        cpp_int net_amount = 0;
        if (result == 0) {
            net_amount = bet;
            econ.add_winnings(uid, bet * 2);
        } else if (result == 1) {
            net_amount = 0;
            econ.refund(uid, bet);
        } else {
            net_amount = -bet;
            econ.record_loss(uid, bet);
        }
        g_econ_dirty = true;

        cpp_int newBank = econ.get_bank(uid);
        std::string playerName = format_user(uid);
        std::string resText = "[ " + playerName + " ] chọn " + std::string(kbbNames[p_choice]) + " " + std::string(kbbEmojis[p_choice]) + "\n";
        resText += "Bot chọn " + std::string(kbbNames[b_choice]) + " " + std::string(kbbEmojis[b_choice]) + "\n\n";
        if (result == 0) resText += "🎉 Bạn đã thắng!\n";
        else if (result == 1) resText += "🤝 Hòa!\n";
        else resText += "😢 Bạn đã thua!\n";
        resText += "Đặt cược: " + money_str(bet) + " VNĐ\n";
        if (net_amount == 0) resText += "Hoàn tiền: " + money_str(bet) + " VNĐ\n";
        else if (net_amount > 0) resText += "Tiền thắng: +" + money_str(net_amount) + " VNĐ\n";
        else resText += "Thua lỗ: " + money_str(net_amount) + " VNĐ\n";
        resText += "💳 Số dư: " + money_str(oldBank) + " → " + money_str(newBank) + " VNĐ";

        static std::atomic<int> ctr{0};
        std::string imgPath = paths::cache("kbb_") + std::to_string(ctr++) + ".png";
        game_render::render_kbb_result_rich(p_choice, b_choice, result, bet, net_amount, imgPath);

        std::vector<std::tuple<std::string,int,int>> empty;
        api.send_message_with_image_mentions_async(resText, imgPath, tid, tt, empty);
    };
    commands["kbb"] = commands["keobuabao"];
    // ============================================================
    // /reload, /auto, /py
    // ============================================================
    
    // ============================================================
    // multiprofile - Quan ly nhieu Zalo account
    // ============================================================
    commands["multiprofile"] = [this](const auto& args, const auto& uid,
                                       const auto& tid, int tt, bool) {
        namespace fs = std::filesystem;
        
        auto role_name = [&](const std::string& u) -> std::string {
            Role r = perm.get_user_role(u);
            if (r == Role::HOST) return "HOST";
            if (r == Role::SUPER_ADMIN) return "SUPER ADMIN";
            if (r == Role::BOT_ADMIN) return "ADMIN";
            if (r == Role::GROUP_ADMIN) return "GROUP ADMIN";
            return "MEMBER";
        };
        
        if (tt != 0) {
            reply_text("❌ Lệnh này chỉ dùng trong tin nhắn riêng (DM).", tid, tt);
            return;
        }
        
        std::string sub = args.size() > 1 ? args[1] : "";
        std::transform(sub.begin(), sub.end(), sub.begin(),
                       [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
        
        int current = 1;
        { std::ifstream f("data/current_profile.txt"); if (f.good()) f >> current; }
        
        // ========== LIST: cho phép ADMIN trở lên ==========
        if (sub.empty() || sub == "list") {
            if (!perm.is_admin(uid)) {
                reply_text("❌ Bạn cần role ADMIN trở lên để xem danh sách profile.\n"
                           "Role hiện tại của bạn: " + role_name(uid), tid, tt);
                return;
            }
            std::string body = "📋 DANH SÁCH PROFILE ZALO:\n\n";
            int found = 0;
            for (int i = 1; i <= 5; i++) {
                std::string cf = "data/cookies/" + std::to_string(i) + ".json";
                if (!fs::exists(cf)) continue;
                std::string prof_uid = "?", name = "(chưa rõ)";
                std::string mf_path = "data/cookies/" + std::to_string(i) + ".meta.json";
                std::ifstream mf(mf_path);
                if (mf.good()) {
                    try {
                        nlohmann::json mj; mf >> mj;
                        prof_uid = mj.value("uid", "?");
                        std::string n = perm.get_user_name(prof_uid);
                        if (!n.empty()) name = n;
                    } catch(...) {}
                }
                body += std::to_string(i) + ". " + name + " (UID: " + prof_uid + ")";
                if (i == current) body += "  ⬅️ ĐANG DÙNG";
                body += "\n";
                found++;
            }
            if (found == 0) body += "(Chưa có profile nào)\n";
            body += "\n📖 Cách dùng:\n";
            body += "• multiprofile switch <1-5>  — Đổi profile (bot restart)\n";
            body += "• multiprofile add          — Thêm profile mới (QR ngay)\n";
            body += "• multiprofile remove <1-5> — Xoá profile\n";
            if (!perm.is_super_admin(uid) && !perm.is_host(uid)) {
                body += "\n⚠️ Chỉ SUPER ADMIN / HOST mới được add/switch/remove.";
            }
            reply_text(body, tid, tt);
            return;
        }
        
        // ========== ADD/SWITCH/REMOVE: chỉ SUPER_ADMIN hoặc HOST ==========
        if (sub == "add" || sub == "switch" || sub == "remove" || sub == "rm") {
            if (!perm.is_super_admin(uid) && !perm.is_host(uid)) {
                reply_text("❌ Lệnh `" + sub + "` cần role SUPER ADMIN hoặc HOST.\n"
                           "Role hiện tại của bạn: " + role_name(uid) + "\n\n"
                           "💡 Bạn vẫn có thể dùng `multiprofile` để xem danh sách.", tid, tt);
                return;
            }
        }
        
        // ========== SWITCH ==========
        if (sub == "switch") {
            if (args.size() < 3) { reply_text("❌ Cú pháp: multiprofile switch <1-5>", tid, tt); return; }
            int n = 0;
            try { n = std::stoi(args[2]); } catch (...) {}
            if (n < 1 || n > 5) { reply_text("❌ Số phải từ 1 đến 5", tid, tt); return; }
            if (!fs::exists("data/cookies/" + std::to_string(n) + ".json")) {
                reply_text("❌ Profile " + std::to_string(n) + " không tồn tại", tid, tt); return;
            }
            if (n == current) { reply_text("ℹ️ Đang dùng profile " + std::to_string(n) + " rồi", tid, tt); return; }
            { std::ofstream f("data/current_profile.txt"); f << n; }
            reply_text("🔄 Đang chuyển sang profile " + std::to_string(n) + "... Bot sẽ khởi động lại.", tid, tt);
            std::thread([](){
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::exit(99);
            }).detach();
            return;
        }
        
        // ========== ADD ==========
        if (sub == "add") {
            int next = 0;
            for (int i = 1; i <= 5; i++) {
                if (!fs::exists("data/cookies/" + std::to_string(i) + ".json")) {
                    next = i; break;
                }
            }
            if (next == 0) {
                reply_text("❌ Đã đủ 5 profile. Dùng `multiprofile remove <n>` trước.", tid, tt);
                return;
            }
            fs::create_directories("data/cookies");
            fs::create_directories("cache");
            std::string cookie_path = "data/cookies/" + std::to_string(next) + ".json";
            std::string qr_path = "cache/qr_login_" + std::to_string(next) + ".png";
            std::string log_path = "data/cookies/" + std::to_string(next) + ".log";
            try { fs::remove(qr_path); } catch(...) {}
            try { fs::remove(log_path); } catch(...) {}
            
            reply_text("🚀 Đang khởi động QR cho profile " + std::to_string(next) +
                       "...\n⏱️ Sẽ tự huỷ sau 180 giây nếu không quét.\n📱 QR sẽ được gửi sau vài giây.",
                       tid, tt);
            
            std::string cmd = "start \"LoginHelper_" + std::to_string(next) +
                              "\" /MIN cmd /c \"node login-helper.cjs " + cookie_path + " " + qr_path + "\"";
            std::cout << "[multiprofile] spawn: " << cmd << "\n";
            system(cmd.c_str());
            
            std::string tid_c = tid;
            int next_c = next;
            std::string cookie_c = cookie_path;
            std::string qr_c = qr_path;
            std::string log_c = log_path;
            
            std::thread([this, tid_c, next_c, cookie_c, qr_c, log_c]() {
                bool qr_sent = false;
                bool done = false;
                auto start = std::chrono::steady_clock::now();
                while (std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - start).count() < 180) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    
                    if (!qr_sent && std::filesystem::exists(qr_c)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(300));
                        try {
                            std::vector<std::tuple<std::string,int,int>> empty;
                            api.send_message_with_image_mentions_async(
                                "📱 Quét QR để thêm profile " + std::to_string(next_c) +
                                "\n(QR tự huỷ sau 180s)",
                                qr_c, tid_c, 0, empty);
                            qr_sent = true;
                        } catch (...) {}
                    }
                    
                    if (std::filesystem::exists(cookie_c)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(300));
                        std::string kill_cmd = "taskkill /F /FI \"WINDOWTITLE eq LoginHelper_" +
                                               std::to_string(next_c) + "*\" >nul 2>&1";
                        system(kill_cmd.c_str());
                        api.send_message("✅ Đã thêm profile " + std::to_string(next_c) +
                                         "!\n👉 Dùng `multiprofile switch " + std::to_string(next_c) +
                                         "` để chuyển sang dùng.", tid_c, 0);
                        done = true;
                        break;
                    }
                    
                    // Kiem tra log de phat hien loi quet nham acc chinh
                    std::ifstream logFile(log_c);
                    if (logFile.good()) {
                        std::string line, lastLines = "";
                        while (std::getline(logFile, line)) {
                            if (line.find("CANH BAO: Ban vua quet QR bang ACC CHINH") != std::string::npos) {
                                std::string kill_cmd = "taskkill /F /FI \"WINDOWTITLE eq LoginHelper_" +
                                                       std::to_string(next_c) + "*\" >nul 2>&1";
                                system(kill_cmd.c_str());
                                api.send_message("❌ Lỗi: Bạn vừa quét mã bằng **chính acc đang chạy**!\n"
                                                 "Vui lòng đăng xuất acc đó khỏi Zalo điện thoại, "
                                                 "sau đó quét lại bằng một tài khoản khác.", tid_c, 0);
                                done = true;
                                break;
                            }
                        }
                    }
                }
                if (!done) {
                    std::string kill_cmd = "taskkill /F /FI \"WINDOWTITLE eq LoginHelper_" +
                                           std::to_string(next_c) + "*\" >nul 2>&1";
                    system(kill_cmd.c_str());
                    api.send_message("⏱️ Hết 180s, huỷ thêm profile " + std::to_string(next_c) + ".", tid_c, 0);
                }
            }).detach();
            return;
        }
        
        // ========== REMOVE ==========
        if (sub == "remove" || sub == "rm") {
            if (args.size() < 3) { reply_text("❌ Cú pháp: multiprofile remove <1-5>", tid, tt); return; }
            int n = 0;
            try { n = std::stoi(args[2]); } catch (...) {}
            if (n < 1 || n > 5) { reply_text("❌ Số phải từ 1 đến 5", tid, tt); return; }
            if (n == current) { reply_text("❌ Không xoá được profile đang dùng. Switch sang profile khác trước.", tid, tt); return; }
            std::string cf = "data/cookies/" + std::to_string(n) + ".json";
            if (!fs::exists(cf)) { reply_text("❌ Profile " + std::to_string(n) + " không tồn tại", tid, tt); return; }
            try { fs::remove(cf); } catch (...) {}
            try { fs::remove("data/cookies/" + std::to_string(n) + ".meta.json"); } catch (...) {}
            reply_text("✅ Đã xoá profile " + std::to_string(n), tid, tt);
            return;
        }
        
        reply_text("❌ Sub không hợp lệ. Dùng: list | switch | add | remove", tid, tt);
    };
        commands["gay"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        std::string target = uid;
        if (args.size() > 1 && !args[1].empty()) {
            target = args[1];
        }
        
        bool is_numeric = !target.empty() && std::all_of(target.begin(), target.end(),
                                                          [](unsigned char c){ return std::isdigit(c); });
        if (!is_numeric) {
            reply_text("❌ Cú pháp: /gay (tự xem) hoặc /gay @user hoặc @user /gay\n"
                       "Chú ý: phải tag THẬT vào user, không gõ tay @abc", tid, tt);
            return;
        }
        
        std::string target_name = format_user(target);
        std::string cached = perm.get_user_name(target);
        if (!cached.empty()) target_name = cached;
        
        std::string avt_path = "cache/avt_" + target + ".png";
        std::string avt_url = "";
        std::ifstream _ft(avt_path, std::ios::binary);
        bool has_cache = _ft.good();
        _ft.close();
        
        if (!has_cache) {
            auto resp = api.get_user_info(target);
            try {
                if (resp.contains("result") && resp["result"].is_object()) {
                    auto& r = resp["result"];
                    if (r.contains("changed_profiles") && r["changed_profiles"].is_object()) {
                        auto& cp = r["changed_profiles"];
                        if (cp.contains(target) && cp[target].is_object()) {
                            auto& u = cp[target];
                            if (u.contains("displayName") && u["displayName"].is_string()) {
                                std::string dn = u["displayName"].get<std::string>();
                                if (!dn.empty()) target_name = dn;
                            }
                            if (u.contains("avatar") && u["avatar"].is_string()) {
                                avt_url = u["avatar"].get<std::string>();
                            }
                        }
                    }
                }
            } catch (...) {}
            if (!avt_url.empty()) {
                api.download_image(avt_url, avt_path);
            }
        }
        
        std::ifstream _ft2(avt_path, std::ios::binary);
        if (_ft2.good()) {
            _ft2.close();
            AssetManager::load("avt_" + target, avt_path);
        }
        
        int percent = myRandom::RanInt(0, 100);
        
        static std::atomic<int> ctr{0};
        std::string out = "cache/gay_" + std::to_string(ctr++) + ".png";
        
        render_gay_meter(target_name, "avt_" + target, percent, out);
        
        api.send_image_async(out, tid, tt);
        std::cout << "[Handler] gay: " << target << " (" << target_name << ") -> " << percent << "%\n";
    };
        commands["reloadconfig"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.is_super_admin(uid) && !perm.is_host(uid)) {
            reply_text("❌ Chỉ SUPER ADMIN / HOST mới dùng được", tid, tt);
            return;
        }
        
        load_bot_info();
        load_scold_list();
        load_husks();
        perm.load_from_file("admins.txt");
        
        reply_text("✅ Đã reload config:\n"
                   "• bot_info.json\n"
                   "• scold.txt\n"
                   "• husks.json\n"
                   "• admins.txt\n\n"
                   "Lưu ý: dictionary.txt / accounts.db cần /reload (restart bot).",
                   tid, tt);
        
        std::cout << "[Handler] Reload config by " << uid << "\n";
    };
        commands["reload"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        (void)args;
        if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin bot dùng được", tid, tt); return; }
        reply_text("🔄 Đang khởi động lại bot...", tid, tt);

        std::thread([](){
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "\n[RELOAD] Dang yeu cau run.bat khoi dong lai..." << std::endl;
            std::exit(99); // Ma 99 bao hieu cho run.bat biet can khoi dong lai
        }).detach();
    };
    commands["auto"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin bot", tid, tt); return; }
        if (args.size() < 2) { reply_text("❌ Cú pháp: /auto bot on:game on", tid, tt); return; }
        
        std::string allArgs = "";
        for (size_t i = 1; i < args.size(); i++) allArgs += args[i] + " ";
        
        bool setBot = false, setGame = false;
        bool botVal = false, gameVal = false;
        
        std::stringstream ss(allArgs);
        std::string part;
        while (std::getline(ss, part, ':')) {
            std::istringstream iss(part);
            std::string type, val;
            if (iss >> type >> val) {
                if (type == "bot") { setBot = true; botVal = (val == "on"); }
                else if (type == "game") { setGame = true; gameVal = (val == "on"); }
            }
        }
        
        json autoState;
        std::ifstream in(paths::data("auto_state.json").c_str());
        if (in.good()) { try { in >> autoState; } catch(...) {} }
        in.close();
        
        if (setBot) { set_bot_for_thread(tid, botVal); autoState[tid]["bot"] = botVal; }
        if (setGame) { set_game_for_thread(tid, gameVal); autoState[tid]["game"] = gameVal; }
        
        std::ofstream out(paths::data("auto_state.json").c_str());
        out << autoState.dump(2);
        out.close();
        
        reply_text("✅ Đã cài đặt auto. Sẽ tự động duy trì khi khởi động lại.", tid, tt);
    };

    commands["py"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        std::cerr << "[PY-ENTRY] uid=" << uid << " tid=" << tid << " tt=" << tt << std::endl;
        (void)args;

        // Rate limit (per-user, 60s window)
        {
            static std::mutex rl_mutex;
            static std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> rl_map;
            std::lock_guard<std::mutex> lock(rl_mutex);
            auto& q = rl_map[uid];
            auto now = std::chrono::steady_clock::now();
            while (!q.empty() && std::chrono::duration_cast<std::chrono::seconds>(now - q.front()).count() >= 60) {
                q.pop_front();
            }
            bool isAdmin0 = perm.is_admin(uid);
            int limit = isAdmin0 ? 30 : 10;
            if ((int)q.size() >= limit) {
                reply_text("\u23F1\uFE0F Qu\u00E1 nhi\u1EC1u l\u1EC7nh /py. Th\u1EED l\u1EA1i sau 1 ph\u00FAt.", tid, tt);
                return;
            }
            q.push_back(now);
        }

        std::string raw = ctx().msg_text;
        size_t nl = raw.find('\n');
        std::string code;
        std::vector<std::string> pyArgs;
        bool isInfo = false;

        if (nl == std::string::npos) {
            size_t sp = raw.find_first_of(" \t");
            std::string inlineCode = (sp == std::string::npos) ? "" : raw.substr(sp + 1);
            size_t p = inlineCode.find_first_not_of(" \t");
            if (p != std::string::npos) inlineCode.erase(0, p);
            else inlineCode.clear();
            if (inlineCode == "info" || inlineCode == "help" || inlineCode.empty()) isInfo = true;
            else code = inlineCode;
        } else {
            std::string firstLine = raw.substr(0, nl);
            code = raw.substr(nl + 1);
            std::istringstream iss(firstLine);
            std::string token;
            iss >> token;
            while (iss >> token) pyArgs.push_back(token);
            if (!pyArgs.empty() && (pyArgs[0] == "info" || pyArgs[0] == "help")) {
                isInfo = true;
                pyArgs.clear();
            }
        }
        if (!isInfo && code.empty()) isInfo = true;
        bool isAdmin = perm.is_admin(uid);
        std::cerr << "[PY-PARSE] nl=" << (nl==std::string::npos?-1:(int)nl) << " code_len=" << code.size() << " isInfo=" << isInfo << " args=" << pyArgs.size() << " admin=" << isAdmin << std::endl;

        if (isInfo) {
            std::string pyVer = "?";
            json v = api.run_python("__py_info_magic_7f3a9b__", {}, true, 0);
            if (v.value("ok", false)) {
                pyVer = v.value("result", json::object()).value("output", "?");
                size_t e = pyVer.find_last_not_of(" \r\n\t");
                if (e != std::string::npos) pyVer.erase(e + 1);
                else pyVer = "?";
            }
            std::string help =
                "\U0001F40D Python Runner\n"
                "Version: Python " + pyVer + "\n"
                "\n"
                "\U0001F4D6 C\u00E1ch d\u00F9ng:\n"
                "\u2022 /py <code> - ch\u1EA1y 1 d\u00F2ng\n"
                "\u2022 /py r\u1ED3i xu\u1ED1ng d\u00F2ng + code - nhi\u1EC1u d\u00F2ng\n"
                "\u2022 /py arg1 arg2 r\u1ED3i xu\u1ED1ng d\u00F2ng + code - truy\u1EC1n args, d\u00F9ng input()\n"
                "\n"
                "\U0001F4E6 Modules cho ph\u00E9p:\n"
                "math, random, string, datetime, json, re, itertools, collections, time\n"
                "\n"
                "\u26A0\uFE0F Gi\u1EDBi h\u1EA1n:\n"
                "\u2022 Timeout 8s, output 8KB, b\u1ED9 nh\u1EDB 1GB\n"
                "\u2022 Kh\u00F4ng c\u00F3: open, os, sys, subprocess, eval, exec\n"
                "\u2022 Code kh\u1EA3 nghi (>=70 \u0111i\u1EC3m): ch\u1EC9 admin ch\u1EA1y \u0111\u01B0\u1EE3c";
            std::cerr << "[PY-HELP] sending help reply" << std::endl;
            reply_text(help, tid, tt);
            return;
        }

        if (code.size() > 2000) {
            reply_text("\u274C Code qu\u00E1 d\u00E0i (t\u1ED1i \u0111a 2000 k\u00FD t\u1EF1)", tid, tt);
            return;
        }

        // Suspicion scoring
        int susp = 0;
        std::vector<std::string> reasons;
        auto has = [&](const char* s) { return code.find(s) != std::string::npos; };
        if (code.size() > 800)   { susp += 10; reasons.push_back("dai>800"); }
        if (code.size() > 1500)  { susp += 15; reasons.push_back("dai>1500"); }
        if (code.size() > 500 && code.find('\n') == std::string::npos) { susp += 10; reasons.push_back("1dongdai"); }
        if (code.size() > 800 && code.find('\n') == std::string::npos) { susp += 15; reasons.push_back("1dongratdai"); }
        if (has("__"))           { susp += 20; reasons.push_back("dunder"); }
        if (has("getattr") || has("setattr")) { susp += 20; reasons.push_back("getattr"); }
        if (has("eval(") || has("exec(") || has("compile(")) { susp += 30; reasons.push_back("eval-exec-compile"); }
        if (has("__import__"))   { susp += 30; reasons.push_back("__import__"); }
        if (has("builtins"))     { susp += 20; reasons.push_back("builtins"); }
        if (has("format_map"))   { susp += 15; reasons.push_back("format_map"); }
        if (has(".format("))     { susp += 4;  reasons.push_back("format"); }
        if (has("while True") || has("while 1")) { susp += 8; reasons.push_back("whileTrue"); }
        if (has("time.sleep"))   { susp += 8;  reasons.push_back("sleep"); }
        if (has("chr("))         { susp += 10; reasons.push_back("chr"); }
        if (has("fromhex"))      { susp += 12; reasons.push_back("fromhex"); }
        if (has("bytes(") || has("bytearray(")) { susp += 5; reasons.push_back("bytes"); }
        if (has("\\x"))          { susp += 8;  reasons.push_back("hexescape"); }
        if (has("lambda"))       { susp += 2;  reasons.push_back("lambda"); }
        int semiC = 0; for (char c : code) if (c == ';') semiC++;
        if (semiC >= 3)          { susp += 5;  reasons.push_back("semicolons"); }

        auto logLine = [&](const char* status, const std::string& extra) {
            std::ofstream lf(paths::data("py_audit.log").c_str(), std::ios::app);
            if (!lf) return;
            auto now = std::chrono::system_clock::now();
            auto tt_ = std::chrono::system_clock::to_time_t(now);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&tt_));
            std::string prev = code.substr(0, std::min<size_t>(code.size(), 100));
            for (auto& ch : prev) { if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' '; }
            lf << buf << " " << status << " score=" << susp << " uid=" << uid
               << " tid=" << tid << " len=" << code.size()
               << " preview=\"" << prev << "\" " << extra << "\n";
        };

        if (!isAdmin && susp >= 70) {
            std::string reasonStr;
            for (size_t i = 0; i < reasons.size() && i < 5; ++i) {
                if (i > 0) reasonStr += ", ";
                reasonStr += reasons[i];
            }
            reply_text("\U0001F6AB Code b\u1ECB \u0111\u00E1nh gi\u00E1 kh\u1EA3 nghi ("
                       + std::to_string(susp) + " \u0111i\u1EC3m).\n"
                       "L\u00FD do: " + reasonStr + "\n"
                       "Ch\u1EC9 admin m\u1EDBi ch\u1EA1y \u0111\u01B0\u1EE3c code n\u00E0y.", tid, tt);
            logLine("BLOCK", "reasons=" + reasonStr);
            return;
        }

        std::cerr << "[PY-EXEC] admin=" << isAdmin << " susp=" << susp << " code_len=" << code.size() << std::endl;
        std::string prefix = "\U0001F40D \u0110ang th\u1EF1c thi Python...";
        if (isAdmin && susp >= 40) {
            prefix += " (\u26A0\uFE0F " + std::to_string(susp) + " \u0111i\u1EC3m)";
        }
        reply_text(prefix, tid, tt);
        std::cerr << "[PY-AFTER-REPLY]" << std::endl;
        logLine("RUN", "");
        std::cerr << "[PY-AFTER-LOG]" << std::endl;

        json r;
        try {
            r = api.run_python(code, pyArgs, isAdmin, susp);
        } catch (const std::exception& e) {
            reply_text("❌ Lỗi C++ khi gọi Python: " + std::string(e.what()), tid, tt);
            return;
        }
        std::cerr << "[PY-AFTER-RUN] ok=" << r.value("ok", false) << std::endl;
        if (r.value("ok", false)) {
            std::string out = r.value("result", json::object()).value("output", "");
            if (out.empty()) out = "(không có output)";
            
            // FIX: Cắt chuỗi an toàn UTF-8 (tránh lỗi invalid UTF-8 byte)
            const size_t MAX_LEN = 1500;
            if (out.length() > MAX_LEN) {
                size_t old_len = out.length();
                out = safe_utf8_truncate(out, MAX_LEN) + "\n\n⚠️ (Da cat bot " + std::to_string(old_len - MAX_LEN) + " ky tu vi qua dai)";
            }
            reply_text("\U0001F40D K\u1EBFt qu\u1EA3:\n" + out, tid, tt);
        } else {
            std::string err = r.value("result", "Unknown error");
            if (err.find('\n') == std::string::npos) {
                reply_text("\u274C " + err, tid, tt);
            } else {
                reply_text("\u274C L\u1ED7i Python:\n" + err, tid, tt);
            }
        }
    };
    commands["python"] = commands["py"];
    commands["kick"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "kick")) { reply_text("❌ Không có quyền", tid, tt); return; }
        if (args.size() < 2) { reply_text("❌ Cú pháp: /kick @user", tid, tt); return; }
        if (!group.is_bot_admin_in_group(tid)) { reply_text("❌ Bot không phải admin nhóm", tid, tt); return; }
        std::string t = args[1];
        bool ok = !t.empty() && std::all_of(t.begin(), t.end(), ::isdigit);
        if (!ok) { reply_text("❌ Tag thật vào", tid, tt); return; }
        auto r = api.remove_user_from_group(tid, t);
        if (r.value("ok", false)) reply_text("👢 Đã kick " + format_user(t), tid, tt);
        else reply_text("❌ Kick thất bại", tid, tt);
    };
    commands["warn"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "warn")) { reply_text("❌ Không có quyền", tid, tt); return; }
        if (args.size() < 2) { reply_text("❌ Cú pháp: /warn @user [lý do]", tid, tt); return; }
        if (!group.is_bot_admin_in_group(tid)) { reply_text("❌ Bot không phải admin", tid, tt); return; }
        group.warn_user(tid, args[1]);
        reply_text("⚠️ Đã cảnh cáo", tid, tt);
    };
    commands["welcome"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "welcome")) { reply_text("❌ Không có quyền", tid, tt); return; }
        if (!group.is_bot_admin_in_group(tid)) { reply_text("❌ Bot không phải admin", tid, tt); return; }
        bool en = (args.size() > 1 && args[1] == "on");
        group.set_welcome_enabled(tid, en);
        reply_text(en ? "✅ Welcome ON" : "✅ Welcome OFF", tid, tt);
    };
    commands["goodbye"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "goodbye")) { reply_text("❌ Không có quyền", tid, tt); return; }
        if (!group.is_bot_admin_in_group(tid)) { reply_text("❌ Bot không phải admin", tid, tt); return; }
        bool en = (args.size() > 1 && args[1] == "on");
        group.set_goodbye_enabled(tid, en);
        reply_text(en ? "✅ Goodbye ON" : "✅ Goodbye OFF", tid, tt);
    };
    commands["antispam"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "antispam")) { reply_text("❌ Không có quyền", tid, tt); return; }
        if (!group.is_bot_admin_in_group(tid)) { reply_text("❌ Bot không phải admin", tid, tt); return; }
        bool en = (args.size() > 1 && args[1] == "on");
        group.set_anti_spam_enabled(tid, en);
        reply_text(en ? "✅ Antispam ON" : "✅ Antispam OFF", tid, tt);
    };
    commands["badword"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "badword")) { reply_text("❌ Không có quyền", tid, tt); return; }
        if (!group.is_bot_admin_in_group(tid)) { reply_text("❌ Bot không phải admin", tid, tt); return; }
        bool en = (args.size() > 1 && args[1] == "on");
        group.set_bad_word_filter_enabled(tid, en);
        reply_text(en ? "✅ Badword ON" : "✅ Badword OFF", tid, tt);
    };
    commands["antilink"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "antilink")) { reply_text("❌ Không có quyền", tid, tt); return; }
        if (!group.is_bot_admin_in_group(tid)) { reply_text("❌ Bot không phải admin", tid, tt); return; }
        bool en = (args.size() > 1 && args[1] == "on");
        group.set_anti_link_enabled(tid, en);
        reply_text(en ? "✅ Antilink ON" : "✅ Antilink OFF", tid, tt);
    };
    commands["antil"] = commands["antilink"];
    commands["listadmin"] = [this](const auto&, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.is_admin(uid)) { reply_text("❌ Không có quyền", tid, tt); return; }

        auto admins = perm.list_admins();

        std::vector<std::pair<std::string, std::string>> highList;
        std::vector<std::pair<std::string, std::string>> normList;

        for (const auto& entry : admins) {
            size_t sp = entry.find("  [");
            std::string u, tag;
            if (sp != std::string::npos) {
                u = entry.substr(0, sp);
                tag = entry.substr(sp + 3);
                size_t end = tag.find(']');
                if (end != std::string::npos) tag = tag.substr(0, end);
            } else {
                u = entry;
                tag = "ADMIN";
            }
            if (tag == "HOST" || tag == "SUPER_ADMIN") highList.push_back({u, tag});
            else normList.push_back({u, tag});
        }

        static std::atomic<int> ctr{0};
        std::string out = paths::cache("listadmin_") + std::to_string(ctr++) + ".png";

        const int W = 800;
        const int ROW_H = 100;
        const int HEADER_H = 140;
        const int SECTION_H = 60;
        const int FOOTER_H = 60;

        int totalRows = (int)(highList.size() + normList.size());
        int H = HEADER_H + FOOTER_H + totalRows * ROW_H;
        if (!highList.empty()) H += SECTION_H;
        if (!normList.empty())  H += SECTION_H;
        H += 40;

        std::vector<uint8_t> canvas;
        TextRenderer::init_canvas(canvas, W, H, 30, 40, 80);
        TextRenderer::fill_linear_gradient(canvas, W, H, 0, H,
                                            59, 130, 246, 17, 24, 39);

        std::string title = "DANH SÁCH QUẢN TRỊ BOT";
        int t_size = 44;
        int t_w = TextRenderer::measure_text_width(t_size, title);
        TextRenderer::draw_text_shadow(canvas, W, H, (W - t_w) / 2, 50, t_size,
                                        255, 255, 255, title);

        int y = HEADER_H;

        auto draw_row = [&](const std::string& u, const std::string& tag, int idx) {
            TextRenderer::draw_rounded_rect_filled(canvas, W, H, 30, y, W - 60, ROW_H - 10, 12, 30, 50, 90, 130);

            int avt_r = 35;
            int avt_cx = 30 + 35 + 10;
            int avt_cy = y + (ROW_H - 10) / 2;
            TextRenderer::draw_circle_filled(canvas, W, H, avt_cx, avt_cy, avt_r, 60, 60, 80, 255);

            std::string apath = paths::cache("avt_") + u + ".png";
            std::ifstream _ftest(apath, std::ios::binary);
            if (!_ftest.good()) {
                _ftest.close();
                auto _resp = api.get_user_info(u);
                try {
                    if (_resp.contains("result") && _resp["result"].is_object()) {
                        auto& _r = _resp["result"];
                        if (_r.contains("changed_profiles") && _r["changed_profiles"].is_object()) {
                            auto& _cp = _r["changed_profiles"];
                            if (_cp.contains(u) && _cp[u].is_object()) {
                                auto& _info = _cp[u];
                                if (_info.contains("avatar") && _info["avatar"].is_string()) {
                                    std::string _url = _info["avatar"].get<std::string>();
                                    if (!_url.empty()) api.download_image(_url, apath);
                                }
                            }
                        }
                    }
                } catch (...) {}
            } else {
                _ftest.close();
            }
            ImageRGBA* avt = AssetManager::get("avt_" + u);
            if (!avt) {
                std::ifstream _ft(apath, std::ios::binary);
                if (_ft.good()) avt = AssetManager::load("avt_" + u, apath);
                _ft.close();
            }
            if (avt) TextRenderer::draw_circle_image(canvas, W, H, avt, avt_cx, avt_cy, avt_r);

            TextRenderer::draw_circle_outline_rainbow(canvas, W, H, avt_cx, avt_cy, avt_r + 2, 4);

            std::string name = perm.get_user_name(u);
            if (name.empty()) name = format_user(u);
            std::string rank_line = std::to_string(idx) + ". " + name;
            TextRenderer::draw_text_on_canvas(canvas, W, H,
                                                avt_cx + avt_r + 20, y + 15, 30,
                                                255, 255, 255, rank_line);

            // Robust role detect: strip brackets/spaces/underscores, lowercase, compare
            std::string role_clean;
            for (char ch : tag) {
                if (ch != '[' && ch != ']' && ch != ' ' && ch != '_' && ch != '\t' && ch != '\r' && ch != '\n') {
                    role_clean += (char)std::tolower(static_cast<unsigned char>(ch));
                }
            }
            bool is_bot = (role_clean == "botadmin" || role_clean == "admin");
            std::string role_display = is_bot ? "Quản Trị Viên Bot" : "Quản Trị Cấp Cao";
            unsigned char rr = is_bot ? 200 : 255;
            unsigned char rg = is_bot ? 200 : 220;
            unsigned char rb = is_bot ? 200 : 100;

            TextRenderer::draw_text_on_canvas(canvas, W, H,
                                                avt_cx + avt_r + 20, y + 52, 22,
                                                rr, rg, rb, role_display);
        };

        int idx = 1;
        if (!highList.empty()) {
            TextRenderer::draw_text_shadow(canvas, W, H, 40, y + 15, 30,
                                            255, 200, 80, "Quản Trị Cấp Cao");
            y += SECTION_H;
            for (auto& kv : highList) {
                draw_row(kv.first, kv.second, idx++);
                y += ROW_H;
            }
        }
        if (!normList.empty()) {
            TextRenderer::draw_text_shadow(canvas, W, H, 40, y + 15, 30,
                                            255, 255, 255, "Quản Trị Viên Bot");
            y += SECTION_H;
            for (auto& kv : normList) {
                draw_row(kv.first, kv.second, idx++);
                y += ROW_H;
            }
        }

        int fy = H - FOOTER_H + 15;
        TextRenderer::draw_text_on_canvas(canvas, W, H, 40, fy, 22,
                                           180, 220, 255,
                                           "Tổng: " + std::to_string(idx - 1) + " admin");

        if (TextRenderer::save_canvas(canvas, W, H, out)) {
            api.send_message_with_image_async("👑 DANH SÁCH QUẢN TRỊ", out, tid, tt);
        } else {
            reply_text("❌ Render thất bại", tid, tt);
        }
    };
    commands["setmoney"] = [this](const auto& args, const auto& uid,
                            const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "setmoney")) {
            reply_text("❌ Không có quyền", tid, tt);
            return;
        }
        std::string t; cpp_int a = 0;
        if (args.size() == 2) { t = uid; a = parse_money(args[1]); }
        else if (args.size() >= 3) { t = args[1]; a = parse_money(args[2]); }
        else { reply_text("❌ setmoney <@user> <số tiền>", tid, tt); return; }
        if (a < 0) { reply_text("❌ Số tiền không hợp lệ. VD: 10k, 5m, 1b", tid, tt); return; }
        bool ok = !t.empty() && std::all_of(t.begin(), t.end(), ::isdigit);
        if (!ok) { reply_text("❌ UID không hợp lệ.", tid, tt); return; }
        cpp_int before = econ.get_bank(t);
        econ.set_money(t, a);
        cpp_int after = econ.get_bank(t);
        reply_text(admin_reply::setmoney(before, after, a), tid, tt);
    };

    commands["sethu"] = [this](const auto& args, const auto& uid,
                          const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "sethu")) {
            reply_text("❌ Không có quyền", tid, tt);
            return;
        }
        if (args.size() < 3) {
            reply_text("❌ sethu <game> <số tiền>", tid, tt);
            return;
        }
        cpp_int a = parse_money(args[2]);
        if (a < 0) { reply_text("❌ Số tiền không hợp lệ. VD: 10k, 5m, 1b", tid, tt); return; }
        cpp_int before = econ.get_husk(args[1]);
        econ.set_husk(args[1], a);
        cpp_int after = econ.get_husk(args[1]);
        reply_text(admin_reply::sethu(args[1], before, after, a), tid, tt);
    };

    commands["buff"] = [this](const auto& args, const auto& uid,
                        const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "buff")) {
            reply_text("❌ Không có quyền", tid, tt);
            return;
        }
        std::string t; cpp_int a = 0;
        if (args.size() == 2) { t = uid; a = parse_money(args[1]); }
        else if (args.size() >= 3) { t = args[1]; a = parse_money(args[2]); }
        else { reply_text("❌ buff <@user> <số tiền>", tid, tt); return; }
        if (a <= 0) { reply_text("❌ Số tiền không hợp lệ. VD: 10k, 5m, 1b", tid, tt); return; }
        bool ok = !t.empty() && std::all_of(t.begin(), t.end(), ::isdigit);
        if (!ok) { reply_text("❌ UID không hợp lệ.", tid, tt); return; }
        cpp_int before = econ.get_bank(t);
        econ.buff(t, a);
        cpp_int after = econ.get_bank(t);
        reply_text(admin_reply::buff(before, after, a), tid, tt);
    };

    commands["nerf"] = [this](const auto& args, const auto& uid,
                        const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "nerf")) {
            reply_text("❌ Không có quyền", tid, tt);
            return;
        }
        std::string t; cpp_int a = 0;
        if (args.size() == 2) { t = uid; a = parse_money(args[1]); }
        else if (args.size() >= 3) { t = args[1]; a = parse_money(args[2]); }
        else { reply_text("❌ nerf <@user> <số tiền>", tid, tt); return; }
        if (a <= 0) { reply_text("❌ Số tiền không hợp lệ. VD: 10k, 5m, 1b", tid, tt); return; }
        bool ok = !t.empty() && std::all_of(t.begin(), t.end(), ::isdigit);
        if (!ok) { reply_text("❌ UID không hợp lệ.", tid, tt); return; }
        cpp_int before = econ.get_bank(t);
        econ.nerf(t, a);
        cpp_int after = econ.get_bank(t);
        reply_text(admin_reply::nerf(before, after, a), tid, tt);
    };

    commands["setad"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        (void)args;
        if (!perm.is_super_admin(uid)) { reply_text("\u274C Ch\u1EC9 SUPER ADMIN!", tid, tt); return; }

        // Parse truc tiep tu ctx().msg_text de tranh args[] khong dong nhat
        std::string raw = ctx().msg_text;
        if (!raw.empty() && (raw[0] == '/' || raw[0] == '!')) raw = raw.substr(1);
        size_t sp = raw.find_first_of(" \t\r\n");
        std::string rest = (sp == std::string::npos) ? "" : raw.substr(sp + 1);

        std::vector<std::string> toks;
        {
            std::istringstream iss(rest);
            std::string tk;
            while (iss >> tk) toks.push_back(tk);
        }

        auto extract_uid = [](const std::string& t) -> std::string {
            if (t.empty()) return "";
            if (t.size() >= 3 && t[0] == '<' && t[1] == '@') {
                size_t end = t.find_first_of(">|", 2);
                std::string u = (end != std::string::npos) ? t.substr(2, end - 2) : t.substr(2);
                if (!u.empty() && std::all_of(u.begin(), u.end(), ::isdigit)) return u;
                return "";
            }
            if (t[0] == '@') {
                std::string u = t.substr(1);
                if (!u.empty() && std::all_of(u.begin(), u.end(), ::isdigit)) return u;
                return "";
            }
            if (std::all_of(t.begin(), t.end(), ::isdigit)) return t;
            return "";
        };

        if (toks.empty()) { reply_text("\U0001F4D6 setad add/remove/list", tid, tt); return; }
        std::string sub = toks[0];
        std::transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (sub == "list") {
            // Forward sang listadmin (render anh 1 voi avatar + fix badge)
            if (commands.count("listadmin")) {
                commands["listadmin"]({}, uid, tid, tt, false);
            } else {
                reply_text("❌ Lệnh /listadmin chưa đăng ký", tid, tt);
            }
            return;
        }

        if (sub == "add") {
            bool is_super = false;
            size_t idx = 1;
            if (idx < toks.size()) {
                std::string tk = toks[idx];
                std::transform(tk.begin(), tk.end(), tk.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (tk == "sp" || tk == "super") { is_super = true; idx++; }
            }
            if (idx >= toks.size()) { reply_text("\u274C setad add [sp] <@user>", tid, tt); return; }
            std::string t = extract_uid(toks[idx]);
            if (t.empty()) {
                reply_text("\u274C UID sai. D\u00F9ng @mention ho\u1EB7c UID s\u1ED1.", tid, tt);
                return;
            }
            Role cur = perm.get_user_role(t);
            Role nr  = is_super ? Role::SUPER_ADMIN : Role::BOT_ADMIN;
            if (cur == Role::HOST) { reply_text("\u274C Kh\u00F4ng th\u1EC3 \u0111\u1ED5i HOST", tid, tt); return; }
            if (cur == nr) { reply_text("\u2139\uFE0F " + format_user(t) + " \u0111\u00E3 c\u00F3 quy\u1EC1n n\u00E0y", tid, tt); return; }
            perm.set_user_role(t, nr);
            std::string disp = format_user(t);
            if (!disp.empty() && disp != t) perm.set_user_name(t, disp);
            reply_text("\U0001F451 \u0110\u00E3 th\u00EAm " + disp + " l\u00E0m " + (is_super ? "SUPER ADMIN" : "ADMIN"), tid, tt);
            return;
        }

        if (sub == "remove") {
            if (toks.size() < 2) { reply_text("\u274C setad remove <@user>", tid, tt); return; }
            std::string t = extract_uid(toks[1]);
            if (t.empty()) { reply_text("\u274C UID sai", tid, tt); return; }
            if (perm.remove_admin(t)) reply_text("\u2705 \u0110\u00E3 x\u00F3a admin " + format_user(t), tid, tt);
            else                     reply_text("\u274C Kh\u00F4ng x\u00F3a \u0111\u01B0\u1EE3c", tid, tt);
            return;
        }

        reply_text("\u274C Sub: add/remove/list", tid, tt);
    };
    commands["setadd"] = commands["setad"];
    commands["setadmin"] = commands["setad"];
    commands["setprefix"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.is_super_admin(uid)) { reply_text("❌ Chỉ SUPER ADMIN!", tid, tt); return; }
        if (args.size() < 2) { reply_text("❌ Cú pháp: /setprefix <tiền tố mới>", tid, tt); return; }
        set_prefix(args[1]);
        reply_text("✅ Đổi prefix thành " + args[1], tid, tt);
    };
    // ============================================================
    // Điểm danh hàng ngày
    // ============================================================
    commands["daily"] = [this](const auto&, const auto& uid,
                               const auto& tid, int tt, bool) {
        if ((!account.is_logged_in(uid) && !g_debug_mode)) {
            reply_text("❌ Cần đăng nhập trước", tid, tt);
            return;
        }
        const cpp_int DAILY_REWARD = 50000;
        std::string err = econ.claim_daily(uid, DAILY_REWARD);
        if (err.empty()) {
            g_econ_dirty = true;
            reply_text("🎁 Điểm danh thành công! +" + money_str(DAILY_REWARD) + " VNĐ vào Bank!\n💳 Số dư Bank: " + money_str(econ.get_bank(uid)) + " VNĐ", tid, tt);
        } else {
            reply_text(err, tid, tt);
        }
    };

    // ============================================================
    // Xếp hạng tin nhắn theo nhóm
    // ============================================================
    commands["rank"] = [this](const auto&, const auto& uid,
                              const auto& tid, int tt, bool) {
        auto top = group.get_top(tid, 10);
        if (top.empty()) {
            reply_text("📊 Chưa có dữ liệu tin nhắn cho nhóm này", tid, tt);
            return;
        }
        std::string body;
        int idx = 1;
        for (const auto& kv : top) {
            std::string name = format_user(kv.first);
            body += std::to_string(idx) + ". " + name
                  + " — " + std::to_string(kv.second) + " tin\n";
            idx++;
        }
        reply_image("🏆 BẢNG XẾP HẠNG", body, tid, tt);
    };

    // ============================================================
    // Gửi sticker Zalo
    // ============================================================
    commands["sticker"] = [this](const auto& args, const auto& uid,
                                 const auto& tid, int tt, bool) {
        if (args.size() < 2) {
            reply_text("❌ Cú pháp: /sticker <id>", tid, tt);
            return;
        }
        api.send_sticker_async(args[1], tid, tt);
    };

    // ============================================================
    // Top user giàu nhất
    // ============================================================
    commands["top"] = [this](const auto& args, const auto& uid,
                             const auto& tid, int tt, bool) {
        int limit = 10;
        if (args.size() > 1) {
            try { limit = std::stoi(args[1]); } catch (...) { limit = 10; }
        }
        if (limit < 1) limit = 1;
        if (limit > 30) limit = 30;

        std::vector<std::pair<std::string, cpp_int>> top;
        if (!account.get_top_wallet(limit, top) || top.empty()) {
            reply_text("📊 Chưa có dữ liệu tài khoản", tid, tt);
            return;
        }

        std::string body;
        int idx = 1;
        for (const auto& kv : top) {
            std::string medal;
            if (idx == 1)      medal = "🥇";
            else if (idx == 2) medal = "🥈";
            else if (idx == 3) medal = "🥉";
            else               medal = " " + std::to_string(idx) + ".";

            std::string wallet_str = money_str(kv.second);
            body += medal + " " + kv.first
                  + " — " + wallet_str + "\n";
            idx++;
        }
        reply_image("💰 TOP " + std::to_string(limit) + " ĐẠI GIA", body, tid, tt);
    };

    // ============================================================
    // Tạo QR code
    // ============================================================
    commands["qr"] = [this](const auto& args, const auto& uid,
                            const auto& tid, int tt, bool) {
        if (args.size() < 2) {
            reply_text("❌ Cú pháp: /qr <nội dung>\n"
                       "Ví dụ: /qr https://zalo.me", tid, tt);
            return;
        }
        std::string text;
        for (size_t i = 1; i < args.size(); i++) {
            if (i > 1) text += " ";
            text += args[i];
        }
        if (text.size() > 1000) {
            reply_text("❌ Nội dung quá dài (tối đa 1000 ký tự)", tid, tt);
            return;
        }
        static std::atomic<int> qr_ctr{0};
        std::string qr_path = "cache/qr_" + std::to_string(qr_ctr++) + ".png";
        api.send_qr_async(text, qr_path, tid, tt);
    };

    // ============================================================
    // Anti-undo — chống thu hồi tin nhắn
    // ============================================================
    commands["antiundo"] = [this](const auto& args, const auto& uid,
                                  const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "antiundo")) {
            reply_text("❌ Không có quyền", tid, tt);
            return;
        }
        if (!group.is_bot_admin_in_group(tid)) {
            reply_text("❌ Bot không phải admin nhóm", tid, tt);
            return;
        }
        bool en = (args.size() > 1 && args[1] == "on");
        group.set_anti_undo_enabled(tid, en);
        reply_text(en ? "✅ Anti-undo ON\nBot sẽ gửi lại nội dung khi ai đó thu hồi tin."
                      : "✅ Anti-undo OFF", tid, tt);
    };

    // ============================================================
    // /groupprotect — bảng điều khiển bảo vệ nhóm
    // ============================================================
    commands["groupprotect"] = [this](const auto& args, const auto& uid,
                                      const auto& tid, int tt, bool) {
        if (!perm.has_permission(uid, "groupprotect")) {
            reply_text("❌ Chỉ admin bot mới dùng được lệnh này", tid, tt);
            return;
        }
        bool bot_is_admin = group.is_bot_admin_in_group(tid);
        std::string admin_note = bot_is_admin
            ? "✅ Bot là admin"
            : "⚠️ Bot KHÔNG phải admin (không xóa/kick được)";

        auto status_map = [&]() -> std::string {
            auto yn = [&](const std::string& name, bool en) {
                return std::string(en ? "✅" : "❌") + " " + name + "\n";
            };
            auto y3 = [&](const std::string& name, int v) {
                std::string icon = (v == 1) ? "✅" : (v == 0 ? "❌" : "❓");
                return icon + " " + name + (v == -1 ? " (chưa biết)\n" : "\n");
            };
            return
                yn("Chống spam",     group.is_anti_spam_enabled(tid)) +
                yn("Lọc từ bậy",     group.is_bad_word_filter_enabled(tid)) +
                yn("Chống link",     group.is_anti_link_enabled(tid)) +
                yn("Chống thu hồi",  group.is_anti_undo_enabled(tid)) +
                yn("Chào mừng",      group.is_welcome_enabled(tid)) +
                yn("Tạm biệt",       group.is_goodbye_enabled(tid)) +
                "── Lock / Approve ──\n" +
                y3("Khóa chat",       group.get_lock_cache(tid)) +
                y3("Duyệt thành viên", group.get_approve_cache(tid));
        };

        bool show_status = (args.size() < 2);
        if (args.size() == 2) {
            std::string s2 = args[1];
            std::transform(s2.begin(), s2.end(), s2.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (s2 == "status" || s2 == "info" || s2 == "help" || s2 == "xem") {
                show_status = true;
            }
        }

        if (show_status) {
            std::string body =
                "📍 Nhóm: " + tid.substr(0, 8) + "...\n"
                "🤖 " + admin_note + "\n"
                "──────────────\n" +
                status_map() +
                "\n📖 Dùng:\n"
                "  /groupprotect on|off\n"
                "  /groupprotect <tên> on|off\n"
                "Tên: spam | badword | link | antiundo\n"
                "     welcome | goodbye | lock | approve";
            reply_image("🛡️ BẢO VỆ NHÓM", body, tid, tt);
            return;
        }

        std::string sub = args[1];
        std::transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (sub == "on" || sub == "off" || sub == "reset") {
            bool en = (sub == "on");
            group.set_all_protect(tid, en);
            if (sub == "reset") {
                group.set_welcome_enabled(tid, false);
                group.set_goodbye_enabled(tid, false);
                reply_text("♻️ Đã reset tất cả bảo vệ về OFF", tid, tt);
            } else {
                reply_text(en ? "🛡️ Đã BẬT tất cả bảo vệ nhóm"
                              : "🛡️ Đã TẮT tất cả bảo vệ nhóm", tid, tt);
            }
            return;
        }

        if (sub == "ban") {
            if (args.size() < 3) {
                reply_text("❌ Cú pháp: /groupprotect ban <@user>", tid, tt);
                return;
            }
            if (!bot_is_admin) {
                reply_text("❌ Bot phải là admin nhóm mới kick/ban được thành viên!", tid, tt);
                return;
            }
            std::string target = args[2];
            auto r = api.remove_user_from_group(tid, target);
            if (r.value("ok", false)) {
                reply_text("🚫 Đã ban & mời " + format_user(target) + " ra khỏi nhóm!", tid, tt);
            } else {
                reply_text("❌ Không kick được " + format_user(target) + ".", tid, tt);
            }
            return;
        }

        if (args.size() < 3) {
            reply_text("❌ Cú pháp: /groupprotect <tên> <on|off>\n"
                       "Tên: spam | badword | link | antiundo | welcome | goodbye | lock | approve | ban <@u>",
                       tid, tt);
            return;
        }

        std::string val = args[2];
        std::transform(val.begin(), val.end(), val.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        bool en = (val == "on");

        if (sub == "spam") {
            group.set_anti_spam_enabled(tid, en);
            reply_text(en ? "✅ Chống spam ON" : "✅ Chống spam OFF", tid, tt);
        }
        else if (sub == "badword") {
            group.set_bad_word_filter_enabled(tid, en);
            reply_text(en ? "✅ Lọc từ bậy ON" : "✅ Lọc từ bậy OFF", tid, tt);
        }
        else if (sub == "link" || sub == "antilink") {
            group.set_anti_link_enabled(tid, en);
            reply_text(en ? "✅ Chống link ON" : "✅ Chống link OFF", tid, tt);
        }
        else if (sub == "antiundo") {
            group.set_anti_undo_enabled(tid, en);
            reply_text(en ? "✅ Chống thu hồi ON" : "✅ Chống thu hồi OFF", tid, tt);
        }
        else if (sub == "welcome") {
            group.set_welcome_enabled(tid, en);
            reply_text(en ? "✅ Chào mừng ON" : "✅ Chào mừng OFF", tid, tt);
        }
        else if (sub == "goodbye") {
            group.set_goodbye_enabled(tid, en);
            reply_text(en ? "✅ Tạm biệt ON" : "✅ Tạm biệt OFF", tid, tt);
        }
        else if (sub == "lock") {
            if (!bot_is_admin) {
                reply_text("❌ Bot phải là admin nhóm mới khóa chat được", tid, tt);
                return;
            }
            if (api.set_group_lock(tid, en)) {
                group.set_lock_cache(tid, en);
                reply_text(en ? "🔒 Đã KHÓA chat — chỉ admin gửi được tin"
                              : "🔓 Đã MỞ khóa chat", tid, tt);
            } else {
                reply_text("❌ Không đổi được. Bot có phải admin?", tid, tt);
            }
        }
        else if (sub == "approve") {
            if (!bot_is_admin) {
                reply_text("❌ Bot phải là admin nhóm mới bật duyệt được", tid, tt);
                return;
            }
            if (api.set_group_approval(tid, en)) {
                group.set_approve_cache(tid, en);
                reply_text(en ? "🆕 Đã BẬT duyệt thành viên mới"
                              : "🆕 Đã TẮT duyệt — ai cũng vào được", tid, tt);
            } else {
                reply_text("❌ Không đổi được. Bot có phải admin?", tid, tt);
            }
        }
        else {
            reply_text("❌ Tên không hợp lệ: " + sub, tid, tt);
        }
    };

    auto exit_handler = [this](const auto&, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.is_host(uid)) { reply_text("❌ Chỉ HOST mới thoát được bot!", tid, tt); return; }
        reply_text("👋 Bot đang thoát...", tid, tt);
        std::cout << "[Handler] Exit từ " << uid << "\n";
        g_running = false;
    };
    // ============================================================
    // /bot on|off - bat/tat bot (default off, chi HOST+ dieu khien)
    // ============================================================
    commands["bot"] = [this](const auto& args, const auto& uid,
                            const auto& tid, int tt, bool) {
        if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin bot", tid, tt); return; }
        if (args.size() < 2) {
            reply_text(std::string("🤖 Trạng thái Bot nhóm này: ") + (get_bot_for_thread(tid) ? "ON ✅" : "OFF 💤"), tid, tt);
            return;
        }
        std::string v = args[1];
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        bool on = (v == "on" || v == "1" || v == "bat");
        set_bot_for_thread(tid, on);
        g_econ_dirty = true;
        reply_text(on ? "✅ Bot ON" : "💤 Bot OFF\nGõ /on để bật lại", tid, tt);
    };
    commands["on"] = [this](const auto&, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin bot", tid, tt); return; }
        set_bot_for_thread(tid, true);
        g_econ_dirty = true;
        reply_text("✅ Bot ON", tid, tt);
    };
    commands["off"] = [this](const auto&, const auto& uid, const auto& tid, int tt, bool) {
        if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin bot", tid, tt); return; }
        reply_text("💤 Bot OFF\nGõ /on để bật lại", tid, tt);
        set_bot_for_thread(tid, false);
        g_econ_dirty = true;
    };

    // ============================================================
    // /game on|off - bat/tat game (default off)
    // ============================================================
    commands["game"] = [this](const auto& args, const auto& uid,
                              const auto& tid, int tt, bool) {
        if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin bot", tid, tt); return; }
        if (args.size() < 2) {
            reply_text(std::string("🎮 Trạng thái Game nhóm này: ") + (get_game_for_thread(tid) ? "ON ✅" : "OFF 💤"), tid, tt);
            return;
        }
        std::string v = args[1];
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        bool on = (v == "on" || v == "1" || v == "bat");
        set_game_for_thread(tid, on);
        g_econ_dirty = true;
        reply_text(on ? "✅ Game ON" : "💤 Game OFF", tid, tt);
    };

    // ============================================================
    // /debug on|off - chi HOST - auto login + buff bank 1b + force on
    // ============================================================
    // ============================================================
    // /debug [off] - HOST only. Khong args = bat.
    // Bat: buff bank 1b + force bot/game on. Tat: restore.
    // ============================================================
    commands["debug"] = [this](const auto& args, const auto& uid,
                               const auto& tid, int tt, bool) {
        if (!perm.is_host(uid)) { reply_text("\u274C Ch\u1EC9 HOST", tid, tt); return; }
        bool turn_off = (args.size() > 1 && args[1] == "off");

        if (turn_off) {
            if (!g_debug_mode) { reply_text("⚠️ Chế độ Debug đang TẮT.", tid, tt); return; }
            set_bot_for_thread(tid, g_debug_saved_bot);
            set_game_for_thread(tid, g_debug_saved_game);
            g_debug_mode = false;
            g_debug_uid = "";
            std::string st = "\U0001F41B Debug OFF\n- Bot: ";
            st += g_bot_enabled ? "ON" : "OFF";
            st += "\n- Game: ";
            st += g_game_enabled ? "ON" : "OFF";
            reply_text(st, tid, tt);
        } else {
            if (g_debug_mode) { reply_text("\u26A0\uFE0F Debug \u0111ang B\u1EACT", tid, tt); return; }
            g_debug_saved_bot  = get_bot_for_thread(tid);
            g_debug_saved_game = get_game_for_thread(tid);
            UserEconomy e = econ.get_user_economy(uid);
            e.bank = cpp_int(DEBUG_BANK_LL);
            econ.set_full_data(uid, e.wallet, e.bank, e.debt, e.games_played, e.games_won);
            set_bot_for_thread(tid, true);
            set_game_for_thread(tid, true);
            g_debug_mode = true;
            g_debug_uid = uid;
            g_econ_dirty = true;
            reply_text("\U0001F41B Debug ON\n- Bank: 1.000.000.000\n- Bot: ON\n- Game: ON", tid, tt);
        }
    };

    commands["exit"] = exit_handler;
    commands["quit"] = exit_handler;
    
    // ============================================================
    // /spam <text> <n> <delay_ms> | /spam stop
    // ============================================================
    commands["spam"] = [this](const auto& args, const auto& uid,
                              const auto& tid, int tt, bool) {
        if (!perm.is_super_admin(uid)) {
            reply_text("\u274C Ch\u1EC9 SUPER ADMIN", tid, tt);
            return;
        }
        if (args.size() >= 2 && args[1] == "stop") {
            g_spam_active = false;
            reply_text("\u2705 \u0110\u00E3 d\u1EEBng spam", tid, tt);
            return;
        }
        if (args.size() < 4) {
            reply_text("\u274C /spam <n\u1ED9i dung> <s\u1ED1 l\u1EA7n> <delay_ms>", tid, tt);
            return;
        }
        if (g_spam_active) {
            reply_text("\u26A0\uFE0F \u0110ang spam, d\u00F9ng /spam stop tr\u01B0\u1EDBc", tid, tt);
            return;
        }
        std::string text = args[1];
        for (size_t k = 2; k + 2 < args.size(); k++) text += " " + args[k];
        int n = 10, delay = 800;
        try { n = std::stoi(args[args.size() - 2]); } catch (...) { n = 10; }
        try { delay = std::stoi(args[args.size() - 1]); } catch (...) { delay = 800; }
        if (n < 1) n = 1;
        if (n > 50) n = 50;
        if (delay < 300) delay = 300;
        if (delay > 3000) delay = 3000;
        g_spam_active = true;
        std::string tid_c = tid;
        int tt_c = tt;
        std::thread([this, tid_c, tt_c, text, n, delay]() {
            for (int i = 0; i < n && g_spam_active; i++) {
                api.send_message_async(text, tid_c, tt_c);
                for (int k = 0; k < delay && g_spam_active; k += 100)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            g_spam_active = false;
        }).detach();
        reply_text("\U0001F4E4 \u0110ang spam " + std::to_string(n) + " l\u1EA7n, delay " + std::to_string(delay) + "ms", tid, tt);
    };

    // ============================================================
    // /scold <uid> [delay_ms] | /scold tha
    // ============================================================
    commands["scold"] = [this](const auto& args, const auto& uid,
                               const auto& tid, int tt, bool) {
        if (!perm.is_super_admin(uid)) {
            reply_text("\u274C Ch\u1EC9 SUPER ADMIN", tid, tt);
            return;
        }
        if (args.size() >= 2 && args[1] == "tha") {
            g_scold_active = false;
            reply_text("\u2705 \u0110\u00E3 tha", tid, tt);
            return;
        }
        if (args.size() < 2) {
            reply_text("\u274C /scold @user [delay_ms]", tid, tt);
            return;
        }
        if (g_scold_active) {
            reply_text("\u26A0\uFE0F \u0110ang ch\u1EEDi, d\u00F9ng /scold tha tr\u01B0\u1EDBc", tid, tt);
            return;
        }
        if (g_scold_list.empty()) {
            reply_text("\u274C Ch\u01B0a c\u00F3 data/scold.txt", tid, tt);
            return;
        }
        std::string target = args[1];
        int delay = 2000;
        if (args.size() >= 3) {
            try { delay = std::stoi(args[2]); } catch (...) { delay = 2000; }
        }
        if (delay < 100) delay = 100;
        if (delay > 10000) delay = 10000;
        g_scold_active = true;
        std::string tid_c = tid;
        int tt_c = tt;
        auto list = g_scold_list;
        std::string target_name = format_user(target);
        std::thread([this, tid_c, tt_c, target, target_name, delay, list]() {
            std::string prefix = "@" + target_name;
            size_t i = 0;
            while (g_scold_active && i < list.size()) {
                std::string msg = prefix + " " + list[i];
                api.send_message_with_mention_async(msg, tid_c, tt_c, target, 0, (int)prefix.size());
                i++;
                if (i >= list.size()) break;
                for (int k = 0; k < delay && g_scold_active; k += 100)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            g_scold_active = false;
        }).detach();
        reply_text("\U0001F525 B\u1EAFt \u0111\u1EA7u ch\u1EEDi " + format_user(target), tid, tt);
    };

    // ============================================================
    // Alias & new commands: topchat, soicau, vietlott, caro, group, voice,
    // girl/boy/anime/cosplay, cadao/tarot/thinh/truyencuoi, dich, thoitiet, mute/unmute
    // ============================================================
    commands["topchat"] = commands["rank"];
    commands["bc"] = commands["baucua"];

    commands["soicau"] = [this](const auto& args, const auto&, const auto& tid, int tt, bool) {
        std::string mode = "all";
        if (args.size() > 1) {
            mode = args[1];
            std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c){ return (char)std::tolower(c); });
            if (mode == "tx" || mode == "taixiu") mode = "taixiu";
            else if (mode == "cl" || mode == "chanle") mode = "chanle";
            else mode = "all";
        }
        std::vector<std::pair<int, bool>> tx_pts;
        {
            std::lock_guard<std::mutex> lk(g_taixiu_mtx);
            tx_pts = g_taixiu_history_pts;
        }
        auto cl_hist = game.get_chanle_history();
        static std::atomic<int> sc_ctr{0};
        std::string out = paths::cache("soicau_") + std::to_string(sc_ctr++) + ".png";
        extra_render::render_soicau(tx_pts, cl_hist, mode, out);
        std::string cap = "📊 SOI CẦU (" + std::to_string(tx_pts.size()) + " phiên Tài Xỉu | " + std::to_string(cl_hist.size()) + " ván Chẵn Lẻ)";
        api.send_message_with_image_async(cap, out, tid, tt);
    };
    commands["sc"] = commands["soicau"];

    commands["vietlott"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (!account.is_logged_in(uid) && !g_debug_mode) {
            reply_text("❌ Cần đăng nhập. Dùng /login <user> <pass>", tid, tt);
            return;
        }
        if (args.size() < 3) {
            reply_text("🎟️ Cú pháp:\n• /vietlott <s1> <s2> <s3> <s4> <s5> <s6> <cược>\n• /vietlott auto <cược>\nSố từ 1 đến 55 (6 số khác nhau).", tid, tt);
            return;
        }
        std::vector<int> player_nums;
        cpp_int bal = econ.get_bank(uid);
        cpp_int bet = 0;

        std::string first_arg = args[1];
        std::transform(first_arg.begin(), first_arg.end(), first_arg.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        if (first_arg == "auto" || first_arg == "ngau" || first_arg == "random") {
            bet = parse_bet_amount(args[2], bal);
            std::vector<int> pool;
            for (int n = 1; n <= 55; n++) pool.push_back(n);
            std::shuffle(pool.begin(), pool.end(), game_rng());
            for (int i = 0; i < 6; i++) player_nums.push_back(pool[i]);
            std::sort(player_nums.begin(), player_nums.end());
        } else {
            if (args.size() < 8) {
                reply_text("❌ Nhập đủ 6 số (1-55) + tiền cược, hoặc dùng /vietlott auto <cược>", tid, tt);
                return;
            }
            std::set<int> uniq;
            for (int i = 1; i <= 6; i++) {
                try {
                    int num = std::stoi(args[i]);
                    if (num < 1 || num > 55 || uniq.count(num)) {
                        reply_text("❌ 6 số phải khác nhau và trong khoảng 1 → 55!", tid, tt);
                        return;
                    }
                    uniq.insert(num);
                    player_nums.push_back(num);
                } catch (...) {
                    reply_text("❌ Số không hợp lệ: " + args[i], tid, tt);
                    return;
                }
            }
            std::sort(player_nums.begin(), player_nums.end());
            bet = parse_bet_amount(args[7], bal);
        }

        if (bet < 1000) { reply_text("❌ Cược tối thiểu 1.000 VNĐ!", tid, tt); return; }
        if (bal < bet) { reply_text("❌ Không đủ tiền! Bank: " + money_str(bal) + " VNĐ", tid, tt); return; }

        cpp_int oldBank = bal;
        econ.place_bet(uid, bet);

        std::vector<int> pool;
        for (int n = 1; n <= 55; n++) pool.push_back(n);
        std::shuffle(pool.begin(), pool.end(), game_rng());
        std::vector<int> draw_nums(pool.begin(), pool.begin() + 6);
        std::sort(draw_nums.begin(), draw_nums.end());
        int extra_num = pool[6];

        int matches = 0;
        bool has_extra = false;
        for (int pn : player_nums) {
            if (std::find(draw_nums.begin(), draw_nums.end(), pn) != draw_nums.end()) matches++;
            if (pn == extra_num) has_extra = true;
        }

        cpp_int husk = econ.get_husk("vietlott");
        if (husk <= 0) { husk = 50000000LL; econ.set_husk("vietlott", husk); }
        cpp_int prize = 0;
        bool is_jp = false;
        std::string rank_label = "Không trúng giải";

        if (matches == 6) {
            is_jp = true;
            prize = bet * 500 + husk;
            econ.set_husk("vietlott", cpp_int(50000000LL));
            rank_label = "💥 JACKPOT 1 (6/6)!";
        } else if (matches == 5 && has_extra) {
            is_jp = true;
            cpp_int jp2 = husk / 5;
            prize = bet * 80 + jp2;
            econ.add_to_husk("vietlott", -jp2);
            rank_label = "🌟 JACKPOT 2 (5/6 + Phụ)!";
        } else if (matches == 5) {
            prize = bet * 25;
            rank_label = "🥇 Giải Nhất (5/6)";
        } else if (matches == 4) {
            prize = bet * 6;
            rank_label = "🥈 Giải Nhì (4/6)";
        } else if (matches == 3) {
            prize = bet * 2;
            rank_label = "🥉 Giải Ba (3/6)";
        } else {
            econ.add_to_husk("vietlott", bet * 50 / 100);
        }

        if (prize > 0) econ.add_winnings(uid, prize);
        else econ.record_loss(uid, bet);
        save_husks();
        g_econ_dirty = true;

        cpp_int newBank = econ.get_bank(uid);
        std::string pnums_str;
        for (size_t i = 0; i < player_nums.size(); i++) {
            if (i) pnums_str += " - ";
            pnums_str += std::to_string(player_nums[i]);
        }
        std::string sub = rank_label + " | Trung " + std::to_string(matches) + "/6"
                        + (has_extra ? " + Phu" : "") + " | Hu: " + money_str(econ.get_husk("vietlott")) + " VND";

        static std::atomic<int> vl_ctr{0};
        std::string out = paths::cache("vietlott_") + std::to_string(vl_ctr++) + ".png";
        extra_render::render_vietlott_result(draw_nums, extra_num, is_jp, sub, out);

        std::string cap = "🎟️ VIETLOTT 6/55 — [ " + format_user(uid) + " ]\n";
        cap += "• Bộ số bạn chọn: " + pnums_str + "\n";
        cap += "• Kết quả: " + rank_label + " (Trùng " + std::to_string(matches) + "/6" + (has_extra ? " + Phụ" : "") + ")\n";
        cap += "• Cược: " + money_str(bet) + " VNĐ\n";
        if (prize > 0) cap += "🎉 Tiền thưởng: +" + money_str(prize) + " VNĐ\n";
        else cap += "💸 Chúc bạn may mắn lần sau!\n";
        cap += "💳 Số dư: " + money_str(oldBank) + " → " + money_str(newBank) + " VNĐ\n";
        cap += "💰 Hũ Vietlott: " + money_str(econ.get_husk("vietlott")) + " VNĐ";
        api.send_message_with_image_async(cap, out, tid, tt);
    };

    commands["caro"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (args.size() > 1) {
            std::string a1 = args[1];
            std::transform(a1.begin(), a1.end(), a1.begin(), [](unsigned char c){ return (char)std::tolower(c); });
            if (a1 == "cancel" || a1 == "huy" || a1 == "stop") {
                if (game.cancel_caro_game(tid)) {
                    g_econ_dirty = true;
                    reply_text("♟️ Đã huỷ ván cờ Caro hiện tại (đã hoàn cược nếu có).", tid, tt);
                } else {
                    reply_text("♟️ Không có ván cờ Caro nào đang diễn ra.", tid, tt);
                }
                return;
            }
        }
        if (game.get_caro_game(tid) != nullptr) {
            reply_text("⚠️ Nhóm đang có ván Caro! Gõ \"<dòng> <cột>\" (0-14) để đánh hoặc /caro cancel để huỷ.", tid, tt);
            return;
        }
        if (args.size() < 2) {
            reply_text("♟️ Cú pháp:\n• /caro bot [cược] — Đấu với Bot\n• /caro @user [cược] — Đấu PvP\n• /caro cancel — Huỷ ván", tid, tt);
            return;
        }
        std::string target = args[1];
        std::string target_lower = target;
        std::transform(target_lower.begin(), target_lower.end(), target_lower.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        bool vs_bot = (target_lower == "bot" || target_lower == "ai");
        if (!vs_bot) {
            bool is_num = !target.empty() && std::all_of(target.begin(), target.end(), [](unsigned char c){ return std::isdigit(c); });
            if (!is_num || target == uid) {
                reply_text("❌ Hãy tag một người chơi hợp lệ hoặc chọn /caro bot!", tid, tt);
                return;
            }
        }
        cpp_int bet = 0;
        if (args.size() >= 3) {
            if (!account.is_logged_in(uid) && !g_debug_mode) {
                reply_text("❌ Cần đăng nhập để đặt cược!", tid, tt);
                return;
            }
            if (!vs_bot && !account.is_logged_in(target) && !g_debug_mode) {
                reply_text("❌ " + format_user(target) + " chưa đăng nhập để cược!", tid, tt);
                return;
            }
            bet = parse_bet_amount(args[2], econ.get_bank(uid));
            if (bet < 1000) { reply_text("❌ Cược tối thiểu 1.000 VNĐ!", tid, tt); return; }
            if (econ.get_bank(uid) < bet) { reply_text("❌ Bạn không đủ tiền cược!", tid, tt); return; }
            if (!vs_bot && econ.get_bank(target) < bet) {
                reply_text("❌ " + format_user(target) + " không đủ tiền cược!", tid, tt);
                return;
            }
            econ.place_bet(uid, bet);
            if (!vs_bot) econ.place_bet(target, bet);
            g_econ_dirty = true;
        }

        std::string p2 = vs_bot ? "__BOT__" : target;
        game.create_caro_game(uid, p2, tid, tt, bet);
        CaroBoard* b = game.get_caro_game(tid);
        std::string p1_name = format_user(uid);
        std::string p2_name = vs_bot ? get_bot_name() : format_user(target);
        static std::atomic<int> cr_ctr{0};
        std::string out = paths::cache("caro_") + std::to_string(cr_ctr++) + ".png";
        extra_render::render_caro_board(b->board, -1, -1, p1_name, p2_name, 1,
                                        "Go <dong> <cot> (VD: 7 7) de danh!", out);
        std::string cap = "♟️ CỜ CARO 15x15 BẮT ĐẦU!\n❌ " + p1_name + "  vs  ⭕ " + p2_name;
        if (bet > 0) cap += "\n💰 Tiền cược: " + money_str(bet) + " VNĐ";
        cap += "\n👉 Lượt của " + p1_name + " (X): gõ \"<dòng> <cột>\" (VD: 7 7)";
        api.send_message_with_image_async(cap, out, tid, tt);
    };

    commands["group"] = [this](const auto&, const auto&, const auto& tid, int tt, bool) {
        if (tt == 0) { reply_text("❌ Lệnh này chỉ dùng trong nhóm!", tid, tt); return; }
        auto resp = api.get_group_info(tid);
        if (resp.contains("error") || !resp.value("ok", true)) {
            reply_text("❌ Không lấy được thông tin nhóm.", tid, tt);
            return;
        }
        json info = json::object();
        if (resp.contains("result") && resp["result"].is_object()) {
            auto& r = resp["result"];
            if (r.contains("gridInfoMap") && r["gridInfoMap"].is_object()) {
                auto& gm = r["gridInfoMap"];
                if (gm.contains(tid) && gm[tid].is_object()) info = gm[tid];
                else if (!gm.empty()) {
                    for (auto& [k, v] : gm.items()) { if (v.is_object()) { info = v; break; } }
                }
            }
            if (info.empty() && r.contains("name")) info = r;
        }
        if (info.empty()) { reply_text("❌ Không đọc được dữ liệu nhóm.", tid, tt); return; }

        std::string gname = info.value("name", "Nhóm Zalo");
        int members = info.value("totalMember", 0);
        if (members <= 0 && info.contains("memberIds") && info["memberIds"].is_array())
            members = (int)info["memberIds"].size();
        int admins = 0;
        if (info.contains("adminIds") && info["adminIds"].is_array())
            admins = (int)info["adminIds"].size();
        std::string creator = info.value("creatorId", "");
        std::string owner_name = creator.empty() ? "Ẩn" : format_user(creator);
        std::string desc = info.value("desc", "");
        bool lock_chat = false, join_appr = false;
        if (info.contains("setting") && info["setting"].is_object()) {
            lock_chat = info["setting"].value("lockSendMsg", 0) == 1;
            join_appr = info["setting"].value("joinAppr", 0) == 1;
        }
        long long created_ts = info.value("createdTime", 0LL);
        std::string created_str = "Ẩn";
        if (created_ts > 0) {
            if (created_ts > 100000000000LL) created_ts /= 1000;
            time_t ct = (time_t)created_ts;
            struct tm* tmi = localtime(&ct);
            char buf[32];
            strftime(buf, sizeof(buf), "%d/%m/%Y", tmi);
            created_str = buf;
        }
        std::string avt_url = info.value("fullAvt", info.value("avt", ""));
        std::string avt_key;
        if (!avt_url.empty()) {
            std::string avt_path = paths::cache("grp_") + tid + ".png";
            std::ifstream ft(avt_path, std::ios::binary);
            bool ex = ft.good(); ft.close();
            if (!ex) api.download_image(avt_url, avt_path);
            if (AssetManager::load("grp_" + tid, avt_path)) avt_key = "grp_" + tid;
        }
        static std::atomic<int> grp_ctr{0};
        std::string out = paths::cache("group_") + std::to_string(grp_ctr++) + ".png";
        extra_render::render_group_info(gname, tid, members, admins, owner_name, created_str, desc, lock_chat, join_appr, avt_key, out);
        api.send_message_with_image_async("🏠 Thông tin nhóm: " + gname, out, tid, tt);
    };
    commands["groupinfo"] = commands["group"];

    commands["voice"] = [this](const auto& args, const auto&, const auto& tid, int tt, bool) {
        if (args.size() < 2) {
            reply_text("❌ Cú pháp: /voice <nội dung cần đọc>", tid, tt);
            return;
        }
        std::string raw = ctx().msg_text;
        size_t sp = raw.find_first_of(" \t\n");
        std::string text = (sp == std::string::npos) ? "" : raw.substr(sp + 1);
        size_t b = text.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) { reply_text("❌ Nội dung trống!", tid, tt); return; }
        text = text.substr(b);
        api.send_voice_text_async(text, tid, tt);
    };

    auto register_media_cmd = [this](const std::string& cmd_name, const std::string& filename, const std::string& label) {
        commands[cmd_name] = [this, cmd_name, filename, label](const auto&, const auto&, const auto& tid, int tt, bool) {
            std::ifstream f(paths::data(filename).c_str());
            if (!f.good()) { reply_text("❌ Không tìm thấy dữ liệu " + filename, tid, tt); return; }
            std::vector<std::string> urls;
            std::string line;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("http", 0) == 0) urls.push_back(line);
            }
            if (urls.empty()) { reply_text("❌ Danh sách ảnh trống!", tid, tt); return; }
            static std::atomic<int> m_ctr{0};
            for (int attempt = 0; attempt < 2; attempt++) {
                std::string url = urls[myRandom::RanInt(0, (int)urls.size() - 1)];
                std::string out = paths::cache(cmd_name + "_") + std::to_string(m_ctr++) + ".jpg";
                if (api.download_image(url, out)) {
                    api.send_message_with_image_async(label, out, tid, tt);
                    return;
                }
            }
            reply_text("❌ Tải ảnh thất bại, thử lại sau nhé!", tid, tt);
        };
    };
    register_media_cmd("girl",    "girl.txt",    "📸 Ảnh gái xinh ngẫu nhiên");
    register_media_cmd("boy",     "boy.txt",     "📸 Ảnh trai đẹp ngẫu nhiên");
    register_media_cmd("anime",   "anime.txt",   "✨ Ảnh Anime ngẫu nhiên");
    register_media_cmd("cosplay", "cosplay.txt", "🎭 Ảnh Cosplay ngẫu nhiên");

    auto register_line_text_cmd = [this](const std::string& cmd_name, const std::string& filename, const std::string& header, bool unescape_nl) {
        commands[cmd_name] = [this, filename, header, unescape_nl](const auto&, const auto&, const auto& tid, int tt, bool) {
            std::ifstream f(paths::data(filename).c_str());
            if (!f.good()) { reply_text("❌ Không tìm thấy " + filename, tid, tt); return; }
            std::vector<std::string> items;
            std::string line;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) items.push_back(line);
            }
            if (items.empty()) { reply_text("❌ Dữ liệu trống!", tid, tt); return; }
            std::string pick = items[myRandom::RanInt(0, (int)items.size() - 1)];
            if (unescape_nl) {
                std::string out;
                for (size_t i = 0; i < pick.size(); i++) {
                    if (pick[i] == '\\' && i + 1 < pick.size() && pick[i + 1] == 'n') {
                        out += '\n'; i++;
                    } else out += pick[i];
                }
                pick = out;
            }
            reply_text(header + "\n" + pick, tid, tt);
        };
    };
    register_line_text_cmd("cadao", "cadao.txt", "📜 CA DAO TỤC NGỮ", true);
    register_line_text_cmd("tarot", "tarot.txt", "🔮 LÁ BÀI TAROT HÔM NAY", true);
    register_line_text_cmd("thinh", "thinh.txt", "💘 THẢ THÍNH", true);

    commands["truyencuoi"] = [this](const auto&, const auto&, const auto& tid, int tt, bool) {
        std::ifstream f(paths::data("truyencuoi.txt").c_str());
        if (!f.good()) { reply_text("❌ Không tìm thấy truyencuoi.txt", tid, tt); return; }
        std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::vector<std::string> stories;
        const std::string delim = "Truyện:";
        size_t pos = 0;
        while ((pos = all.find(delim, pos)) != std::string::npos) {
            size_t next = all.find(delim, pos + delim.size());
            std::string chunk = (next == std::string::npos) ? all.substr(pos) : all.substr(pos, next - pos);
            std::string unesc;
            for (size_t i = 0; i < chunk.size(); i++) {
                if (chunk[i] == '\\' && i + 1 < chunk.size() && chunk[i + 1] == 'n') {
                    unesc += '\n'; i++;
                } else unesc += chunk[i];
            }
            if (!unesc.empty()) stories.push_back(unesc);
            if (next == std::string::npos) break;
            pos = next;
        }
        if (stories.empty()) { reply_text("❌ Dữ liệu truyện cười trống!", tid, tt); return; }
        std::string pick = stories[myRandom::RanInt(0, (int)stories.size() - 1)];
        reply_text("😂 " + pick, tid, tt);
    };

    commands["dich"] = [this](const auto& args, const auto&, const auto& tid, int tt, bool) {
        std::string target_lang = "vi";
        std::string text;
        if (args.size() >= 2) {
            std::string a1 = args[1];
            std::transform(a1.begin(), a1.end(), a1.begin(), [](unsigned char c){ return (char)std::tolower(c); });
            static const std::set<std::string> langs = {"vi","en","ja","ko","zh","fr","de","es","th","ru"};
            if (langs.count(a1)) {
                target_lang = a1;
                for (size_t i = 2; i < args.size(); i++) { if (i > 2) text += " "; text += args[i]; }
            } else {
                for (size_t i = 1; i < args.size(); i++) { if (i > 1) text += " "; text += args[i]; }
            }
        }
        if (text.empty() && ctx().quote.has_quote) text = ctx().quote.content;
        if (text.empty()) {
            reply_text("🌐 Cú pháp: /dich [en|vi|ja|ko|zh|...] <văn bản>\nHoặc reply một tin nhắn rồi gõ /dich [lang]", tid, tt);
            return;
        }
        auto r = api.translate_text(text, target_lang);
        if (!r.value("ok", false)) {
            reply_text("❌ Lỗi dịch: " + r.value("error", "không xác định"), tid, tt);
            return;
        }
        const auto& res = r.contains("result") ? r["result"] : r;
        std::string out_txt = res.value("text", "");
        std::string from_l  = res.value("from", "auto");
        reply_text("🌐 DỊCH (" + from_l + " → " + target_lang + "):\n" + out_txt, tid, tt);
    };

    commands["thoitiet"] = [this](const auto& args, const auto&, const auto& tid, int tt, bool) {
        std::string city;
        for (size_t i = 1; i < args.size(); i++) { if (i > 1) city += " "; city += args[i]; }
        if (city.empty()) city = "Hanoi";
        auto r = api.get_weather(city);
        if (!r.value("ok", false)) {
            reply_text("❌ Không lấy được thời tiết cho \"" + city + "\".", tid, tt);
            return;
        }
        const auto& res = r.contains("result") ? r["result"] : r;
        std::string name = res.value("name", city);
        std::string country = res.value("country", "");
        auto cur = res.value("current", json::object());
        auto day = res.value("daily", json::object());
        double temp = cur.value("temperature_2m", 0.0);
        double feels = cur.value("apparent_temperature", temp);
        int hum = cur.value("relative_humidity_2m", 0);
        double wind = cur.value("wind_speed_10m", 0.0);
        int wcode = cur.value("weather_code", 0);
        auto wmo_desc = [](int c) -> std::string {
            if (c == 0) return "☀️ Trời quang đãng";
            if (c <= 3) return "⛅ Ít mây / Có mây";
            if (c == 45 || c == 48) return "🌫️ Có sương mù";
            if (c >= 51 && c <= 67) return "🌧️ Có mưa";
            if (c >= 80 && c <= 82) return "🌦️ Mưa rào";
            if (c >= 95) return "⛈️ Có dông bão";
            return "🌤️ Thời tiết ổn định";
        };
        std::ostringstream os;
        os.precision(1);
        os << std::fixed;
        os << "🌤️ THỜI TIẾT: " << name << (country.empty() ? "" : ", " + country) << "\n";
        os << "• Trạng thái: " << wmo_desc(wcode) << "\n";
        os << "• Nhiệt độ: " << temp << "°C (cảm giác " << feels << "°C)\n";
        if (day.contains("temperature_2m_min") && day["temperature_2m_min"].is_array() && !day["temperature_2m_min"].empty()) {
            os << "• Thấp / Cao: " << day["temperature_2m_min"][0].get<double>() << "°C / "
               << day["temperature_2m_max"][0].get<double>() << "°C\n";
        }
        os << "• Độ ẩm: " << hum << "% | Gió: " << wind << " km/h";
        reply_text(os.str(), tid, tt);
    };

    commands["mute"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (tt == 0) { reply_text("❌ Chỉ dùng trong nhóm!", tid, tt); return; }
        if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin bot mới dùng được lệnh này!", tid, tt); return; }
        if (args.size() < 2) { reply_text("❌ Cú pháp: /mute <@user> [số phút]", tid, tt); return; }
        std::string target = args[1];
        if (perm.is_admin(target)) { reply_text("❌ Không thể mute admin bot!", tid, tt); return; }
        int mins = 10;
        if (args.size() >= 3) {
            try { mins = std::stoi(args[2]); } catch (...) { mins = 10; }
        }
        group.mute_user(tid, target, mins);
        reply_text("🔇 Đã cấm chat " + format_user(target) + (mins > 0 ? (" trong " + std::to_string(mins) + " phút.") : " vô thời hạn."), tid, tt);
    };

    commands["unmute"] = [this](const auto& args, const auto& uid, const auto& tid, int tt, bool) {
        if (tt == 0) { reply_text("❌ Chỉ dùng trong nhóm!", tid, tt); return; }
        if (!perm.is_admin(uid)) { reply_text("❌ Chỉ admin bot mới dùng được lệnh này!", tid, tt); return; }
        if (args.size() < 2) { reply_text("❌ Cú pháp: /unmute <@user>", tid, tt); return; }
        std::string target = args[1];
        group.unmute_user(tid, target);
        reply_text("🔊 Đã bỏ cấm chat cho " + format_user(target) + ".", tid, tt);
    };

commands["stop"] = exit_handler;

}

// ============================================================
// Message cache for /undo
// ============================================================
void CommandHandler::cache_message(const std::string& thread_id,
                                   const std::string& msg_id,
                                   const std::string& cli_msg_id,
                                   const std::string& uid_from,
                                   const std::string& sender_name,
                                   const std::string& content,
                                   bool is_self) {
    if (thread_id.empty() || msg_id.empty()) return;
    std::lock_guard<std::mutex> lk(msgs_mtx_);
    CachedMessage cm;
    cm.msg_id      = msg_id;
    cm.cli_msg_id  = cli_msg_id;
    cm.uid_from    = uid_from;
    cm.sender_name = sender_name;
    cm.content     = content;
    cm.is_self     = is_self;
    cm.ts          = std::time(nullptr);
    auto& dq = thread_msgs_[thread_id];
    dq.push_back(cm);
    while (dq.size() > 100) dq.pop_front();
    auto now = std::time(nullptr);
    while (!dq.empty() && (now - dq.front().ts) > 1800) dq.pop_front();
}
void CommandHandler::handle_message(const std::string& user_id, const std::string& thread_id,
                                    int thread_type, const std::string& message,
                                    const std::string& sender_name,
                                    bool is_self, const QuoteInfo& quote) {
    ctx().sender_id.clear();
    ctx().msg_text.clear();
    ctx().sender_name.clear();
    ctx().quote = QuoteInfo{};

    try {
    ctx().sender_id.clear();
    ctx().msg_text.clear();
    ctx().sender_name.clear();
    ctx().quote = QuoteInfo{};
    ctx().sender_id = user_id;
    ctx().msg_text = message;
    ctx().sender_name = sender_name;
    ctx().quote = quote;
    if (!user_id.empty() && !sender_name.empty()) {
        perm.set_user_name(user_id, sender_name);
        cache_user_name(user_id, sender_name);
    }
    if (game.has_active_game(user_id)) {
        game.handle_input(user_id, message, thread_id);
        return;
    }
    if (message.size() >= 6) {
        std::string head = message.substr(0, 6);
        if (head == "Prefix" || head == "prefix") {
            std::string np = message.substr(6);
            // Hỗ trợ cú pháp "Prefix -> !" (bỏ dấu -> nếu user gõ)
            {
                auto arrow = np.find("->");
                if (arrow != std::string::npos) np = np.substr(arrow + 2);
            }
            const std::string ws = " \t\r\n";
            size_t s = np.find_first_not_of(ws);
            if (s != std::string::npos) {
                size_t e = np.find_last_not_of(ws);
                np = np.substr(s, e - s + 1);
            } else np = "";
            if (np.empty()) {
                reply_text("📌 Prefix hiện tại: \"" + prefix + "\"\n"
                           "Cú pháp: Prefix <prefix mới>\nVD: Prefix !", thread_id, thread_type);
                return;
            }
            if (np.size() > 3) {
                reply_text("❌ Prefix tối đa 3 ký tự", thread_id, thread_type);
                return;
            }
            if (!perm.is_super_admin(user_id)) { reply_text("❌ Chỉ SUPER ADMIN!", thread_id, thread_type); return; }
            set_prefix(np);
            reply_text("✅ Đổi prefix thành \"" + np + "\"\nVD: " + np + "help", thread_id, thread_type);
            return;
        }
    }
    auto raw_args = parse_command(message);
    if (raw_args.empty()) return;

    std::string mention_uid;
    std::string cmd_token;
    std::vector<std::string> clean_args;
    bool consumed_first = false;
    for (const auto& tok : raw_args) {
        if (tok.size() >= 4 && tok.substr(0, 2) == "<@" && tok.back() == '>') {
            if (mention_uid.empty()) mention_uid = tok.substr(2, tok.size() - 3);
            continue;
        }
        if (!consumed_first) {
            consumed_first = true;
            if (tok.size() > prefix.size() &&
                tok.substr(0, prefix.size()) == prefix) {
                cmd_token = tok;
                continue;
            }
            clean_args.push_back(tok);
            continue;
        }
        clean_args.push_back(tok);
    }
    if (cmd_token.empty()) return;

    std::vector<std::string> args;
    args.push_back(cmd_token);
    if (!mention_uid.empty()) args.push_back(mention_uid);
    for (const auto& a : clean_args) args.push_back(a);

    std::string cmd = cmd_token.substr(prefix.length());
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (!cmd.empty() && (cmd[0] == '-' || cmd[0] == ':')) return;
    std::cout << "[hmsg] cmd=" << cmd << " tt=" << thread_type << " tid=" << thread_id
              << " bot=" << (get_bot_for_thread(thread_id) ? 1 : 0)
              << " game=" << (get_game_for_thread(thread_id) ? 1 : 0) << "\n";

    // --- DM whitelist ---
    if (thread_type == 0) {
        static const std::set<std::string> dm_allowed = {
            "login", "signup", "logout", "whoami", "help", "command", "multiprofile", "reload"
        };
        bool is_admin_cmd_dm = (cmd == "bot" || cmd == "game" || cmd == "debug" ||
                                cmd == "on" || cmd == "off" ||
                                cmd == "setad" || cmd == "listadmin" || cmd == "setprefix");
        if (dm_allowed.find(cmd) == dm_allowed.end() &&
            !(perm.is_admin(user_id) && is_admin_cmd_dm)) {
            reply_text("\u274C V\u00E0o group \u0111\u1EC3 ch\u01A1i. IB ch\u1EC9 \u0111\u1EC3 \u0111\u0103ng nh\u1EADp/\u0111\u0103ng k\u00FD.", thread_id, thread_type);
            return;
        }
    }

    // --- Game gate per-thread ---
    {
        bool is_game_cmd = (cmd == "chanle" || cmd == "cl" || cmd == "taixiu" || cmd == "tx" ||
                            cmd == "baucua" || cmd == "bc" || cmd == "doanso" || cmd == "doantu" ||
                            cmd == "caro" || cmd == "noitu" || cmd == "keobuabao" || cmd == "kbb" ||
                            cmd == "vietlott" || cmd == "soicau" || cmd == "sc" ||
                            cmd == "nongtrai" || cmd == "ntr");
        if (is_game_cmd && !get_game_for_thread(thread_id) && !perm.is_admin(user_id)) {
            return;
        }
    }

    // --- Bot gate per-thread ---
    if (!get_bot_for_thread(thread_id)) {
        bool is_admin_cmd_bot = (cmd == "on" || cmd == "off" || cmd == "bot" ||
                                 cmd == "game" || cmd == "debug");
        // Auth commands luon duoc phep (chi trong DM) du bot dang OFF
        bool is_auth_cmd = (thread_type == 0) &&
                            (cmd == "login" || cmd == "signup" ||
                             cmd == "logout" || cmd == "whoami" ||
                             cmd == "help" || cmd == "command" || cmd == "multiprofile" || cmd == "reload");
        if (!is_auth_cmd && (!is_admin_cmd_bot || !perm.is_admin(user_id))) return;
    }
    // Debug auto-refill khi bank < 1000
    if (g_debug_mode && user_id == g_debug_uid) {
        if (econ.get_bank(user_id) < 1000) {
            UserEconomy _e = econ.get_user_economy(user_id);
            econ.set_full_data(user_id, _e.wallet, cpp_int(DEBUG_BANK_LL), _e.debt, _e.games_played, _e.games_won);
            std::cout << "[Debug] Auto-refill bank -> 1b for " << user_id << "\n";
        }
    }

    auto it = commands.find(cmd);
    if (it != commands.end()) {
        it->second(args, user_id, thread_id, thread_type, is_self);

        if (cmd == "nap" || cmd == "rut" || cmd == "bank" || cmd == "vay" ||
            cmd == "tra" || cmd == "chuyen" ||
            cmd == "chanle" || cmd == "cl" || cmd == "taixiu" || cmd == "tx" ||
            cmd == "baucua" || cmd == "bc" || cmd == "keobuabao" || cmd == "kbb" ||
            cmd == "vietlott" || cmd == "nongtrai" || cmd == "ntr" || cmd == "doanso" || cmd == "caro" ||
            cmd == "setmoney" || cmd == "sethu" || cmd == "buff" ||
            cmd == "nerf" || cmd == "daily") {
            g_econ_dirty = true;
        }
    } else {
        // Gợi ý lệnh (Did you mean)
        std::string best_match = "";
        int min_dist = 3;
        for (auto& p : commands) {
            if (p.first.length() < 3) continue;
            int dist = levenshtein(cmd, p.first);
            if (dist < min_dist && dist < (int)cmd.length()) {
                min_dist = dist;
                best_match = p.first;
            }
        }
        if (!best_match.empty()) {
            reply_text("❌ Lệnh không hợp lệ.\n👉 Có phải ý bạn là: /" + best_match + "?", thread_id, thread_type);
        } else {
            reply_text("❌ Lệnh không hợp lệ. Gõ /command để xem danh sách.", thread_id, thread_type);
        }
    }    } catch (const std::exception& e) {
        std::cerr << "[CRITICAL] Crash in handle_message: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[CRITICAL] Unknown crash in handle_message\n";
    }
}
// ============================================================
// Tick taixiu - goi tu main loop moi 1s
// ============================================================
void CommandHandler::tick_taixiu() {
    std::time_t now = std::time(nullptr);
    std::vector<std::string> to_end;
    std::vector<std::string> to_kill;
    std::vector<std::pair<std::string,int>> to_update;
    {
        std::lock_guard<std::mutex> lk(g_taixiu_mtx);
        for (auto& kv : g_taixiu_sessions) {
            if (!kv.second.active) continue;
            if (kv.second.players.empty()) {
                if (now - kv.second.start_time > 60) to_kill.push_back(kv.first);
                continue;
            }
            if (now >= kv.second.end_time) { to_end.push_back(kv.first); continue; }
            if (now - kv.second.last_update >= 10) {
                int rem = (int)(kv.second.end_time - now);
                if (rem < 0) rem = 0;
                to_update.push_back({kv.first, rem});
                kv.second.last_update = now;
            }
        }
        for (auto& tid : to_kill) g_taixiu_sessions[tid].active = false;
    }
    for (auto& tid : to_end) end_taixiu_session(tid);
    for (auto& pr : to_update) send_taixiu_update(pr.first, pr.second);
}

void CommandHandler::send_taixiu_update(const std::string& thread_id, int remaining) {
    TaixiuSession sess;
    {
        std::lock_guard<std::mutex> lk(g_taixiu_mtx);
        auto it = g_taixiu_sessions.find(thread_id);
        if (it == g_taixiu_sessions.end() || !it->second.active) return;
        sess = it->second;
    }
    if (sess.players.empty()) return;

    // Xoa anh waiting cu (neu co)
    if (!sess.waiting_msg_id.empty()) {
        api.undo_message(sess.waiting_msg_id, sess.waiting_cli_msg_id, thread_id, 1);
        std::cout << "[TX] Deleted old waiting msg=" << sess.waiting_msg_id << "\n";
    }

    cpp_int tai_total = 0, xiu_total = 0;
    for (auto& p : sess.players) {
        if (p.bet_type == "tai") tai_total += p.amount;
        else xiu_total += p.amount;
    }

    static std::atomic<int> ctr{0};
    std::string img = paths::cache("tx_wait_") + std::to_string(ctr++) + ".png";
    taixiu_render::render_waiting(remaining, tai_total, xiu_total, img);

    std::string cap = "[\U0001F3B2 T\u00E0i X\u1EC9u]";
    cap += "\nTh\u1EDDi gian c\u00F2n l\u1EA1i: " + std::to_string(remaining) + "s";
    cap += "\nT\u1ED5ng T\u00E0i: " + money_str(tai_total) + " VN\u0110";
    cap += "\nT\u1ED5ng X\u1EC9u: " + money_str(xiu_total) + " VN\u0110";
    cap += "\nS\u1ED1 ng\u01B0\u1EDDi ch\u01A1i: " + std::to_string(sess.players.size());

    std::vector<std::tuple<std::string,int,int>> empty;
    json r = api.send_message_with_image_mentions_sync(cap, img, thread_id, 1, empty);
    std::cout << "[TX] RAW response = " << r.dump() << "\n";

    // Parse msgId tu response (zca-js tra ve nhieu format khac nhau)
    std::string new_msg_id, new_cli_msg_id;
    try {
        auto try_extract = [&](const json& node) {
            if (!node.is_object()) return;
            if (new_msg_id.empty() && node.contains("msgId")) {
                const auto& v = node["msgId"];
                if (v.is_string()) new_msg_id = v.get<std::string>();
                else if (v.is_number()) new_msg_id = std::to_string(v.get<long long>());
            }
            if (new_cli_msg_id.empty() && node.contains("cliMsgId")) {
                const auto& v = node["cliMsgId"];
                if (v.is_string()) new_cli_msg_id = v.get<std::string>();
                else if (v.is_number()) new_cli_msg_id = std::to_string(v.get<long long>());
            }
        };

        if (r.contains("result") && r["result"].is_object()) {
            auto& res = r["result"];
            try_extract(res);
            if (new_msg_id.empty() && res.contains("message")) try_extract(res["message"]);
            if (new_msg_id.empty() && res.contains("attachment") && res["attachment"].is_array() && !res["attachment"].empty()) {
                try_extract(res["attachment"][0]);
            }
            if (new_msg_id.empty() && res.contains("msgIds") && res["msgIds"].is_array() && !res["msgIds"].empty()) {
                const auto& v = res["msgIds"][0];
                new_msg_id = v.is_string() ? v.get<std::string>() : std::to_string(v.get<long long>());
            }
        }

        if (new_msg_id.empty() && r.contains("attachment") && r["attachment"].is_array() && !r["attachment"].empty()) {
            try_extract(r["attachment"][0]);
        }

        if (new_msg_id.empty() && r.contains("message") && r["message"].is_object()) {
            try_extract(r["message"]);
        }

        if (new_msg_id.empty()) try_extract(r);
    } catch (const std::exception& e) {
        std::cout << "[TX] Parse msgId error: " << e.what() << "\n";
    }

    std::cout << "[TX] Update thread=" << thread_id << " rem=" << remaining
              << " new_msg=" << new_msg_id << " new_cli=" << new_cli_msg_id << "\n";

    {
        std::lock_guard<std::mutex> lk(g_taixiu_mtx);
        auto it = g_taixiu_sessions.find(thread_id);
        if (it != g_taixiu_sessions.end()) {
            it->second.waiting_msg_id = new_msg_id;
            it->second.waiting_cli_msg_id = new_cli_msg_id;
        }
    }
}

void CommandHandler::end_taixiu_session(const std::string& thread_id) {
    TaixiuSession sess;
    {
        std::lock_guard<std::mutex> lk(g_taixiu_mtx);
        auto it = g_taixiu_sessions.find(thread_id);
        if (it == g_taixiu_sessions.end() || !it->second.active) return;
        sess = it->second;
        it->second = TaixiuSession{};
    }
    if (sess.players.empty()) return;

    // Undo tin nhan "waiting" cuoi cung truoc khi gui result
    if (!sess.waiting_msg_id.empty()) {
        api.undo_message(sess.waiting_msg_id, sess.waiting_cli_msg_id, thread_id, 1);
        std::cout << "[TX] Undo last waiting msg=" << sess.waiting_msg_id << "\n";
    }

    int d1 = myRandom::Dice(), d2 = myRandom::Dice(), d3 = myRandom::Dice();
    int total = d1 + d2 + d3;
    std::string result = (total > 10) ? "tai" : "xiu";
    {
        std::lock_guard<std::mutex> lk(g_taixiu_mtx);
        g_taixiu_history_pts.push_back({total, result == "tai"});
        if (g_taixiu_history_pts.size() > 20) g_taixiu_history_pts.erase(g_taixiu_history_pts.begin());
    }

    cpp_int tai_total = 0, xiu_total = 0, husk_add = 0;
    for (auto& p : sess.players) {
        if (p.bet_type == "tai") tai_total += p.amount;
        else xiu_total += p.amount;
        if (p.bet_type == result) {
            econ.add_winnings(p.uid, p.amount * 2);
        } else {
            econ.record_loss(p.uid, p.amount);
            husk_add += p.amount * 60 / 100;
        }
    }
    if (husk_add > 0) econ.add_to_husk("tai_xiu", husk_add);
    cpp_int husk_after = econ.get_husk("tai_xiu");
    save_husks();
    g_econ_dirty = true;

    static std::atomic<int> ctr{0};
    std::string img = paths::cache("taixiu_") + std::to_string(ctr++) + ".png";
    std::vector<int> dice_vec = {d1, d2, d3};
    taixiu_render::render_result(dice_vec, total, result, tai_total, xiu_total,
                                  false, cpp_int(0), img);

    std::string cap = "K\u1EBFt qu\u1EA3: " + std::to_string(d1) + " - " + std::to_string(d2) + " - " + std::to_string(d3);
    cap += "\nT\u1ED5ng: " + std::to_string(total) + " - " + (result == "tai" ? "T\u00C0I" : "X\u1EC8U");
    cap += "\n\n";

    std::vector<std::tuple<std::string, int, int>> mentions;
    for (auto& p : sess.players) {
        int pos = (int)cap.size();
        std::string tag = p.name;
        cap += tag + ": [" + (p.bet_type == "tai" ? "T\u00E0i" : "X\u1EC9u") + "] ";
        if (p.bet_type == result) cap += "Th\u1EAFng " + money_str(p.amount) + " VN\u0110";
        else cap += "Thua " + money_str(p.amount) + " VN\u0110";
        cap += "\n";
        mentions.push_back({p.uid, pos, (int)tag.size()});
    }
    cap += "\nTi\u1EC1n h\u0169 hi\u1EC7n t\u1EA1i: " + money_str(husk_after) + " VN\u0110 \U0001F4B0";

    api.send_message_with_image_mentions_async(cap, img, thread_id, 1, mentions);
    std::cout << "[TX] End thread=" << thread_id << " result=" << result << " n=" << sess.players.size() << "\n";
}

















void CommandHandler::handle_game_chat(const std::string& thread_id, int thread_type, const std::string& text, const std::string& sender_uid, const std::string& sender_name) {
    ctx().sender_id.clear();
    ctx().msg_text.clear();
    ctx().sender_name.clear();
    ctx().quote = QuoteInfo{};

    ctx().sender_id   = sender_uid;
    ctx().sender_name = sender_name;
    ctx().msg_text    = text;

    const bool isCmd = !text.empty() && !prefix.empty() && text.rfind(prefix, 0) == 0;
    const bool hasNewline = text.find('\n') != std::string::npos;
    if (isCmd || text.empty()) return;

    // 1. Cờ Caro 15x15 (<dòng> <cột>)
    if (CaroBoard* cb = game.get_caro_game(thread_id)) {
        std::istringstream iss(text);
        int r = -1, c = -1;
        std::string extra;
        if ((iss >> r >> c) && !(iss >> extra) && r >= 0 && r < 15 && c >= 0 && c < 15) {
            std::string expected_uid = (cb->current_turn == 1) ? cb->player1_id : cb->player2_id;
            if (sender_uid != expected_uid) return;
            if (cb->board[r][c] != '.') {
                reply_text("⚠️ Ô (" + std::to_string(r) + ", " + std::to_string(c) + ") đã được đánh rồi!", thread_id, thread_type);
                return;
            }
            bool won = game.caro_move(sender_uid, r, c);
            std::string p1n = format_user(cb->player1_id);
            std::string p2n = cb->vs_bot ? get_bot_name() : format_user(cb->player2_id);
            std::string winner_name = format_user(sender_uid);

            if (won) {
                g_econ_dirty = true;
                static std::atomic<int> cr_ctr{0};
                std::string out = paths::cache("caro_") + std::to_string(cr_ctr++) + ".png";
                extra_render::render_caro_board(cb->board, cb->last_r, cb->last_c, p1n, p2n, cb->current_turn,
                                                winner_name + " THANG CUOC!", out);
                std::string cap = "🏆 " + winner_name + " ĐÃ CHIẾN THẮNG CỜ CARO!";
                if (cb->bet_amount > 0) cap += "\n💰 Tiền thưởng: +" + money_str(cb->bet_amount) + " VNĐ";
                api.send_message_with_image_async(cap, out, thread_id, thread_type);
                return;
            }

            if (!cb->is_active) {
                g_econ_dirty = true;
                static std::atomic<int> cr_ctr{0};
                std::string out = paths::cache("caro_") + std::to_string(cr_ctr++) + ".png";
                extra_render::render_caro_board(cb->board, cb->last_r, cb->last_c, p1n, p2n, cb->current_turn,
                                                "HOA NHAU!", out);
                api.send_message_with_image_async("🤝 Bàn cờ đầy — HÒA!", out, thread_id, thread_type);
                return;
            }

            if (cb->vs_bot && cb->is_active) {
                auto ai_mv = game.pick_caro_ai_move(thread_id);
                bool bot_won = game.caro_move("__BOT__", ai_mv.first, ai_mv.second);
                static std::atomic<int> cr_ctr{0};
                std::string out = paths::cache("caro_") + std::to_string(cr_ctr++) + ".png";
                if (bot_won) {
                    g_econ_dirty = true;
                    extra_render::render_caro_board(cb->board, cb->last_r, cb->last_c, p1n, p2n, 2,
                                                    p2n + " THANG CUOC!", out);
                    api.send_message_with_image_async("🤖 " + p2n + " đã đánh (" + std::to_string(ai_mv.first) + ", " + std::to_string(ai_mv.second) + ") và CHIẾN THẮNG!", out, thread_id, thread_type);
                    return;
                }
                if (!cb->is_active) {
                    g_econ_dirty = true;
                    extra_render::render_caro_board(cb->board, cb->last_r, cb->last_c, p1n, p2n, 1, "HOA NHAU!", out);
                    api.send_message_with_image_async("🤝 Bàn cờ đầy — HÒA!", out, thread_id, thread_type);
                    return;
                }
                extra_render::render_caro_board(cb->board, cb->last_r, cb->last_c, p1n, p2n, cb->current_turn,
                                                "Bot danh (" + std::to_string(ai_mv.first) + "," + std::to_string(ai_mv.second) + ") - Den luot " + p1n, out);
                api.send_message_with_image_async("♟️ Bot vừa đánh (" + std::to_string(ai_mv.first) + ", " + std::to_string(ai_mv.second) + "). Đến lượt bạn (X)!", out, thread_id, thread_type);
                return;
            }

            std::string next_name = (cb->current_turn == 1) ? p1n : p2n;
            std::string mark = (cb->current_turn == 1) ? "X" : "O";
            static std::atomic<int> cr_ctr{0};
            std::string out = paths::cache("caro_") + std::to_string(cr_ctr++) + ".png";
            extra_render::render_caro_board(cb->board, cb->last_r, cb->last_c, p1n, p2n, cb->current_turn,
                                            "Den luot " + next_name + " (" + mark + ")", out);
            api.send_message_with_image_async("♟️ " + winner_name + " đánh (" + std::to_string(r) + ", " + std::to_string(c) + "). Đến lượt " + next_name + " (" + mark + ")!", out, thread_id, thread_type);
            return;
        }
    }

    // 2. WordChain hook (real Vietnamese dictionary)
    {
        auto& wc = getWordChain();
        if (wc.isLoaded() && wc.hasActiveGame(thread_id) && !hasNewline) {
            PlayOutcome o = wc.playWord(thread_id, sender_uid, text);
            if (o.result == PlayResult::OK || o.result == PlayResult::WIN) {
                reply_text(o.bot_reply, thread_id, thread_type);
                return;
            } else if (o.result != PlayResult::WORD_NOT_TWO_SYLLABLES) {
                reply_text("❌ " + o.error_msg, thread_id, thread_type);
                return;
            }
        }
    }

    // 3. GuessNumber & WordGuess
    std::lock_guard<std::mutex> lk(g_taixiu_mtx);
    if (!g_mini_games.count(thread_id)) return;

    auto& g = g_mini_games[thread_id];
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (g.type == "guessNumber") {
        try {
            size_t pos_end = 0;
            int guess = std::stoi(text, &pos_end);
            if (pos_end != text.size()) return;
            if (guess < g.range_min || guess > g.range_max) {
                reply_text("❌ Số phải trong khoảng " + std::to_string(g.range_min) + " → " + std::to_string(g.range_max), thread_id, thread_type);
                return;
            }
            g.attempts++;
            int remain = g.maxAttempts - g.attempts;
            if (guess == g.targetNumber) {
                reply_text("🎉 Chúc mừng! Đúng rồi! Số là " + std::to_string(g.targetNumber) + ".\nBạn đoán sau " + std::to_string(g.attempts) + " lượt.", thread_id, thread_type);
                g_mini_games.erase(thread_id);
                return;
            }
            if (g.attempts >= g.maxAttempts) {
                reply_text("💀 Hết lượt! Số cần đoán là " + std::to_string(g.targetNumber) + ".\nGõ /doanso <min> <max> để chơi lại.", thread_id, thread_type);
                g_mini_games.erase(thread_id);
                return;
            }
            std::string hint = (guess < g.targetNumber) ? "📈 Cao hơn" : "📉 Thấp hơn";
            reply_text(hint + " (còn " + std::to_string(remain) + " lượt)", thread_id, thread_type);
            return;
        } catch (...) {}
    } else if (g.type == "wordGuess") {
        g.attempts++;
        if (lower == g.targetWord) {
            reply_text("🎉 Chúc mừng! Bạn đã đoán đúng từ \"" + g.targetWord + "\" sau " + std::to_string(g.attempts) + " lần thử.", thread_id, thread_type);
            g_mini_games.erase(thread_id);
            return;
        }
        if (g.attempts >= 10) {
            reply_text("💀 Trò chơi kết thúc! Từ cần đoán là \"" + g.targetWord + "\".", thread_id, thread_type);
            g_mini_games.erase(thread_id);
        } else {
            reply_text("❌ Đoán sai rồi! Còn " + std::to_string(10 - g.attempts) + " lượt.", thread_id, thread_type);
        }
    }
}
