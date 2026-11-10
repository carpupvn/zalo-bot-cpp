#include "paths.hpp"
namespace paths {
    // g_profile giu lai de tuong thich (chi de debug), KHONG dung cho path
    std::string g_profile;
    
    std::string data(const std::string& rel) { return "data/" + rel; }
    std::string cache(const std::string& rel) { return "cache/" + rel; }
    std::string file(const std::string& rel) { return rel; }
}