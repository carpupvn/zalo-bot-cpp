#pragma once
#include <string>
namespace paths {
    extern std::string g_profile;
    std::string data(const std::string& rel);
    std::string cache(const std::string& rel);
    std::string file(const std::string& rel);
}