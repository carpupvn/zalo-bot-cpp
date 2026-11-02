#include "PermissionManager.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

void PermissionManager::set_user_role(const std::string& user_id, Role role) {
    user_roles[user_id] = role;
    if (!file_path_.empty()) save_to_file(file_path_);
}

Role PermissionManager::get_user_role(const std::string& user_id) {
    auto it = user_roles.find(user_id);
    if (it != user_roles.end()) return it->second;
    return Role::MEMBER;
}

bool PermissionManager::is_admin(const std::string& user_id) {
    return get_user_role(user_id) >= Role::BOT_ADMIN;
}

bool PermissionManager::is_super_admin(const std::string& user_id) {
    return get_user_role(user_id) >= Role::SUPER_ADMIN;
}

bool PermissionManager::is_host(const std::string& user_id) {
    return get_user_role(user_id) == Role::HOST;
}

bool PermissionManager::has_permission(const std::string& user_id,
                                       const std::string& command) {
    Role role = get_user_role(user_id);

    // HOST only
    if (command == "exit") {
        return role == Role::HOST;
    }

    // SUPER_ADMIN+
    if (command == "setad" || command == "setprefix") {
        return role >= Role::SUPER_ADMIN;
    }

    // BOT_ADMIN+
    if (command == "setmoney" || command == "sethu" || command == "buff" ||
        command == "nerf" || command == "on" || command == "off" ||
        command == "kick" || command == "warn" || command == "mute" ||
        command == "unmute" || command == "welcome" || command == "goodbye" ||
        command == "antispam" || command == "badword" || command == "listadmin" ||
        command == "antilink" || command == "antiundo" || command == "groupprotect") {
        return role >= Role::BOT_ADMIN;
    }

    return true;
}

void PermissionManager::set_group_admin(const std::string& group_id,
                                        const std::string& user_id, bool is_admin) {
    if (is_admin) group_admins[group_id].insert(user_id);
    else          group_admins[group_id].erase(user_id);
}

bool PermissionManager::is_group_admin(const std::string& group_id,
                                       const std::string& user_id) {
    auto it = group_admins.find(group_id);
    if (it != group_admins.end()) return it->second.count(user_id) > 0;
    return false;
}

std::vector<std::string> PermissionManager::list_admins() {
    std::vector<std::string> out;
    for (auto& kv : user_roles) {
        if (kv.second >= Role::BOT_ADMIN) {
            std::string tag;
            if (kv.second == Role::HOST)             tag = "HOST";
            else if (kv.second == Role::SUPER_ADMIN) tag = "SUPER_ADMIN";
            else if (kv.second == Role::BOT_ADMIN)   tag = "ADMIN";
            else continue;

            std::string name = get_user_name(kv.first);
            out.push_back(kv.first + "  [" + tag + "]  " + name);
        }
    }
    return out;
}

bool PermissionManager::remove_admin(const std::string& user_id) {
    auto it = user_roles.find(user_id);
    if (it == user_roles.end()) return false;
    if (it->second == Role::HOST) return false;
    user_roles[user_id] = Role::MEMBER;
    if (!file_path_.empty()) save_to_file(file_path_);
    return true;
}

void PermissionManager::set_user_name(const std::string& user_id, const std::string& name) {
    if (user_id.empty() || name.empty()) return;
    user_names[user_id] = name;
    // Chỉ đánh dấu dirty — ghi file do flush_if_dirty() xử lý
    if (!file_path_.empty() && is_admin(user_id)) dirty_ = true;
}

void PermissionManager::flush_if_dirty() {
    if (dirty_ && !file_path_.empty()) {
        save_to_file(file_path_);
        dirty_ = false;
    }
}

std::string PermissionManager::get_user_name(const std::string& user_id) {
    auto it = user_names.find(user_id);
    if (it != user_names.end()) return it->second;
    return "";
}

// ============================================================
// Load từ file
// Format: uid|role|display_name
// ============================================================
bool PermissionManager::load_from_file(const std::string& path) {
    file_path_ = path;

    std::ifstream f(path);
    if (!f.good()) {
        std::cout << "[Perm] Chưa có " << path << ", sẽ tạo mới khi có admin\n";
        return true;  // OK, file chưa có
    }

    std::string line;
    int count = 0;
    while (std::getline(f, line)) {
        // Bỏ comment và dòng trống
        if (line.empty() || line[0] == '#') continue;

        // Trim
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // Parse: uid|role|name
        std::stringstream ss(line);
        std::string uid, role_str, name;

        if (!std::getline(ss, uid, '|')) continue;
        if (!std::getline(ss, role_str, '|')) continue;
        std::getline(ss, name);  // có thể rỗng

        Role r;
        if (role_str == "HOST")             r = Role::HOST;
        else if (role_str == "SUPER_ADMIN") r = Role::SUPER_ADMIN;
        else if (role_str == "BOT_ADMIN")   r = Role::BOT_ADMIN;
        else continue;

        user_roles[uid] = r;
        if (!name.empty()) user_names[uid] = name;
        count++;
    }

    std::cout << "[Perm] Loaded " << count << " admins từ " << path << "\n";
    return true;
}

// ============================================================
// Save ra file
// ============================================================
bool PermissionManager::save_to_file(const std::string& path) const {
    std::ofstream f(path);
    if (!f.good()) {
        std::cerr << "[Perm] Không ghi được " << path << "\n";
        return false;
    }

    f << "# Danh sach admin\n";
    f << "# Format: uid|role|display_name\n";
    f << "# role: HOST | SUPER_ADMIN | BOT_ADMIN\n";
    f << "# File nay khong bi xoa khi build\n\n";

    for (const auto& kv : user_roles) {
        if (kv.second < Role::BOT_ADMIN) continue;

        std::string tag;
        if (kv.second == Role::HOST)             tag = "HOST";
        else if (kv.second == Role::SUPER_ADMIN) tag = "SUPER_ADMIN";
        else if (kv.second == Role::BOT_ADMIN)   tag = "BOT_ADMIN";
        else continue;

        auto it_name = user_names.find(kv.first);
        std::string name = (it_name != user_names.end()) ? it_name->second : "";

        f << kv.first << "|" << tag << "|" << name << "\n";
    }
    return true;
}