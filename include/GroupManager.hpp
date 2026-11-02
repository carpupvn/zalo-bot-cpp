#pragma once
#include <string>
#include <map>
#include <set>
#include <vector>
#include <chrono>
#include <regex>
#include <utility>
#include "ZaloApi.hpp"

class GroupManager {
public:
    GroupManager(ZaloApi& api);

    void set_own_id(const std::string& own_id) { own_id_ = own_id; }

    bool is_bot_admin_in_group(const std::string& group_id);
    void invalidate_admin_cache(const std::string& group_id);

    void on_member_join(const std::string& group_id, const std::string& user_id,
                        const std::string& user_name);
    void on_member_leave(const std::string& group_id, const std::string& user_id,
                         const std::string& user_name);

    // Trả về true nếu user vượt ngưỡng SPAM_THRESHOLD
    bool check_spam(const std::string& user_id, const std::string& thread_id);

    // Đếm số lần spam trong window
    int get_spam_count(const std::string& user_id);
    void reset_spam_count(const std::string& user_id);

    bool contains_bad_words(const std::string& message);
    std::string find_bad_word(const std::string& message);

    bool contains_link(const std::string& message);
    void set_anti_link_enabled(const std::string& group_id, bool enabled);
    bool is_anti_link_enabled(const std::string& group_id) const;

    void set_welcome_enabled(const std::string& group_id, bool enabled);
    void set_goodbye_enabled(const std::string& group_id, bool enabled);
    void set_anti_spam_enabled(const std::string& group_id, bool enabled);
    void set_bad_word_filter_enabled(const std::string& group_id, bool enabled);
    void set_anti_undo_enabled(const std::string& group_id, bool enabled);

    bool is_welcome_enabled(const std::string& group_id) const;
    bool is_goodbye_enabled(const std::string& group_id) const;
    bool is_anti_spam_enabled(const std::string& group_id) const;
    bool is_bad_word_filter_enabled(const std::string& group_id) const;
    bool is_anti_undo_enabled(const std::string& group_id) const;

    // Bật/tắt đồng loạt tất cả bảo vệ (spam + badword + link + antiundo)
    void set_all_protect(const std::string& group_id, bool enabled);

    // Cache lock chat / duyệt thành viên
    void set_lock_cache(const std::string& g, bool v)    { lock_send_cache_[g] = v; }
    void set_approve_cache(const std::string& g, bool v) { join_approval_cache_[g] = v; }
    int  get_lock_cache(const std::string& g) const {
        auto it = lock_send_cache_.find(g);
        return (it == lock_send_cache_.end()) ? -1 : (it->second ? 1 : 0);
    }
    int  get_approve_cache(const std::string& g) const {
        auto it = join_approval_cache_.find(g);
        return (it == join_approval_cache_.end()) ? -1 : (it->second ? 1 : 0);
    }

    void kick_user(const std::string& group_id, const std::string& user_id);
    void warn_user(const std::string& group_id, const std::string& user_id);
    void mute_user(const std::string& group_id, const std::string& user_id, int minutes);
    void unmute_user(const std::string& group_id, const std::string& user_id);

    // Đếm tin nhắn để xếp hạng
    void increment_message(const std::string& thread_id, const std::string& user_id);
    std::vector<std::pair<std::string,int>> get_top(const std::string& thread_id, int n);

    // Ngưỡng spam
    static const int SPAM_THRESHOLD = 5;
    static const int SPAM_KICK_THRESHOLD = 5;

private:
    ZaloApi& api;
    std::string own_id_;

    std::map<std::string, bool> welcome_enabled;
    std::map<std::string, bool> goodbye_enabled;
    std::map<std::string, bool> anti_spam_enabled;
    std::map<std::string, bool> bad_word_filter_enabled;
    std::map<std::string, bool> anti_link_enabled;
    std::map<std::string, bool> anti_undo_enabled;
    std::map<std::string, bool> lock_send_cache_;
    std::map<std::string, bool> join_approval_cache_;

    std::map<std::string, bool> bot_admin_cache_;

    std::map<std::string, std::vector<std::chrono::steady_clock::time_point>> message_times;
    std::map<std::string, std::chrono::steady_clock::time_point> muted_users;

    // Đếm số lần spam liên tiếp (reset khi user dừng spam 30s)
    std::map<std::string, int> spam_count_;
    std::map<std::string, std::chrono::steady_clock::time_point> last_spam_time_;

    // thread_id -> user_id -> số tin nhắn
    std::map<std::string, std::map<std::string,int>> message_count_;

    std::set<std::string> bad_words = {
        "dm", "dmm", "dcm", "đm", "đmm", "đcm", "vl", "vcl",
        "cmm", "clm", "đéo", "địt", "lồn", "cặc", "buồi"
    };

    const int SPAM_WINDOW_SECONDS = 10;
    const int SPAM_COUNT_RESET_SECONDS = 30;
};