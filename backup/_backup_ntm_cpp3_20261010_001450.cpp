#include "NongTraiManager.hpp"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>

using json = nlohmann::json;

static std::map<std::string, CropDef> g_crops = {
    {"LUA",           {"LUA","L1","Lua","", 60*60,   20000, 1800000,"1"}},
    {"CACHUA",        {"CACHUA","C1","Ca Chua","",90*60,30000, 2700000,"2"}},
    {"CAROT",         {"CAROT","C2","Ca Rot","",120*60,35000, 3200000,"3"}},
    {"THOM",          {"THOM","T1","Thom","",180*60, 70000, 6300000,"4"}},
    {"DUAHAU",        {"DUAHAU","D1","Dua Hau","",150*60,50000,4750000,"5"}},
    {"NHO",           {"NHO","N1","Nho","",210*60,   90000, 8100000,"6"}},
    {"HOAHONG",       {"HOAHONG","H1","Hoa Hong","",120*60,40000,3600000,"7"}},
    {"XOAI",          {"XOAI","X1","Xoai","",240*60, 110000, 9900000,"8"}},
    {"THANHLONG",     {"THANHLONG","T2","Thanh Long","",180*60,60000,5400000,"9"}},
    {"HOAHUONGDUONG", {"HOAHUONGDUONG","H2","Hoa Huong Duong","",150*60,45000,4050000,"10"}},
    {"HOATULIP",      {"HOATULIP","H3","Hoa Tulip","",165*60,55000,4950000,"11"}},
};

static std::map<std::string, ShopItemDef> g_shop_items = {
    {"WATER",             {"WATER","W1","Binh Nuoc","",10000, 0.60}},
    {"SHOVEL",            {"SHOVEL","S1","Cuoc","",15000, 1.0}},
    {"LAND",              {"LAND","L0","Manh Dat","",10000000,1.0}},
    {"FERTILIZER",        {"FERTILIZER","F1","Phan Bon So Cap","",20000, 0.20}},
    {"MEDIUM_FERTILIZER", {"MEDIUM_FERTILIZER","MF1","Phan Bon Trung Cap","",35000, 0.30}},
    {"SUPER_FERTILIZER",  {"SUPER_FERTILIZER","SF1","Phan Bon Cao Cap","",50000, 0.40}},
};

const std::map<std::string, CropDef>& get_crops() { return g_crops; }
const std::map<std::string, ShopItemDef>& get_shop_items() { return g_shop_items; }

const CropDef* find_crop_by_key(const std::string& key) {
    auto it = g_crops.find(key);
    return it == g_crops.end() ? nullptr : &it->second;
}
const CropDef* find_crop_by_code(const std::string& code) {
    for (auto& kv : g_crops) if (kv.second.code == code) return &kv.second;
    return nullptr;
}
const ShopItemDef* find_shop_item_by_code(const std::string& code) {
    for (auto& kv : g_shop_items) if (kv.second.code == code) return &kv.second;
    return nullptr;
}

static long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static std::string fmt_seconds(long long sec) {
    if (sec <= 0) return "0s";
    long long h = sec / 3600; sec %= 3600;
    long long m = sec / 60;   sec %= 60;
    std::ostringstream os;
    if (h > 0) os << h << "h ";
    if (m > 0) os << m << "m ";
    if (sec > 0 || (h == 0 && m == 0)) os << sec << "s";
    return os.str();
}

NongTraiManager::NongTraiManager(EconomyManager& econ) : econ_(econ) {}

Farm& NongTraiManager::get_or_create(const std::string& user_id) {
    auto it = farms_.find(user_id);
    if (it != farms_.end()) return it->second;

    Farm f;
    for (int i = 0; i < 4; ++i) {
        Plot p;
        p.water_level = 100;
        p.last_watered_at = now_ms();
        p.status = "good";
        f.plots.push_back(std::move(p));
    }
    auto res = farms_.emplace(user_id, std::move(f));
    dirty_ = true;
    return res.first->second;
}

void NongTraiManager::refresh_soil(Farm& farm) {
    long long now = now_ms();
    const long long ONE_HOUR = 3600LL * 1000;
    for (auto& p : farm.plots) {
        long long elapsed = now - p.last_watered_at;
        int hours = (int)(elapsed / ONE_HOUR);
        int new_lv = std::max(0, 100 - hours * 10);
        std::string new_status = (new_lv <= 0) ? "dry" : (new_lv <= 40 ? "low" : "good");
        if (p.water_level != new_lv || p.status != new_status) {
            p.water_level = new_lv;
            p.status = new_status;
            dirty_ = true;
        }
    }
}

std::string NongTraiManager::format_status_text(const std::string& user_id) {
    auto& farm = get_or_create(user_id);
    refresh_soil(farm);

    std::ostringstream os;
    os << "NONG TRAI CUA BAN\n\n";
    long long now = now_ms();
    for (size_t i = 0; i < farm.plots.size(); ++i) {
        auto& p = farm.plots[i];
        os << "O " << (i + 1) << ": ";
        if (p.crop_key.empty()) {
            os << "Chua trong cay\n";
        } else {
            const CropDef* cd = find_crop_by_key(p.crop_key);
            if (cd) {
                long long elapsed = now - p.planted_at;
                long long total = (long long)cd->growth_time_sec * 1000;
                long long left = std::max(0LL, total - elapsed);
                os << "Dang trong " << cd->name << " : ";
                if (left == 0) os << "CO THE THU HOACH\n";
                else os << "Con " << fmt_seconds(left / 1000) << "\n";
            }
        }
        if (!p.fertilized.empty()) {
            os << "  Da bon: ";
            for (size_t k = 0; k < p.fertilized.size(); ++k) {
                if (k) os << ", ";
                os << p.fertilized[k];
            }
            os << "\n";
        }
        os << "  Do am: " << p.water_level << "% (";
        if (p.status == "dry") os << "Kho can";
        else if (p.status == "low") os << "Thieu nuoc";
        else os << "Dat tot";
        os << ")\n\n";
    }
    return os.str();
}

long long NongTraiManager::calculate_land_price(int current_plots) const {
    long long base = 10000000LL;
    if (current_plots < 4) return base;
    long long mult = 1;
    for (int i = 0; i < current_plots - 4; ++i) {
        if (mult > (long long)9e15 / 2) return (long long)9e15;
        mult *= 2;
    }
    if (base > (long long)9e15 / mult) return (long long)9e15;
    return base * mult;
}

int NongTraiManager::bag_count(const std::string& user_id, const std::string& item_code) {
    auto& farm = get_or_create(user_id);
    auto it = farm.bag.find(item_code);
    return it == farm.bag.end() ? 0 : it->second;
}

void NongTraiManager::bag_add(const std::string& user_id, const std::string& item_code, int delta) {
    auto& farm = get_or_create(user_id);
    int& v = farm.bag[item_code];
    v += delta;
    if (v <= 0) farm.bag.erase(item_code);
    dirty_ = true;
}

bool NongTraiManager::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) {
        std::cout << "[Farm] Chua co " << path << ", se tao moi khi co nguoi choi\n";
        return true;
    }
    try {
        json j; f >> j;
        std::lock_guard<std::mutex> lk(mtx_);
        farms_.clear();
        if (j.contains("users") && j["users"].is_object()) {
            for (auto& kv : j["users"].items()) {
                const std::string& uid = kv.key();
                const json& fu = kv.value();
                Farm farm;
                if (fu.contains("bag") && fu["bag"].is_object()) {
                    for (auto& bk : fu["bag"].items()) farm.bag[bk.key()] = bk.value().get<int>();
                }
                if (fu.contains("plots") && fu["plots"].is_array()) {
                    for (auto& pj : fu["plots"]) {
                        Plot p;
                        p.crop_key = pj.value("crop_key", std::string(""));
                        p.planted_at = pj.value("planted_at", 0LL);
                        p.water_level = pj.value("water_level", 100);
                        p.last_watered_at = pj.value("last_watered_at", 0LL);
                        p.status = pj.value("status", std::string("good"));
                        if (pj.contains("fertilized") && pj["fertilized"].is_array())
                            for (auto& s : pj["fertilized"]) p.fertilized.push_back(s.get<std::string>());
                        farm.plots.push_back(std::move(p));
                    }
                }
                farms_[uid] = std::move(farm);
            }
        }
        dirty_ = false;
        std::cout << "[Farm] Loaded " << farms_.size() << " users from " << path << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Farm] Load loi JSON: " << e.what() << "\n";
        return false;
    }
}

bool NongTraiManager::save(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    json j;
    j["users"] = json::object();
    for (auto& kv : farms_) {
        json fj;
        json bag = json::object();
        for (auto& bk : kv.second.bag) bag[bk.first] = bk.second;
        fj["bag"] = bag;
        json arr = json::array();
        for (auto& p : kv.second.plots) {
            json pj;
            pj["crop_key"] = p.crop_key;
            pj["planted_at"] = p.planted_at;
            pj["water_level"] = p.water_level;
            pj["last_watered_at"] = p.last_watered_at;
            pj["status"] = p.status;
            pj["fertilized"] = p.fertilized;
            arr.push_back(std::move(pj));
        }
        fj["plots"] = arr;
        j["users"][kv.first] = std::move(fj);
    }
    std::ofstream f(path);
    if (!f.good()) return false;
    f << j.dump(2);
    dirty_ = false;
    return true;
}