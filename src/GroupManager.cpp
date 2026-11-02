#include "GroupManager.hpp"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <cctype>

GroupManager::GroupManager(ZaloApi& api) : api(api) {}

bool GroupManager::is_bot_admin_in_group(const std::string& group_id) {
    auto it = bot_admin_cache_.find(group_id);
    if (it != bot_admin_cache_.end()) return it->second;

    if (own_id_.empty()) {
        std::cerr << "[Group] Chưa biết own_id\n";
        return false;
    }

    auto resp = api.get_group_info(group_id);

    if (resp.contains("error")) {
        std::cerr << "[Group] getGroupInfo error: " << resp.dump().substr(0, 200) << "\n";
        bot_admin_cache_[group_id] = false;
        return false;
    }

    // Parse path: result.gridInfoMap.<group_id>
    json info = json::object();
    if (resp.contains("result") && resp["result"].is_object()) {
        auto& r = resp["result"];
        if (r.contains("gridInfoMap") && r["gridInfoMap"].is_object()) {
            auto& gm = r["gridInfoMap"];
            if (gm.contains(group_id) && gm[group_id].is_object()) {
                info = gm[group_id];
            } else if (!gm.empty()) {
                // fallback: lấy entry đầu tiên
                for (auto& [k, v] : gm.items()) {
                    if (v.is_object()) { info = v; break; }
                }
            }
        }
        // fallback nếu là path cũ
        if (info.empty() && r.contains("groupId") && r["groupId"].is_string()) {
            info = r;
        }
    }

    if (info.empty()) {
        std::cerr << "[Group] Không parse được group info cho " << group_id << "\n";
        bot_admin_cache_[group_id] = false;
        return false;
    }

    bool is_admin = false;

    // Check creatorId
    if (info.contains("creatorId") && info["creatorId"].is_string()) {
        if (info["creatorId"].get<std::string>() == own_id_) is_admin = true;
    }

    // Check adminIds
    if (!is_admin && info.contains("adminIds") && info["adminIds"].is_array()) {
        for (const auto& id : info["adminIds"]) {
            if (id.is_string() && id.get<std::string>() == own_id_) {
                is_admin = true; break;
            }
        }
    }

    bot_admin_cache_[group_id] = is_admin;
    std::cout << "[Group] Bot " << (is_admin ? "LÀ" : "KHÔNG phải")
              << " admin của nhóm " << group_id << "\n";
    return is_admin;
}

void GroupManager::invalidate_admin_cache(const std::string& group_id) {
    bot_admin_cache_.erase(group_id);
}

void GroupManager::on_member_join(const std::string& group_id, const std::string& /*user_id*/,
                                  const std::string& user_name) {
    if (!welcome_enabled[group_id]) return;
    api.send_message("👋 Chào mừng " + user_name + " đã tham gia nhóm!", group_id, 1);
}

void GroupManager::on_member_leave(const std::string& group_id, const std::string& /*user_id*/,
                                   const std::string& user_name) {
    if (!goodbye_enabled[group_id]) return;
    api.send_message("👋 Tạm biệt " + user_name + " đã rời nhóm.", group_id, 1);
}

// ============================================================
// Spam check — trả về true khi user vượt ngưỡng
// ============================================================
bool GroupManager::check_spam(const std::string& user_id, const std::string& thread_id) {
    if (!anti_spam_enabled[thread_id]) return false;

    auto now = std::chrono::steady_clock::now();
    auto& times = message_times[user_id];

    // Xóa timestamp cũ ngoài window
    times.erase(std::remove_if(times.begin(), times.end(),
        [&](const auto& t) {
            return std::chrono::duration_cast<std::chrono::seconds>(now - t).count()
                   > SPAM_WINDOW_SECONDS;
        }), times.end());

    times.push_back(now);

    // Reset bộ đếm spam tổng nếu user đã "yên" > 30s
    auto it_last = last_spam_time_.find(user_id);
    if (it_last != last_spam_time_.end()) {
        auto gap = std::chrono::duration_cast<std::chrono::seconds>(
                        now - it_last->second).count();
        if (gap > SPAM_COUNT_RESET_SECONDS) {
            spam_count_[user_id] = 0;
        }
    }
    last_spam_time_[user_id] = now;

    // Nếu vượt ngưỡng window → tăng tổng spam count
    if ((int)times.size() > SPAM_THRESHOLD) {
        spam_count_[user_id]++;
        return true;
    }
    return false;
}

int GroupManager::get_spam_count(const std::string& user_id) {
    auto it = spam_count_.find(user_id);
    return it != spam_count_.end() ? it->second : 0;
}

void GroupManager::reset_spam_count(const std::string& user_id) {
    spam_count_.erase(user_id);
    last_spam_time_.erase(user_id);
    message_times.erase(user_id);
}
bool GroupManager::contains_bad_words(const std::string& message) {
    return !find_bad_word(message).empty();
}

std::string GroupManager::find_bad_word(const std::string& message) {
    std::string lower_msg = message;
    std::transform(lower_msg.begin(), lower_msg.end(), lower_msg.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    auto is_sep = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '.' || c == ',' || c == '!' || c == '?' ||
               c == ';' || c == ':' || c == '"' || c == '\'' ||
               c == '(' || c == ')' || c == '[' || c == ']';
    };

    for (const auto& word : bad_words) {
        size_t pos = 0;
        while ((pos = lower_msg.find(word, pos)) != std::string::npos) {
            bool left_ok  = (pos == 0) || is_sep((unsigned char)lower_msg[pos - 1]);
            bool right_ok = (pos + word.size() >= lower_msg.size())
                         || is_sep((unsigned char)lower_msg[pos + word.size()]);
            if (left_ok && right_ok) return word;
            pos += word.size();
        }
    }
    return "";
}

// ============================================================
// ANTI-LINK
// ============================================================
bool GroupManager::contains_link(const std::string& message) {
    // Bắt: http(s)://... | www.xxx | t.me/xxx | zalo.me/xxx | xxx.com/path
    // LƯU Ý: MinGW libstdc++ không hỗ trợ lookbehind → bỏ (?<!...)
    static const std::regex url_re(
        R"((https?://|www\.)[^\s]+|t\.me/[^\s]+|zalo\.me/[^\s]+|[a-zA-Z0-9\-]+\.(com|net|org|vn|io|me|info|xyz|co|us|uk|edu|gov)(/[^\s]*)?)",
        std::regex::icase
    );
    return std::regex_search(message, url_re);
}

void GroupManager::set_anti_link_enabled(const std::string& group_id, bool enabled) {
    anti_link_enabled[group_id] = enabled;
}

bool GroupManager::is_anti_link_enabled(const std::string& group_id) const {
    auto it = anti_link_enabled.find(group_id);
    return it != anti_link_enabled.end() && it->second;
}

void GroupManager::set_welcome_enabled(const std::string& g, bool e) { welcome_enabled[g] = e; }
void GroupManager::set_goodbye_enabled(const std::string& g, bool e) { goodbye_enabled[g] = e; }
void GroupManager::set_anti_spam_enabled(const std::string& g, bool e) { anti_spam_enabled[g] = e; }
void GroupManager::set_bad_word_filter_enabled(const std::string& g, bool e) { bad_word_filter_enabled[g] = e; }
void GroupManager::set_anti_undo_enabled(const std::string& g, bool e) { anti_undo_enabled[g] = e; }

bool GroupManager::is_welcome_enabled(const std::string& g) const {
    auto it = welcome_enabled.find(g);
    return it != welcome_enabled.end() && it->second;
}
bool GroupManager::is_goodbye_enabled(const std::string& g) const {
    auto it = goodbye_enabled.find(g);
    return it != goodbye_enabled.end() && it->second;
}
bool GroupManager::is_anti_spam_enabled(const std::string& g) const {
    auto it = anti_spam_enabled.find(g);
    return it != anti_spam_enabled.end() && it->second;
}
bool GroupManager::is_bad_word_filter_enabled(const std::string& g) const {
    auto it = bad_word_filter_enabled.find(g);
    return it != bad_word_filter_enabled.end() && it->second;
}
bool GroupManager::is_anti_undo_enabled(const std::string& g) const {
    auto it = anti_undo_enabled.find(g);
    return it != anti_undo_enabled.end() && it->second;
}

void GroupManager::set_all_protect(const std::string& g, bool e) {
    anti_spam_enabled[g]       = e;
    bad_word_filter_enabled[g] = e;
    anti_link_enabled[g]       = e;
    anti_undo_enabled[g]       = e;
}

void GroupManager::kick_user(const std::string& group_id, const std::string& user_id) {
    api.remove_user_from_group(group_id, user_id);
}

void GroupManager::warn_user(const std::string& group_id, const std::string& user_id) {
    api.send_message("⚠️ Cảnh cáo: " + user_id + " vi phạm quy định!", group_id, 1);
}

void GroupManager::mute_user(const std::string& group_id, const std::string& user_id, int minutes) {
    muted_users[user_id] = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);
    api.send_message("🔇 " + user_id + " bị cấm chat " + std::to_string(minutes) + " phút.",
                     group_id, 1);
}

void GroupManager::unmute_user(const std::string& group_id, const std::string& user_id) {
    muted_users.erase(user_id);
    api.send_message("🔊 " + user_id + " được bỏ cấm chat.", group_id, 1);
}
// ============================================================
// Đếm tin nhắn — xếp hạng
// ============================================================
void GroupManager::increment_message(const std::string& thread_id, const std::string& user_id) {
    if (thread_id.empty() || user_id.empty()) return;
    message_count_[thread_id][user_id]++;
}

std::vector<std::pair<std::string,int>> GroupManager::get_top(const std::string& thread_id, int n) {
    auto it = message_count_.find(thread_id);
    if (it == message_count_.end()) return {};
    std::vector<std::pair<std::string,int>> v(it->second.begin(), it->second.end());
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });
    if ((int)v.size() > n) v.resize(n);
    return v;
}