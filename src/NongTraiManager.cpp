#include "NongTraiManager.hpp"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <set>

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
// ============================================================
// BƯỚC 3: GAMEPLAY
// ============================================================

static std::string fmt_money(long long v) {
    bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-v) : (unsigned long long)v;
    std::string s = std::to_string(u);
    int n = (int)s.size();
    for (int i = n - 3; i > 0; i -= 3) s.insert(i, ".");
    return (neg ? "-" : "") + s;
}

struct ProductInfo {
    std::string type;  // CROP | PRODUCT | ITEM
    std::string key;
    std::string code;
    const CropDef* crop = nullptr;
    const ShopItemDef* item = nullptr;
    bool valid = false;
};

static ProductInfo resolve_product_code(const std::string& raw) {
    ProductInfo pi;
    std::string code = raw;
    for (auto& c : code) c = (char)std::toupper((unsigned char)c);
    if (code.size() > 3 && code.substr(code.size() - 3) == "_TP") {
        std::string base = code.substr(0, code.size() - 3);
        const CropDef* cd = find_crop_by_code(base);
        if (cd) { pi.type="PRODUCT"; pi.key=cd->key; pi.code=code; pi.crop=cd; pi.valid=true; }
        return pi;
    }
    const CropDef* cd = find_crop_by_code(code);
    if (cd) { pi.type="CROP"; pi.key=cd->key; pi.code=code; pi.crop=cd; pi.valid=true; return pi; }
    const ShopItemDef* si = find_shop_item_by_code(code);
    if (si) { pi.type="ITEM"; pi.key=si->key; pi.code=code; pi.item=si; pi.valid=true; return pi; }
    return pi;
}

std::vector<int> NongTraiManager::parse_slots(const std::string& s) {
    std::set<int> slots;
    std::istringstream iss(s);
    std::string part;
    while (iss >> part) {
        auto dash = part.find('-');
        if (dash != std::string::npos) {
            try {
                int a = std::stoi(part.substr(0, dash));
                int b = std::stoi(part.substr(dash + 1));
                if (a > b) std::swap(a, b);
                for (int i = a; i <= b; ++i) if (i >= 1) slots.insert(i - 1);
            } catch (...) {}
        } else {
            try { int a = std::stoi(part); if (a >= 1) slots.insert(a - 1); } catch (...) {}
        }
    }
    return std::vector<int>(slots.begin(), slots.end());
}

std::string NongTraiManager::cmd_shop(const std::string& uid) {
    auto& farm = get_or_create(uid);
    std::ostringstream os;
    os << "NONG TRAI SHOP\n\n";
    os << "VAT PHAM:\n";
    for (auto& kv : g_shop_items) {
        const auto& it = kv.second;
        long long price = (it.key == "LAND") ? calculate_land_price((int)farm.plots.size()) : it.price;
        os << it.code << " " << it.name << ": " << fmt_money(price) << " VND\n";
    }
    os << "\nHAT GIONG:\n";
    for (auto& kv : g_crops) {
        const auto& c = kv.second;
        os << c.code << " " << c.name << ": " << fmt_money(c.seed_price) << " VND";
        os << " (thu " << fmt_money(c.harvest_value) << " sau " << (c.growth_time_sec/60) << "p)\n";
    }
    os << "\nDung: nongtrai buy <code> [qty]";
    return os.str();
}

std::string NongTraiManager::cmd_buy(const std::string& uid, const std::string& code_in, int qty) {
    if (qty <= 0) qty = 1;
    ProductInfo pi = resolve_product_code(code_in);
    if (!pi.valid) return "Ma san pham khong hop le!";
    if (pi.type == "PRODUCT") return "Khong the mua san pham thu hoach!";

    long long price = 0;
    if (pi.type == "CROP") price = pi.crop->seed_price;
    else if (pi.type == "ITEM") price = (pi.item->key == "LAND") ? calculate_land_price((int)get_or_create(uid).plots.size()) : pi.item->price;

    if (pi.type == "ITEM" && pi.item->key == "LAND" && qty != 1) return "Chi mua 1 manh dat moi lan!";

    long long total = price * qty;
    UserEconomy e = econ_.get_user_economy(uid);
    if (e.bank < total) return "Khong du tien! Can " + fmt_money(total) + " VND";

    econ_.set_full_data(uid, e.wallet, e.bank - total, e.debt, e.games_played, e.games_won);

    if (pi.type == "CROP") {
        bag_add(uid, pi.code, qty);
        return "Da mua " + std::to_string(qty) + " hat " + pi.crop->name + " - " + fmt_money(total) + " VND";
    }
    if (pi.type == "ITEM") {
        if (pi.item->key == "LAND") {
            auto& farm = get_or_create(uid);
            Plot p; p.water_level = 100; p.last_watered_at = now_ms(); p.status = "good";
            farm.plots.push_back(std::move(p));
            set_dirty();
            return "Da mua manh dat - " + fmt_money(total) + " VND. So o: " + std::to_string(farm.plots.size());
        }
        bag_add(uid, pi.code, qty);
        return "Da mua " + std::to_string(qty) + " " + pi.item->name + " - " + fmt_money(total) + " VND";
    }
    return "Loi khong xac dinh.";
}

std::string NongTraiManager::cmd_sell(const std::string& uid, const std::string& code_in, int qty) {
    ProductInfo pi = resolve_product_code(code_in);
    if (!pi.valid) return "Ma san pham khong hop le!";
    auto& farm = get_or_create(uid);
    auto bit = farm.bag.find(pi.code);
    int have = (bit == farm.bag.end()) ? 0 : bit->second;
    if (have <= 0) return "Khong co san pham nay!";
    if (qty < 0) qty = have;
    if (qty > have) qty = have;

    long long price_per = 0;
    std::string name;
    if (pi.type == "PRODUCT") { price_per = pi.crop->harvest_value; name = pi.crop->name + " (TP)"; }
    else if (pi.type == "CROP") { price_per = (long long)(pi.crop->seed_price * 0.9); name = "hat " + pi.crop->name; }
    else if (pi.type == "ITEM") { price_per = (long long)(pi.item->price * 0.9); name = pi.item->name; }
    else return "Khong the ban!";

    long long revenue = price_per * qty;
    bag_add(uid, pi.code, -qty);
    UserEconomy e = econ_.get_user_economy(uid);
    econ_.set_full_data(uid, e.wallet, e.bank + revenue, e.debt, e.games_played, e.games_won);
    return "Da ban " + std::to_string(qty) + " " + name + " - " + fmt_money(revenue) + " VND";
}

std::string NongTraiManager::cmd_bag(const std::string& uid) {
    auto& farm = get_or_create(uid);
    std::ostringstream os;
    os << "TUI DO CUA BAN\n\n";
    if (farm.bag.empty()) { os << "Tui do trong!"; return os.str(); }
    std::vector<std::string> seeds, products, items;
    for (auto& kv : farm.bag) {
        ProductInfo pi = resolve_product_code(kv.first);
        if (!pi.valid) continue;
        std::string line = "  " + pi.code + " ";
        if (pi.type == "CROP") line += pi.crop->name + " (hat): " + std::to_string(kv.second);
        else if (pi.type == "PRODUCT") line += pi.crop->name + " (TP): " + std::to_string(kv.second);
        else line += pi.item->name + ": " + std::to_string(kv.second);
        if (pi.type == "CROP") seeds.push_back(line);
        else if (pi.type == "PRODUCT") products.push_back(line);
        else items.push_back(line);
    }
    if (!seeds.empty()) { os << "HAT GIONG:\n"; for (auto& s : seeds) os << s << "\n"; os << "\n"; }
    if (!products.empty()) { os << "THANH PHAM:\n"; for (auto& s : products) os << s << "\n"; os << "\n"; }
    if (!items.empty()) { os << "VAT PHAM:\n"; for (auto& s : items) os << s << "\n"; }
    return os.str();
}

std::string NongTraiManager::cmd_gieohat(const std::string& uid, const std::string& code_in, const std::vector<int>& slots) {
    if (slots.empty()) return "Chon o dat! Vi du: nongtrai gieohat L1 1-4";
    ProductInfo pi = resolve_product_code(code_in);
    if (!pi.valid || pi.type != "CROP") return "Ma hat giong khong hop le!";
    auto& farm = get_or_create(uid);
    auto bit = farm.bag.find(pi.code);
    int have = (bit == farm.bag.end()) ? 0 : bit->second;
    if (have < (int)slots.size()) return "Chi co " + std::to_string(have) + " hat, khong du!";

    int ok = 0, fail = 0;
    for (int idx : slots) {
        if (idx < 0 || idx >= (int)farm.plots.size()) { fail++; continue; }
        Plot& p = farm.plots[idx];
        if (!p.crop_key.empty()) { fail++; continue; }
        if (p.status == "dry" || p.water_level <= 40) { fail++; continue; }
        p.crop_key = pi.key;
        p.planted_at = now_ms();
        p.fertilized.clear();
        bag_add(uid, pi.code, -1);
        ok++;
    }
    set_dirty();
    return "Trong " + std::to_string(ok) + "/" + std::to_string(slots.size()) + " o. Hat con: " + std::to_string(bag_count(uid, pi.code));
}

std::string NongTraiManager::cmd_tuoinuoc(const std::string& uid, const std::vector<int>& slots) {
    if (slots.empty()) return "Chon o dat! Vi du: nongtrai tuoinuoc 1-4";
    auto& farm = get_or_create(uid);
    refresh_soil(farm);
    auto& bag = farm.bag;
    int have = bag.count("W1") ? bag["W1"] : 0;
    if (have < (int)slots.size()) return "Khong du binh nuoc! Co " + std::to_string(have);

    int ok = 0, fail = 0;
    for (int idx : slots) {
        if (idx < 0 || idx >= (int)farm.plots.size()) { fail++; continue; }
        Plot& p = farm.plots[idx];
        if (p.water_level >= 100 || p.status == "dry") { fail++; continue; }
        long long target = std::min(100, p.water_level + 60);
        long long hours_needed = (100 - target) / 10;
        p.last_watered_at = now_ms() - hours_needed * 3600LL * 1000;
        p.water_level = (int)target;
        p.status = (target > 40) ? "good" : "low";
        bag["W1"]--;
        if (bag["W1"] <= 0) bag.erase("W1");
        ok++;
    }
    set_dirty();
    return "Tuoi " + std::to_string(ok) + "/" + std::to_string(slots.size()) + " o. Con: " + std::to_string(bag_count(uid, "W1")) + " W1";
}

std::string NongTraiManager::cmd_cuocdat(const std::string& uid, const std::vector<int>& slots) {
    if (slots.empty()) return "Chon o dat! Vi du: nongtrai cuocdat 1-4";
    auto& farm = get_or_create(uid);
    refresh_soil(farm);
    auto& bag = farm.bag;
    int have = bag.count("S1") ? bag["S1"] : 0;
    if (have < (int)slots.size()) return "Khong du cuoc! Co " + std::to_string(have);

    int ok = 0, fail = 0;
    for (int idx : slots) {
        if (idx < 0 || idx >= (int)farm.plots.size()) { fail++; continue; }
        Plot& p = farm.plots[idx];
        if (p.status != "dry") { fail++; continue; }
        p.water_level = 10;
        p.status = "low";
        p.last_watered_at = now_ms() - 9 * 3600LL * 1000;
        bag["S1"]--;
        if (bag["S1"] <= 0) bag.erase("S1");
        ok++;
    }
    set_dirty();
    return "Cuoc " + std::to_string(ok) + "/" + std::to_string(slots.size()) + " o. Con: " + std::to_string(bag_count(uid, "S1")) + " S1";
}

std::string NongTraiManager::cmd_thuhoach(const std::string& uid, const std::vector<int>& slots) {
    if (slots.empty()) return "Chon o dat! Vi du: nongtrai thuhoach 1-4";
    auto& farm = get_or_create(uid);
    long long now = now_ms();
    int ok = 0, fail = 0;
    std::map<std::string,int> harvested;
    for (int idx : slots) {
        if (idx < 0 || idx >= (int)farm.plots.size()) { fail++; continue; }
        Plot& p = farm.plots[idx];
        if (p.crop_key.empty()) { fail++; continue; }
        const CropDef* cd = find_crop_by_key(p.crop_key);
        if (!cd) { fail++; continue; }
        long long total = (long long)cd->growth_time_sec * 1000;
        if (now - p.planted_at < total) { fail++; continue; }
        std::string product_code = cd->code + "_TP";
        bag_add(uid, product_code, 1);
        harvested[cd->name]++;
        p.crop_key.clear();
        p.planted_at = 0;
        p.fertilized.clear();
        ok++;
    }
    set_dirty();
    std::ostringstream os;
    os << "THU HOACH: " << ok << "/" << slots.size() << " o\n";
    if (!harvested.empty()) {
        os << "San pham:\n";
        for (auto& kv : harvested) os << "  " << kv.first << " x" << kv.second << "\n";
    }
    if (fail > 0) os << "(That bai: " << fail << " o)";
    return os.str();
}

std::string NongTraiManager::cmd_use(const std::string& uid, const std::string& code_in, const std::vector<int>& slots) {
    ProductInfo pi = resolve_product_code(code_in);
    if (!pi.valid) return "Ma khong hop le!";
    if (pi.type == "CROP") return cmd_gieohat(uid, code_in, slots);
    if (pi.type == "ITEM") {
        if (pi.item->key == "WATER") return cmd_tuoinuoc(uid, slots);
        if (pi.item->key == "SHOVEL") return cmd_cuocdat(uid, slots);
        auto& farm = get_or_create(uid);
        auto& bag = farm.bag;
        int have = bag.count(pi.code) ? bag[pi.code] : 0;
        if (have < (int)slots.size()) return "Khong du phan bon!";
        double effect = pi.item->effect;
        int ok = 0, fail = 0;
        long long now = now_ms();
        for (int idx : slots) {
            if (idx < 0 || idx >= (int)farm.plots.size()) { fail++; continue; }
            Plot& p = farm.plots[idx];
            if (p.crop_key.empty()) { fail++; continue; }
            bool already = false;
            for (auto& f : p.fertilized) if (f == pi.item->key) { already = true; break; }
            if (already) { fail++; continue; }
            const CropDef* cd = find_crop_by_key(p.crop_key);
            if (!cd) { fail++; continue; }
            long long total = (long long)cd->growth_time_sec * 1000;
            long long left = total - (now - p.planted_at);
            if (left <= 0) { fail++; continue; }
            long long reduce = (long long)(left * effect);
            p.planted_at -= reduce;
            p.fertilized.push_back(pi.item->key);
            bag[pi.code]--;
            if (bag[pi.code] <= 0) bag.erase(pi.code);
            ok++;
        }
        set_dirty();
        return "Bon " + std::to_string(ok) + "/" + std::to_string(slots.size()) + " o. Con: " + std::to_string(bag_count(uid, pi.code));
    }
    return "Khong the su dung!";
}