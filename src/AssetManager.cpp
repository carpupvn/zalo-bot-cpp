#include "AssetManager.hpp"
#include <iostream>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

std::map<std::string, ImageRGBA> AssetManager::cache_;
std::mutex AssetManager::mtx_;

ImageRGBA* AssetManager::load(const std::string& key, const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);

    auto it = cache_.find(key);
    if (it != cache_.end() && it->second.valid()) {
        return &it->second;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.good()) {
        std::cerr << "[Asset] Khong tim thay: " << path << "\n";
        return nullptr;
    }
    f.close();

    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);

    if (!pixels) {
        std::cerr << "[Asset] stbi_load fail: " << path
                  << " - " << stbi_failure_reason() << "\n";
        return nullptr;
    }

    ImageRGBA img;
    img.width = w;
    img.height = h;
    img.data.assign(pixels, pixels + (w * h * 4));
    stbi_image_free(pixels);

    cache_[key] = std::move(img);
    std::cout << "[Asset] Loaded " << key << " (" << w << "x" << h << ")\n";
    return &cache_[key];
}

ImageRGBA* AssetManager::get(const std::string& key) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = cache_.find(key);
    if (it != cache_.end() && it->second.valid()) return &it->second;
    return nullptr;
}

void AssetManager::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    cache_.clear();
}

void AssetManager::preload_common() {
    std::cout << "[Asset] Preloading...\n";

    // Tai xiu
    for (int i = 1; i <= 6; i++) {
        load("dice_" + std::to_string(i),
             "assets/repo/taixiu/dice_" + std::to_string(i) + ".png");
    }

    // Bau cua
    const char* bau_cua[] = {"bau", "cua", "tom", "ca", "ga", "nai"};
    for (int i = 0; i < 6; i++) {
        load(std::string("bc_") + bau_cua[i],
             std::string("assets/repo/baucua/") + bau_cua[i] + ".png");
    }

    // Keo bua bao
    const char* kbb[] = {"keo", "bua", "bao"};
    for (int i = 0; i < 3; i++) {
        load(std::string("kbb_") + kbb[i] + "_left",
             std::string("assets/repo/keobuabao/") + kbb[i] + "-left.png");
        load(std::string("kbb_") + kbb[i] + "_right",
             std::string("assets/repo/keobuabao/") + kbb[i] + "-right.png");
    }

    std::cout << "[Asset] Preload xong\n";
}
