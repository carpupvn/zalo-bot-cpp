#pragma once
#include <string>
#include <map>
#include <set>
#include <vector>

enum class Role {
    MEMBER      = 0,
    GROUP_ADMIN = 1,
    BOT_ADMIN   = 2,
    SUPER_ADMIN = 3,
    HOST        = 4
};

class PermissionManager {
public:
    void set_user_role(const std::string& user_id, Role role);
    Role get_user_role(const std::string& user_id);

    bool is_admin(const std::string& user_id);         // BOT_ADMIN+
    bool is_super_admin(const std::string& user_id);   // SUPER_ADMIN+
    bool is_host(const std::string& user_id);

    bool has_permission(const std::string& user_id, const std::string& command);

    void set_group_admin(const std::string& group_id, const std::string& user_id, bool is_admin);
    bool is_group_admin(const std::string& group_id, const std::string& user_id);

    std::vector<std::string> list_admins();
    bool remove_admin(const std::string& user_id);

    // Cache tên hiển thị
    void set_user_name(const std::string& user_id, const std::string& name);
    std::string get_user_name(const std::string& user_id);

    // Load/save file
    bool load_from_file(const std::string& path);
    bool save_to_file(const std::string& path) const;
    void set_file_path(const std::string& path) { file_path_ = path; }

    // Dirty-flag: chỉ ghi file khi cần (gọi định kỳ trong main loop)
    void flush_if_dirty();
    bool is_dirty() const { return dirty_; }

private:
    std::map<std::string, Role> user_roles;
    std::map<std::string, std::string> user_names;   // uid → display name
    std::map<std::string, std::set<std::string>> group_admins;

    std::string file_path_;
    mutable bool dirty_ = false;
};