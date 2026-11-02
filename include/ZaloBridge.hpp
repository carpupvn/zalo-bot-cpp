#pragma once
#include "json.hpp"
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <windows.h>

using json = nlohmann::json;

class ZaloBridge {
public:
    ZaloBridge();
    ~ZaloBridge();

    // Khởi động subprocess node bridge.js
    bool start(const std::string& node_exe, const std::string& script);

    // Gửi command và đợi response (blocking)
    json send_command(const std::string& action, const json& params = json::object(),
                      int timeout_ms = 15000);

    // Gửi command không đợi response (fire-and-forget)
    void send_async(const std::string& action, const json& params = json::object());

    void stop();

    void set_event_handler(std::function<void(const json&)> cb) {
        on_event_ = std::move(cb);
    }

private:
    HANDLE hChildStdin_  = nullptr;   // C++ writes → Node stdin
    HANDLE hChildStdout_ = nullptr;   // C++ reads  ← Node stdout
    PROCESS_INFORMATION pi_ = {};

    std::thread reader_;
    std::atomic<bool> running_{false};

    std::function<void(const json&)> on_event_;

    // Response tracking
    std::mutex mtx_;
    std::condition_variable cv_;
    int next_id_ = 1;
    std::map<int, json> responses_;

    std::mutex write_mtx_;

    void reader_loop();
    void send_raw(const std::string& line);
};