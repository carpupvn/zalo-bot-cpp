#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <ctime>
#include <boost/multiprecision/cpp_int.hpp>
#include "EconomyManager.hpp"

using cpp_int = boost::multiprecision::cpp_int;

struct CropDef {
    std::string key;
    std::string code;
    std::string name;
    std::string icon;
    int growth_time_sec;
    long long seed_price;
    long long harvest_value;
    std::string crop_image_code;
};

struct ShopItemDef {
    std::string key;
    std::string code;
    std::string name;
    std::string icon;
    long long price;
    double effect;
};

const std::map<std::string, CropDef>& get_crops();
const std::map<std::string, ShopItemDef>& get_shop_items();
const CropDef* find_crop_by_key(const std::string& key);
const CropDef* find_crop_by_code(const std::string& code);
const ShopItemDef* find_shop_item_by_code(const std::string& code);

struct Plot {
    std::string crop_key;
    long long planted_at = 0;
    int water_level = 100;
    long long last_watered_at = 0;
    std::string status = "good";
    std::vector<std::string> fertilized;
};

struct Farm {
    std::map<std::string, int> bag;
    std::vector<Plot> plots;
};

class NongTraiManager {
public:
    explicit NongTraiManager(EconomyManager& econ);

    bool load(const std::string& path = "data/nong_trai.json");
    bool save(const std::string& path = "data/nong_trai.json");
    void set_dirty() { dirty_ = true; }
    bool is_dirty() const { return dirty_; }

    Farm& get_or_create(const std::string& user_id);
    void refresh_soil(Farm& farm);

    std::string format_status_text(const std::string& user_id);
    long long calculate_land_price(int current_plots) const;

    int bag_count(const std::string& user_id, const std::string& item_code);
    void bag_add(const std::string& user_id, const std::string& item_code, int delta);

    std::mutex& mtx() { return mtx_; }
    std::map<std::string, Farm>& raw() { return farms_; }

private:
    EconomyManager& econ_;
    std::map<std::string, Farm> farms_;
    bool dirty_ = false;
    std::mutex mtx_;
};
