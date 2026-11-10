#include "ZaloApi.hpp"
#include "ZaloBridge.hpp"
#include "CommandHandler.hpp"
#include "PermissionManager.hpp"
#include "EconomyManager.hpp"
#include "GameManager.hpp"
#include "GroupManager.hpp"
#include "AccountManager.hpp"
#include "NongTraiManager.hpp"

extern NongTraiManager* g_farm_mgr;
#include "Database.hpp"
#include "RuntimeState.hpp"
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "EmojiRenderer.hpp"
#include "json.hpp"
#include "paths.hpp"
#include <filesystem>

#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <regex>
#include <vector>
#include <windows.h>

using json = nlohmann::json;

static void setup_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

static int utf16_to_utf8_offset(const std::string& s, int utf16_count) {
    int utf16_seen = 0;
    int i = 0;
    while (i < (int)s.size() && utf16_seen < utf16_count) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80)        { i += 1; utf16_seen += 1; }
        else if ((c & 0xE0) == 0xC0) { i += 2; utf16_seen += 1; }
        else if ((c & 0xF0) == 0xE0) { i += 3; utf16_seen += 1; }
        else if ((c & 0xF8) == 0xF0) { i += 4; utf16_seen += 2; }
        else                  { i += 1; utf16_seen += 1; }
    }
    return i;
}

static std::string extract_uid(const json& m) {
    if (!m.contains("uid")) return "";
    const auto& u = m["uid"];
    if (u.is_string()) return u.get<std::string>();
    if (u.is_object() && u.contains("id") && u["id"].is_string())
        return u["id"].get<std::string>();
    return "";
}

static std::string resolve_mentions(const std::string& content, const json& mentions) {
    std::string result = content; 
    if (!mentions.is_array() || mentions.empty()) return result;

    struct Range { int byte_pos; int byte_len; std::string uid; };
    std::vector<Range> ranges;

    for (const auto& m : mentions) {
        int utf16_pos = m.value("pos", 0);
        int utf16_len = m.value("len", 0);
        std::string uid = extract_uid(m);
        if (uid.empty() || utf16_len <= 0) continue;

        int byte_start = utf16_to_utf8_offset(content, utf16_pos);
        int byte_end   = utf16_to_utf8_offset(content, utf16_pos + utf16_len);
        int byte_len   = byte_end - byte_start;
        
        if (byte_start < 0 || byte_end > (int)content.size() || byte_len <= 0) continue;
        ranges.push_back({byte_start, byte_len, uid});
    }

    std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.byte_pos > b.byte_pos; });

    for (const auto& r : ranges) {
        if (r.byte_pos < 0 || r.byte_pos + r.byte_len > (int)result.size()) continue;
        result = result.substr(0, r.byte_pos) + "<@" + r.uid + ">" + result.substr(r.byte_pos + r.byte_len);
    }
    return result;
}
static void migrate_root_configs(const std::string& profile_dir) {
    namespace fs = std::filesystem;
    struct Pair { const char* src; const char* dst_name; };
    Pair pairs[] = {
        { "bot_info.json",         "bot_info.json" },
        { "data/scold.txt",        "scold.txt" },
        { "data/husks.json",       "husks.json" },
        { "data/nong_trai.json",   "nong_trai.json" },
        { "data/auto_state.json",  "auto_state.json" },
        { "data/dictionary.txt",   "dictionary.txt" },
    };
    for (auto& p : pairs) {
        std::string src = p.src;
        std::string dst = profile_dir + "/" + p.dst_name;
        if (fs::exists(src) && !fs::exists(dst)) {
            try {
                fs::copy_file(src, dst);
                std::cout << "[Migrate] " << src << " -> " << dst << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[Migrate] Fail " << src << ": " << e.what() << "\n";
            }
        }
    }
}
int main() {
    paths::g_profile.clear();
    {
        std::ifstream pf("data/current_profile.txt");
        int prof = 1;
        if (pf.good()) pf >> prof;
        if (prof >= 1 && prof <= 5) paths::g_profile = std::to_string(prof);
        
        // Tạo thư mục nếu chưa có
        std::filesystem::create_directories(paths::data(""));
        std::filesystem::create_directories(paths::cache(""));
        migrate_root_configs(paths::data(""));
    }
    setup_console();
    
    std::cout << "🤖 Zalo Bot C++ khởi động...\n\n";

    if (!TextRenderer::init("assets/BeVietnamPro-Bold.ttf")) {
        std::cerr << "⚠️ Không có font, chế độ ảnh sẽ không hoạt động.\n";
    }

    TextRenderer::init_emoji("assets/TwemojiMozilla.ttf");

    AssetManager::preload_common();
    EmojiRenderer::init("assets/emoji");

    Database db(paths::data("bot.db"));
    if (!db.init()) {
        std::cerr << "❌ DB chính fail\n";
        std::cin.get();
        return 1;
    }

    AccountManager account;
    if (!account.init(paths::data("accounts.db"))) {
        std::cerr << "❌ Account DB fail\n";
    
        std::cin.get();
        return 1;
    }

        // Restore sessions
    try {
        std::ifstream sf("data/sessions.json");
        if (sf.good()) {
            nlohmann::json sj; sf >> sj;
            int cnt = 0;
            for (auto& item : sj.items()) {
                std::string uid = item.key();
                if (item.value().is_string()) {
                    account.restore_session(uid, item.value().get<std::string>());
                    cnt++;
                }
            }
            if (cnt > 0) std::cout << "[Session] Restored " << cnt << " user(s)\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[Session] Load error: " << e.what() << "\n";
    }
ZaloBridge bridge;
    ZaloApi    api;
    api.set_bridge(&bridge);

    PermissionManager perm;
    perm.load_from_file(paths::file("admins.txt"));

    EconomyManager    econ;
    GameManager       game(econ);
    GroupManager      group(api);
    CommandHandler    handler(api, perm, econ, game, group, account);

    NongTraiManager farm(econ);
    farm.load(paths::data("nong_trai.json"));
    g_farm_mgr = &farm;

    bridge.set_event_handler([&](const json& ev) {
        
        
        try {
        std::string event = ev.value("event", "");

        if (event == "login_ok") {
            std::string own_id = ev.value("own_id", "");
            api.set_own_id(own_id);

            // Tự động nhận diện tài khoản đang đăng nhập làm HOST
            // (Ghi đè HOST cũ nếu có)
            perm.set_user_role(own_id, Role::HOST);
            group.set_own_id(own_id);

            std::cout << "✅ Bot own_id: " << own_id << "\n";
            std::cout << "👑 Đã set " << own_id << " làm HOST\n";
        }
        else if (event == "closed") {
            std::cout << "⚠️ WebSocket đóng: " << ev.value("code", 0)
                      << " " << ev.value("reason", "") << "\n";
        }
        else if (event == "message") {
            try {
                const auto& msg = ev["data"];
                int type = msg.value("type", 0);
                const auto& data = msg["data"];

                std::string content;
                if (data.contains("content") && data["content"].is_string()) {
                    content = data["content"].get<std::string>();
                }

                std::string thread_id = msg.value("threadId", "");
                if (thread_id.empty()) thread_id = data.value("threadId", "");
                if (thread_id.empty()) thread_id = data.value("conversationId", "");
                if (thread_id.empty()) thread_id = msg.value("conversationId", "");
                if (thread_id.empty()) thread_id = data.value("idTo", "");
                if (thread_id.empty()) thread_id = msg.value("idTo", "");

                std::string sender_id   = data.value("uidFrom", "");
                std::string sender_name = data.value("dName", "");
                bool        is_self     = msg.value("isSelf", false);

                // Cache msg + extract quote
                {
                    std::string _mid = data.value("msgId", "");
                    std::string _cmid = data.value("cliMsgId", "");
                    if (!_mid.empty()) {
                        handler.cache_message(thread_id, _mid, _cmid, sender_id, sender_name, content, is_self);
                    }
                }

                QuoteInfo quote;
                if (data.contains("quote") && data["quote"].is_object()) {
                    auto& q = data["quote"];
                    quote.msg_id = q.value("globalMsgId", "");
                    if (quote.msg_id.empty()) quote.msg_id = q.value("msgId", "");
                    quote.cli_msg_id = q.value("cliMsgId", "");
                    quote.uid_from = q.value("ownerId", "");
                    if (quote.uid_from.empty()) quote.uid_from = q.value("uidFrom", "");
                    if (q.contains("msg") && q["msg"].is_string())
                        quote.content = q["msg"].get<std::string>();
                    else if (q.contains("content") && q["content"].is_string())
                        quote.content = q["content"].get<std::string>();
                    quote.has_quote = !quote.msg_id.empty();
                }

                if (content.empty()) return;

                // Đếm tin nhắn để xếp hạng (chỉ tính tin từ người khác)
                if (!is_self && !sender_id.empty() && !thread_id.empty()) {
                    group.increment_message(thread_id, sender_id);
                }

                if (is_self) {
                    std::string marker = "[Bot " + handler.get_bot_name() + " TRẢ LỜI";
                    if (content.find(marker) != std::string::npos) return;
                    if (content.find("[Bot ") == 0) return;
                    std::cout << "[SELF] " << sender_name << ": " << content << "\n";
                } else {
                    std::cout << "[MSG] " << sender_name << ": " << content << "\n";
                }

                json mentions = json::array();
                if (data.contains("mentions")) mentions = data["mentions"];
                std::string content_resolved = resolve_mentions(content, mentions);

                if (content_resolved != content) {
                    std::cout << "        [resolved] " << content_resolved << "\n";
                }

                if (mentions.is_array()) {
                    for (const auto& m : mentions) {
                        std::string m_uid = extract_uid(m);
                        int m_pos = m.value("pos", 0);
                        int m_len = m.value("len", 0);
                        if (m_uid.empty() || m_len <= 0) continue;

                        int byte_start = utf16_to_utf8_offset(content, m_pos);
                        int byte_end   = utf16_to_utf8_offset(content, m_pos + m_len);
                        if (byte_end > (int)content.size()) continue;
                        if (byte_start > byte_end) continue;

                        std::string raw_mention = content.substr(byte_start, byte_end - byte_start);
                        std::string name = raw_mention;
                        if (!name.empty() && name[0] == '@') name = name.substr(1);
                        if (name.empty()) continue;

                        handler.cache_user_name(m_uid, name);
                        perm.set_user_name(m_uid, name);
                        std::cout << "[main] Cache user: " << m_uid
                                  << " → " << name << "\n";
                    }
                }

                std::string cur_prefix = handler.get_prefix();
                bool is_bot_self = (sender_id == api.get_own_id());

                if (type == 1 && thread_id != sender_id) {
                    std::string login_cmd  = cur_prefix + "login";
                    std::string signup_cmd = cur_prefix + "signup";

                    bool is_auth =
                        (content_resolved == login_cmd) ||
                        (content_resolved == signup_cmd) ||
                        (content_resolved.rfind(login_cmd  + " ", 0) == 0) ||
                        (content_resolved.rfind(signup_cmd + " ", 0) == 0);

                    if (is_auth) {
                        std::string msg_id     = data.value("msgId", "");
                        std::string cli_msg_id = data.value("cliMsgId", "");

                        bool removed = false;
                        std::string action_done;

                        if (!msg_id.empty() && !cli_msg_id.empty()) {
                            if (is_bot_self) {
                                auto r = api.undo_message(msg_id, cli_msg_id, thread_id, type);
                                if (r.value("ok", false)) {
                                    removed = true;
                                    action_done = "thu hồi";
                                }
                            } else {
                                auto r = api.delete_message(msg_id, cli_msg_id,
                                                            sender_id, thread_id, type);
                                if (r.value("ok", false)) {
                                    removed = true;
                                    action_done = "xóa";
                                }
                            }
                        }

                        if (!is_bot_self) {
                            std::string warn =
                                "[Bot " + handler.get_bot_name() + "]\n"
                                "⚠️ Không được dùng lệnh đăng nhập/đăng ký trong nhóm!\n"
                                "→ Hãy ib riêng cho bot.\n";

                            if (removed) warn += "🗑 Đã " + action_done + " tin nhắn của bạn.";
                            else         warn += "❌ Hãy thu hồi tin nhắn của bạn.";

                            api.send_message(warn, thread_id, type);
                        }
                        return;
                    }
                }

                bool is_command = (content_resolved.size() >= cur_prefix.size() &&
                                   content_resolved.substr(0, cur_prefix.size()) == cur_prefix);
                bool is_prefix_change = (content_resolved.size() >= 7 &&
                                         (content_resolved.substr(0, 7) == "Prefix " ||
                                          content_resolved.substr(0, 7) == "prefix "));

                if (!is_command && !is_prefix_change && type == 1) {
                    // Chuyển tin nhắn cho game xử lý (đã bỏ điều kiện !is_self để dễ test)
                    std::cout << "[main] Non-command message -> game chat: " << content_resolved << "\n";
                    handler.handle_game_chat(thread_id, type, content_resolved, sender_id, sender_name);
                }

                if (is_command || is_prefix_change) {
                    std::string msg_id     = data.value("msgId", "");
                    std::string cli_msg_id = data.value("cliMsgId", "");

                    if (!msg_id.empty() && !cli_msg_id.empty()) {
                        api.add_reaction_async("HEART", msg_id, cli_msg_id, thread_id, type);
                        std::cout << "[main] Thả tim vào tin lệnh " << msg_id << "\n";
                    }
                }

    // Gate bot/game duoc xu ly trong CommandHandler::handle_message (per-thread)

                // CHỐNG SPAM
                if (group.check_spam(sender_id, thread_id)) {
                    std::string msg_id     = data.value("msgId", "");
                    std::string cli_msg_id = data.value("cliMsgId", "");

                    bool deleted = false;
                    if (!msg_id.empty() && !cli_msg_id.empty()) {
                        auto r = api.delete_message(msg_id, cli_msg_id,
                                                    sender_id, thread_id, type);
                        if (r.value("ok", false)) deleted = true;
                    }

                    int spam_count = group.get_spam_count(sender_id);

                    if (spam_count >= GroupManager::SPAM_KICK_THRESHOLD) {
                        std::string reason =
                            "[Bot " + handler.get_bot_name() + "]\n"
                            "🚫 " + sender_name + " đã spam " +
                            std::to_string(spam_count) + " lần.\n"
                            "→ Bị KICK khỏi nhóm.";

                        api.send_message(reason, thread_id, type);
                        auto k = api.remove_user_from_group(thread_id, sender_id);

                        if (!k.value("ok", false)) {
                            std::cerr << "[main] Kick fail: "
                                      << k.dump().substr(0, 200) << "\n";
                        } else {
                            std::cout << "[main] Đã kick " << sender_name
                                      << " (" << spam_count << " lần spam)\n";
                        }

                        group.reset_spam_count(sender_id);
                        return;
                    }

                    std::string warn =
                        "[Bot " + handler.get_bot_name() + "]\n"
                        "⚠️ " + sender_name + " spam quá nhanh!\n"
                        "📊 Cảnh cáo " + std::to_string(spam_count) + "/" +
                        std::to_string(GroupManager::SPAM_KICK_THRESHOLD) + " lần.\n";

                    if (deleted) warn += "🗑 Đã xóa tin nhắn.";
                    else         warn += "❌ Chưa được cấp quyền xóa tin nhắn.";

                    api.send_message(warn, thread_id, type);
                    return;
                }

                // LỌC TỪ BẬY
                if (group.is_bad_word_filter_enabled(thread_id)) {
                    std::string bad = group.find_bad_word(content_resolved);
                    if (!bad.empty()) {
                        std::string msg_id     = data.value("msgId", "");
                        std::string cli_msg_id = data.value("cliMsgId", "");

                        bool deleted = false;
                        if (!msg_id.empty() && !cli_msg_id.empty()) {
                            auto r = api.delete_message(msg_id, cli_msg_id,
                                                        sender_id, thread_id, type);
                            if (r.value("ok", false)) deleted = true;
                        }

                        std::string warn =
                            "[Bot " + handler.get_bot_name() + "]\n"
                            "⚠️ Phát hiện từ ngữ không phù hợp!\n";

                        if (deleted) warn += "🗑 Đã xóa tin nhắn của " + sender_name + ".";
                        else         warn += "❌ Chưa được cấp quyền xóa tin nhắn.";

                        api.send_message(warn, thread_id, type);
                        return;
                    }
                }

                // CHỐNG LINK
                if (group.is_anti_link_enabled(thread_id)) {
                    if (group.contains_link(content_resolved)) {
                        std::string msg_id     = data.value("msgId", "");
                        std::string cli_msg_id = data.value("cliMsgId", "");

                        bool deleted = false;
                        if (!msg_id.empty() && !cli_msg_id.empty()) {
                            auto r = api.delete_message(msg_id, cli_msg_id,
                                                        sender_id, thread_id, type);
                            if (r.value("ok", false)) deleted = true;
                        }

                        std::string warn =
                            "[Bot " + handler.get_bot_name() + "]\n"
                            "⚠️ Phát hiện link trong tin nhắn!\n";

                        if (deleted) warn += "🗑 Đã xóa tin nhắn của " + sender_name + ".";
                        else         warn += "❌ Chưa được cấp quyền xóa tin nhắn.";

                        api.send_message(warn, thread_id, type);
                        return;
                    }
                }

                handler.handle_message(sender_id, thread_id, type,
                                       content_resolved, sender_name, is_self, quote);
            } catch (const std::exception& e) {
                std::cerr << "[main] Event error: " << e.what() << "\n";
            }
        }
        else if (event == "undo") {
            try {
                const auto& d = ev["data"];
                std::string thread_id = d.value("threadId", "");
                std::string uid_from  = d.value("uidFrom", "");
                std::string content   = d.value("originalContent", "");
                bool        is_group  = d.value("isGroup", false);

                if (thread_id.empty()) return;
                if (!is_group) return;                        // chỉ group
                if (!group.is_anti_undo_enabled(thread_id)) return;
                if (content.empty()) {
                    std::cout << "[main] undo event: uid=" << uid_from
                              << " thread=" << thread_id
                              << " (no cache)\n";
                    return;
                }

                std::string name = handler.format_user(uid_from);
                std::string warn =
                    "[Bot " + handler.get_bot_name() + "]\n"
                    "👀 " + name + " vừa thu hồi tin nhắn:\n"
                    "```\n" + content + "\n```";

                api.send_message_async(warn, thread_id, 1);
                std::cout << "[main] Anti-undo sent: " << uid_from
                          << " in " << thread_id << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[main] undo error: " << e.what() << "\n";
            }
        }
        } catch (const std::exception& e) {
            std::cerr << "[EXCEPTION] " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[EXCEPTION] Unknown error\n";
        }
        
    });

    std::cout << "🔌 Khởi động Node bridge...\n";
    {
        std::ifstream pf("data/current_profile.txt");
        int prof = 1;
        if (pf.good()) pf >> prof;
        if (prof < 1 || prof > 5) prof = 1;
        std::string cookie_file = "data/cookies/" + std::to_string(prof) + ".json";
        _putenv_s("BOT_COOKIE_FILE", cookie_file.c_str());
        std::cout << "[Profile] Using " << cookie_file << "\n";
    }
    if (!bridge.start("node", "bridge.js")) {
        std::cerr << "❌ Không chạy được Node.\n";
        std::cin.get();
        return 1;
    }

    std::cout << "\n🤖 Bot đang chạy.\n";
    std::cout << "   Trạng thái: BẬT\n";
    std::cout << "   Gõ 'Prefix <dấu>' để đổi prefix (HOST)\n";
    std::cout << "   Gõ '" << handler.get_prefix() << "exit' để thoát (HOST)\n\n";

    std::thread stdin_thread([]() {
        std::cin.get();
        g_running = false;
    });
    stdin_thread.detach();

    auto last_flush = std::chrono::steady_clock::now();
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        static auto last_tick = std::chrono::steady_clock::now();
        auto tick_now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(tick_now - last_tick).count() >= 1) {
            handler.tick_taixiu();
            last_tick = tick_now;
        }

        // Flush admins.txt moi 30s
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            now - last_flush).count();
        if (elapsed >= 30) {
            if (g_farm_mgr && g_farm_mgr->is_dirty()) { g_farm_mgr->save("data/nong_trai.json"); }
    perm.flush_if_dirty();
            last_flush = now;
        }

        // Save economy khi co command lien quan tien
        if (g_econ_dirty) {
            g_econ_dirty = false;
            auto logged = account.get_logged_in_map();
            for (auto& kv : logged) {
                const std::string& uid = kv.first;
                const std::string& un  = kv.second;
                UserEconomy e = econ.get_user_economy(uid);
                AccountEconomyData d;
                d.wallet       = e.wallet;
                d.bank         = e.bank;
                d.debt         = e.debt;
                d.games_played = e.games_played;
                d.games_won    = e.games_won;
                account.save_economy(un, d);
            }
            std::cout << "[main] Save economy (dirty): " << logged.size() << " user\n";
        }
    }

    // Flush lan cuoi khi thoat
    if (g_farm_mgr && g_farm_mgr->is_dirty()) { g_farm_mgr->save("data/nong_trai.json"); }
    perm.flush_if_dirty();

    // Flush economy lan cuoi
    {
        auto logged = account.get_logged_in_map();
        for (auto& kv : logged) {
            const std::string& uid = kv.first;
            const std::string& un  = kv.second;
            UserEconomy e = econ.get_user_economy(uid);
            AccountEconomyData d;
            d.wallet       = e.wallet;
            d.bank         = e.bank;
            d.debt         = e.debt;
            d.games_played = e.games_played;
            d.games_won    = e.games_won;
            account.save_economy(un, d);
        }
        account.export_to_txt(paths::data("accounts.txt"));
        std::cout << "[main] Final save: " << logged.size() << " user\n";
    }

    std::cout << "\n⏹ Đang dừng bot...\n";
    bridge.stop();
    std::cout << "👋 Đã thoát.\n";
    return 0;
}