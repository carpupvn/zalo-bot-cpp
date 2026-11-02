#include "ZaloApi.hpp"
#include <iostream>
#include <thread>
#include <cstdlib>
#include <ctime>

ZaloApi::ZaloApi() {
    http_client.set_timeout(30000);
}

// ============================================================
// Rate-limit: đảm bảo ít nhất 300ms giữa 2 lần gửi
// ============================================================
void ZaloApi::throttle() {
    std::lock_guard<std::mutex> lk(send_mtx_);
    auto now = std::chrono::steady_clock::now();
    auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now - last_send_).count();
    if (gap < 300) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300 - gap));
    }
    last_send_ = std::chrono::steady_clock::now();
}

bool ZaloApi::login(const std::string& cookie, const std::string& imei,
                    const std::string& user_agent) {
    cookie_     = cookie;
    user_agent_ = user_agent;
    return true;
}

void ZaloApi::record_sent(const std::string& content, const std::string& thread_id) {
    std::lock_guard<std::mutex> lk(sent_mtx_);
    sent_history_.push_back({content, thread_id, std::chrono::steady_clock::now()});
    if (sent_history_.size() > 100) {
        sent_history_.erase(sent_history_.begin());
    }
}

bool ZaloApi::was_sent_by_bot(const std::string& content, const std::string& thread_id) {
    std::lock_guard<std::mutex> lk(sent_mtx_);
    auto now = std::chrono::steady_clock::now();
    for (auto it = sent_history_.rbegin(); it != sent_history_.rend(); ++it) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->ts).count();
        if (age > 10) break;
        if (it->content == content && it->thread_id == thread_id) {
            return true;
        }
    }
    return false;
}

static int utf8_to_utf16_offset(const std::string& s, int byte_limit) {
    int utf16_count = 0;
    int i = 0;
    int max_i = (byte_limit < (int)s.size()) ? byte_limit : (int)s.size();

    while (i < max_i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            i += 1; utf16_count += 1;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2; utf16_count += 1;
        } else if ((c & 0xF0) == 0xE0) {
            i += 3; utf16_count += 1;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4; utf16_count += 2;
        } else {
            i += 1; utf16_count += 1;
        }
    }
    return utf16_count;
}

json ZaloApi::send_message(const std::string& message, const std::string& thread_id,
                           int thread_type) {
    if (!bridge_) return json{{"error", "no bridge"}};

    json r = bridge_->send_command("send_message", {
        {"text", message},
        {"thread_id", thread_id},
        {"type", thread_type}
    });

    record_sent(message, thread_id);
    return r;
}

json ZaloApi::send_message_with_mention(const std::string& message,
                                         const std::string& thread_id,
                                         int thread_type,
                                         const std::string& mention_uid,
                                         int mention_pos,
                                         int mention_len) {
    if (!bridge_) return json{{"error", "no bridge"}};

    int utf16_pos = utf8_to_utf16_offset(message, mention_pos);
    int utf16_len = utf8_to_utf16_offset(message.substr(mention_pos), mention_len);

    json mentions = json::array();
    mentions.push_back({
        {"pos", utf16_pos},
        {"uid", mention_uid},
        {"len", utf16_len}
    });

    json r = bridge_->send_command("send_message", {
        {"text", message},
        {"thread_id", thread_id},
        {"type", thread_type},
        {"mentions", mentions}
    });

    record_sent(message, thread_id);
    return r;
}

void ZaloApi::send_message_async(const std::string& message,
                                  const std::string& thread_id,
                                  int thread_type) {
    if (!bridge_) return;
    throttle();

    bridge_->send_async("send_message", {
        {"text", message},
        {"thread_id", thread_id},
        {"type", thread_type}
    });

    record_sent(message, thread_id);
}

void ZaloApi::send_message_with_mention_async(const std::string& message,
                                               const std::string& thread_id,
                                               int thread_type,
                                               const std::string& mention_uid,
                                               int mention_pos,
                                               int mention_len) {
    if (!bridge_) return;
    throttle();

    int utf16_pos = utf8_to_utf16_offset(message, mention_pos);
    int utf16_len = utf8_to_utf16_offset(message.substr(mention_pos), mention_len);

    json mentions = json::array();
    mentions.push_back({
        {"pos", utf16_pos},
        {"uid", mention_uid},
        {"len", utf16_len}
    });

    bridge_->send_async("send_message", {
        {"text", message},
        {"thread_id", thread_id},
        {"type", thread_type},
        {"mentions", mentions}
    });

    record_sent(message, thread_id);
}

void ZaloApi::send_image_async(const std::string& image_path,
                                const std::string& thread_id,
                                int thread_type) {
    if (!bridge_) return;
    throttle();
    bridge_->send_async("send_image", {
        {"path", image_path},
        {"thread_id", thread_id},
        {"type", thread_type}
    });
}

void ZaloApi::send_message_with_image_async(const std::string& text,
                                             const std::string& image_path,
                                             const std::string& thread_id,
                                             int thread_type) {
    if (!bridge_) return;
    throttle();
    bridge_->send_async("send_message_with_image", {
        {"text", text},
        {"image_path", image_path},
        {"thread_id", thread_id},
        {"type", thread_type}
    });
    record_sent(text, thread_id);
}

void ZaloApi::send_qr_async(const std::string& text,
                             const std::string& thread_id,
                             int thread_type) {
    if (!bridge_) return;
    throttle();

    // Tạo path duy nhất
    std::string outPath = "data/qr_"
                        + std::to_string((long long)std::time(nullptr))
                        + "_" + std::to_string(rand() % 100000)
                        + ".png";

    auto r = bridge_->send_command("create_qr", {
        {"text", text},
        {"path", outPath}
    }, 15000);

    if (!r.value("ok", false)) {
        send_message_async("❌ Tạo QR thất bại", thread_id, thread_type);
        return;
    }

    // Lấy path thực từ result (có thể bridge override)
    std::string realPath = outPath;
    if (r.contains("result") && r["result"].is_object()) {
        realPath = r["result"].value("path", outPath);
    }

    send_message_with_image_async("📱 QR Code", realPath, thread_id, thread_type);
}

bool ZaloApi::set_group_lock(const std::string& group_id, bool locked) {
    if (!bridge_) return false;
    throttle();
    auto r = bridge_->send_command("update_group_settings", {
        {"group_id",    group_id},
        {"lockSendMsg", locked}
    }, 10000);
    return r.value("ok", false);
}

bool ZaloApi::set_group_approval(const std::string& group_id, bool approval) {
    if (!bridge_) return false;
    throttle();
    auto r = bridge_->send_command("update_group_settings", {
        {"group_id", group_id},
        {"joinAppr", approval}
    }, 10000);
    return r.value("ok", false);
}

json ZaloApi::send_sticker(const std::string& sticker_id, const std::string& thread_id,
                           int thread_type) {
    if (!bridge_) return json{{"error", "no bridge"}};
    return bridge_->send_command("send_sticker", {
        {"sticker_id", sticker_id},
        {"thread_id", thread_id},
        {"type", thread_type}
    });
}

void ZaloApi::send_sticker_async(const std::string& sticker_id,
                                  const std::string& thread_id,
                                  int thread_type) {
    if (!bridge_) return;
    throttle();
    bridge_->send_async("send_sticker", {
        {"sticker_id", sticker_id},
        {"thread_id", thread_id},
        {"type", thread_type}
    });
}

json ZaloApi::add_user_to_group(const std::string& group_id, const std::string& user_id) {
    if (!bridge_) return json{{"error", "no bridge"}};
    return bridge_->send_command("add_user_to_group",
                                 {{"group_id", group_id}, {"user_id", user_id}});
}

json ZaloApi::remove_user_from_group(const std::string& group_id, const std::string& user_id) {
    if (!bridge_) return json{{"error", "no bridge"}};
    return bridge_->send_command("remove_user_from_group",
                                 {{"group_id", group_id}, {"user_id", user_id}});
}

json ZaloApi::get_group_info(const std::string& group_id) {
    if (!bridge_) return json{{"error", "no bridge"}};
    return bridge_->send_command("get_group_info", {{"group_id", group_id}});
}

json ZaloApi::get_user_info(const std::string& user_id) {
    if (!bridge_) return json{{"error", "no bridge"}};
    return bridge_->send_command("get_user_info", {{"user_id", user_id}}, 28000);
}

bool ZaloApi::download_image(const std::string& url, const std::string& save_path) {
    if (!bridge_) return false;
    auto r = bridge_->send_command("download_image",
                                    {{"url", url}, {"path", save_path}},
                                    30000);
    return r.value("ok", false);
}

json ZaloApi::delete_message(const std::string& msg_id,
                              const std::string& cli_msg_id,
                              const std::string& uid_from,
                              const std::string& thread_id,
                              int thread_type) {
    if (!bridge_) return json{{"error", "no bridge"}};
    return bridge_->send_command("delete_message", {
        {"msg_id", msg_id},
        {"cli_msg_id", cli_msg_id},
        {"uid_from", uid_from},
        {"thread_id", thread_id},
        {"type", thread_type}
    });
}

json ZaloApi::undo_message(const std::string& msg_id,
                            const std::string& cli_msg_id,
                            const std::string& thread_id,
                            int thread_type) {
    if (!bridge_) return json{{"error", "no bridge"}};
    return bridge_->send_command("undo_message", {
        {"msg_id", msg_id},
        {"cli_msg_id", cli_msg_id},
        {"thread_id", thread_id},
        {"type", thread_type}
    });
}

json ZaloApi::add_reaction(const std::string& reaction,
                            const std::string& msg_id,
                            const std::string& cli_msg_id,
                            const std::string& thread_id,
                            int thread_type) {
    if (!bridge_) return json{{"error", "no bridge"}};
    return bridge_->send_command("add_reaction", {
        {"reaction", reaction},
        {"msg_id", msg_id},
        {"cli_msg_id", cli_msg_id},
        {"thread_id", thread_id},
        {"type", thread_type}
    });
}

void ZaloApi::add_reaction_async(const std::string& reaction,
                                  const std::string& msg_id,
                                  const std::string& cli_msg_id,
                                  const std::string& thread_id,
                                  int thread_type) {
    if (!bridge_) return;
    bridge_->send_async("add_reaction", {
        {"reaction", reaction},
        {"msg_id", msg_id},
        {"cli_msg_id", cli_msg_id},
        {"thread_id", thread_id},
        {"type", thread_type}
    });
}
