#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <ctime>
#include <boost/multiprecision/cpp_int.hpp>
#include "EconomyManager.hpp"

using cpp_int = boost::multiprecision::cpp_int;

// ============================================================
// Crop definitions (từ data-nongtrai.js)
// growthTime tính bằng GIÂY (khớp JS: 60*60 = 1h)
// ============================================================
struct CropDef {
    std::string key;            // "LUA"
    std::string code;           // "L1"
    std::string name;           // "Lúa"
    std::string icon;           // "🌾"
    int growth_time_sec;        // 3600
    long long seed_price;       // 20000
    long long harvest_value;    // 1800000
    std::string crop_image_code; // "1".."11" (khớp assets/repo/nongtrai/caytrong/)
};

struct ShopItemDef {
    std::string key;            // "WATER"
    std::string code;           // "W1"
    std::string name;           // "Bình Nước"
    std::string icon;           // "💧"
    long long price;            // 10000
    double effect;              // 0.2 / 0.6 (tùy loại)
};

// Global registries
const std::map<std::string, CropDef>& get_crops();
const std::map<std::string, ShopItemDef>& get_shop_items();
const CropDef* find_crop_by_key(const std::string& key);
const CropDef* find_crop_by_code(const std::string& code);
const ShopItemDef* find_shop_item_by_code(const std::string& code);

// ============================================================
// Per-user farm state
// ============================================================
struct Plot {
    std::string crop_key;                // "" nếu trống
    long long planted_at = 0;            // epoch ms
    int water_level = 100;               // 0..100
    long long last_watered_at = 0;       // epoch ms
    std::string status = "good";         // dry | low | good
    std::vector<std::string> fertilized; // ["FERTILIZER", ...]
};

struct Farm {
    std::map<std::string, int> bag;      // item_code -> qty
    std::vector<Plot> plots;
};

class NongTraiManager {
public:
    NongTraiManager(EconomyManager& econ);

    // Load/save persistent state
    bool load(const std::string& path = "data/nong_trai.json");
    bool save(const std::string& path = "data/nong_trai.json");
    void set_dirty() { dirty_ = true; }
    bool is_dirty() const { return dirty_; }

    // Get or create farm for user (init 4 plots if new)
    Farm& get_or_create(const std::string& user_id);

    // Update water level based on elapsed time (gọi trước mỗi query)
    void refresh_soil(Farm& farm);

    // Trả về text mô tả trạng thái (dùng cho `nongtrai farm` text-only)
    std::string format_status_text(const std::string& user_id);

    // Đếm số ô + giá mảnh đất tiếp theo
    long long calculate_land_price(int current_plots) const;

    // Truy vấn bag
    int bag_count(const std::string& user_id, const std::string& item_code);
    void bag_add(const std::string& user_id, const std::string& item_code, int delta);

    // Mở khóa mutex cho handler gọi
    std::mutex& mtx() { return mtx_; }
    std::map<std::string, Farm>& raw() { return farms_; }

private:
    EconomyManager& econ_;
    std::map<std::string, Farm> farms_;
    bool dirty_ = false;
    std::mutex mtx_;
};