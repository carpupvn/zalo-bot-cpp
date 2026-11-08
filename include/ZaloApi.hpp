#pragma once
#include "nativeHTTP.hpp"
#include "json.hpp"
#include "ZaloBridge.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <tuple>

using json = nlohmann::json;

class ZaloApi {
public:
    json run_python(const std::string& code, const std::vector<std::string>& args, bool is_admin = false, int suspicion = 0);
    ZaloApi();
    bool login(const std::string& cookie, const std::string& imei, const std::string& user_agent);

    json send_message(const std::string& message, const std::string& thread_id, int thread_type);
    json send_message_with_mention(const std::string& message,
                                    const std::string& thread_id,
                                    int thread_type,
                                    const std::string& mention_uid,
                                    int mention_pos,
                                    int mention_len);

    void send_message_with_image_mentions_async(const std::string& text,
                                                 const std::string& image_path,
                                                 const std::string& thread_id,
                                                 int thread_type,
                                                 const std::vector<std::tuple<std::string,int,int>>& mentions);

    void send_message_async(const std::string& message,
                            const std::string& thread_id,
                            int thread_type);

    void send_message_with_image_mentions_ttl_async(const std::string& text,
                                                     const std::string& image_path,
                                                     const std::string& thread_id,
                                                     int thread_type,
                                                     const std::vector<std::tuple<std::string,int,int>>& mentions,
                                                     int ttl_ms);

    json send_message_with_image_mentions_sync(const std::string& text,
                                                  const std::string& image_path,
                                                  const std::string& thread_id,
                                                  int thread_type,
                                                  const std::vector<std::tuple<std::string,int,int>>& mentions);

    void send_message_with_mention_async(const std::string& message,
                                          const std::string& thread_id,
                                          int thread_type,
                                          const std::string& mention_uid,
                                          int mention_pos,
                                          int mention_len);

    void send_image_async(const std::string& image_path,
                          const std::string& thread_id,
                          int thread_type);

    void send_message_with_image_async(const std::string& text,
                                        const std::string& image_path,
                                        const std::string& thread_id,
                                        int thread_type);

    void send_message_with_image_mention_async(const std::string& text,
                                                const std::string& image_path,
                                                const std::string& thread_id,
                                                int thread_type,
                                                const std::string& mention_uid,
                                                int mention_pos,
                                                int mention_len);

    // Tạo QR từ text → upload ảnh lên Zalo (async, tự tạo file data/qr_*.png)
    void send_qr_async(const std::string& text,
                       const std::string& thread_id,
                       int thread_type);

    // Khóa chat (chỉ admin gửi được tin) — đồng bộ, trả về true nếu OK
    bool set_group_lock(const std::string& group_id, bool locked);

    // Bật/tắt duyệt thành viên mới — đồng bộ
    bool set_group_approval(const std::string& group_id, bool approval);

    json send_sticker(const std::string& sticker_id, const std::string& thread_id, int thread_type);
    void send_sticker_async(const std::string& sticker_id, const std::string& thread_id, int thread_type);
    json add_user_to_group(const std::string& group_id, const std::string& user_id);
    json remove_user_from_group(const std::string& group_id, const std::string& user_id);
    json get_group_info(const std::string& group_id);
    json get_user_info(const std::string& user_id);

    // Táº£i áº£nh vá» file local (blocking)
    bool download_image(const std::string& url, const std::string& save_path);

    json delete_message(const std::string& msg_id,
                        const std::string& cli_msg_id,
                        const std::string& uid_from,
                        const std::string& thread_id,
                        int thread_type);

    json undo_message(const std::string& msg_id,
                      const std::string& cli_msg_id,
                      const std::string& thread_id,
                      int thread_type);

    json add_reaction(const std::string& reaction,
                      const std::string& msg_id,
                      const std::string& cli_msg_id,
                      const std::string& thread_id,
                      int thread_type);

    void add_reaction_async(const std::string& reaction,
                            const std::string& msg_id,
                            const std::string& cli_msg_id,
                            const std::string& thread_id,
                            int thread_type);

    std::string get_own_id() const { return own_id; }
    void set_own_id(const std::string& id) { own_id = id; }
    void set_bridge(ZaloBridge* b) { bridge_ = b; }

    bool was_sent_by_bot(const std::string& content, const std::string& thread_id);

    nativeHTTP::Client& get_client() { return http_client; }

private:
    struct SentMsg {
        std::string content;
        std::string thread_id;
        std::chrono::steady_clock::time_point ts;
    };

    void record_sent(const std::string& content, const std::string& thread_id);
    void throttle();

    nativeHTTP::Client http_client;
    ZaloBridge* bridge_ = nullptr;
    std::string own_id;
    std::string cookie_;
    std::string user_agent_;

    std::vector<SentMsg> sent_history_;
    std::mutex sent_mtx_;

    std::chrono::steady_clock::time_point last_send_;
    std::mutex send_mtx_;
};
