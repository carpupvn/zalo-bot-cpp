#pragma once
#include <string>
#include <map>
#include <functional>
#include <vector>
#include <mutex>
#include <deque>
#include <ctime>
#include "ZaloApi.hpp"
#include "PermissionManager.hpp"
#include "EconomyManager.hpp"
#include "GameManager.hpp"
#include "GroupManager.hpp"
#include "AccountManager.hpp"

struct CachedMessage {
    std::string msg_id;
    std::string cli_msg_id;
    std::string uid_from;
    std::string sender_name;
    std::string content;
    bool is_self = false;
    std::time_t ts = 0;
};

struct QuoteInfo {
    std::string msg_id;
    std::string cli_msg_id;
    std::string uid_from;
    std::string content;
    bool has_quote = false;
};

class CommandHandler {
public:
    CommandHandler(ZaloApi& api, PermissionManager& perm, EconomyManager& econ,
                   GameManager& game, GroupManager& group, AccountManager& account);

    void handle_message(const std::string& user_id, const std::string& thread_id,
                        int thread_type, const std::string& message,
                        const std::string& sender_name,
                        bool is_self = false,
                        const QuoteInfo& quote = QuoteInfo{});

    void cache_message(const std::string& thread_id,
                       const std::string& msg_id,
                       const std::string& cli_msg_id,
                       const std::string& uid_from,
                       const std::string& sender_name,
                       const std::string& content,
                       bool is_self);

    std::string get_prefix() const { return prefix; }
    void set_prefix(const std::string& new_prefix) { prefix = new_prefix; }
    std::string get_bot_name() const { return bot_name_; }

    void cache_user_name(const std::string& uid, const std::string& name);
    std::string format_user(const std::string& uid);

private:
    ZaloApi& api;
    PermissionManager& perm;
    EconomyManager& econ;
    GameManager& game;
    GroupManager& group;
    AccountManager& account;

    std::string prefix = "/";
    std::string bot_name_ = "Zalo Bot";

    std::string current_sender_id_;
    std::string current_sender_name_;

    std::map<std::string, std::string> user_name_cache_;

    std::map<std::string, std::deque<CachedMessage>> thread_msgs_;
    mutable std::mutex msgs_mtx_;
    QuoteInfo current_quote_;
    mutable std::mutex cache_mtx_;

    using CommandFunc = std::function<void(const std::vector<std::string>&,
                                           const std::string&, const std::string&,
                                           int, bool)>;
    std::map<std::string, CommandFunc> commands;

    void register_commands();
    void load_bot_info();
    void load_user_economy_impl(const std::string& uid, const std::string& username);
    void save_user_economy_impl(const std::string& uid);
    std::vector<std::string> parse_command(const std::string& message);

    // Reply dạng text (mặc định)
    void reply_text(const std::string& msg, const std::string& thread_id, int thread_type);

    // Reply dạng ảnh (chỉ dùng cho lệnh được thiết kế hiển thị dài)
    void reply_image(const std::string& title, const std::string& msg,
                     const std::string& thread_id, int thread_type);
};
