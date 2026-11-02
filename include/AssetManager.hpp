#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <mutex>

struct ImageRGBA {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> data;
    bool valid() const { return width > 0 && height > 0 && !data.empty(); }
};

class AssetManager {
public:
    static ImageRGBA* load(const std::string& key, const std::string& path);
    static ImageRGBA* get(const std::string& key);
    static void clear();
    static void preload_common();

private:
    static std::map<std::string, ImageRGBA> cache_;
    static std::mutex mtx_;
};