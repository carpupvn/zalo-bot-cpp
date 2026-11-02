#include "ZaloBridge.hpp"
#include <iostream>
#include <sstream>

ZaloBridge::ZaloBridge() = default;

ZaloBridge::~ZaloBridge() { stop(); }

bool ZaloBridge::start(const std::string& node_exe, const std::string& script) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE hStdinRead  = nullptr;
    HANDLE hStdoutWrite = nullptr;

    if (!CreatePipe(&hStdinRead, &hChildStdin_, &sa, 0)) return false;
    if (!CreatePipe(&hChildStdout_, &hStdoutWrite, &sa, 0)) return false;

    // Chỉ con được kế thừa đầu pipe cần thiết
    SetHandleInformation(hChildStdin_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStdout_, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError  = hStdoutWrite;   // stderr cũng đẩy ra stdout để C++ đọc

    std::string cmd = "\"" + node_exe + "\" \"" + script + "\"";
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    BOOL ok = CreateProcessA(
        nullptr,
        cmdBuf.data(),
        nullptr, nullptr,
        TRUE,                      // inherit handles
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi_
    );

    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);

    if (!ok) {
        std::cerr << "❌ CreateProcess failed: " << GetLastError() << "\n";
        return false;
    }

    running_ = true;
    reader_ = std::thread([this] { reader_loop(); });
    return true;
}

void ZaloBridge::send_raw(const std::string& line) {
    std::lock_guard<std::mutex> lk(write_mtx_);
    if (!hChildStdin_) return;

    std::string data = line + "\n";
    DWORD written = 0;
    WriteFile(hChildStdin_, data.data(), (DWORD)data.size(), &written, nullptr);
}

json ZaloBridge::send_command(const std::string& action, const json& params,
                              int timeout_ms) {
    int id;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        id = next_id_++;
    }

    json cmd = {{"id", id}, {"action", action}};
    for (auto& [k, v] : params.items()) cmd[k] = v;

    send_raw(cmd.dump());

    std::unique_lock<std::mutex> lk(mtx_);
    if (!cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                      [this, id] { return responses_.count(id) > 0; })) {
        return json{{"error", "timeout"}};
    }
    json resp = responses_[id];
    responses_.erase(id);
    return resp;
}

void ZaloBridge::send_async(const std::string& action, const json& params) {
    json cmd = {{"id", -1}, {"action", action}};
    for (auto& [k, v] : params.items()) cmd[k] = v;
    send_raw(cmd.dump());
}

void ZaloBridge::reader_loop() {
    std::string buffer;
    char buf[4096];
    DWORD read = 0;

    while (running_) {
        BOOL ok = ReadFile(hChildStdout_, buf, sizeof(buf) - 1, &read, nullptr);
        if (!ok || read == 0) break;

        buf[read] = '\0';
        buffer.append(buf, read);

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (line.size() > 4 && line.substr(0, 4) == "EVT:") {
                try {
                    json ev = json::parse(line.substr(4));
                    if (on_event_) {
                        // Chạy handler trên thread riêng → tránh deadlock
                        // (vì handler có thể gọi send_command → chờ response)
                        auto cb = on_event_;
                        std::thread([cb, ev]() {
                            try { cb(ev); }
                            catch (const std::exception& e) {
                                std::cerr << "[Bridge] Event handler error: "
                                          << e.what() << "\n";
                            }
                        }).detach();
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[Bridge] JSON parse error: " << e.what() << "\n";
                }
            } else if (line.size() > 4 && line.substr(0, 4) == "RES:") {
                try {
                    json r = json::parse(line.substr(4));
                    int id = r.value("id", -1);
                    std::lock_guard<std::mutex> lk(mtx_);
                    responses_[id] = r;
                    cv_.notify_all();
                } catch (...) {}
            } else if (line.size() > 4 && line.substr(0, 4) == "LOG:") {
                std::cout << "[Node] " << line.substr(4) << "\n";
            } else {
                std::cout << "[Node?] " << line << "\n";
            }
        }
    }

    running_ = false;
    cv_.notify_all();
}

void ZaloBridge::stop() {
    if (!running_ && !hChildStdin_) return;
    running_ = false;

    if (hChildStdin_) {
        CloseHandle(hChildStdin_);
        hChildStdin_ = nullptr;
    }
    if (hChildStdout_) {
        CloseHandle(hChildStdout_);
        hChildStdout_ = nullptr;
    }
    if (pi_.hProcess) {
        TerminateProcess(pi_.hProcess, 0);
        CloseHandle(pi_.hProcess);
        pi_.hProcess = nullptr;
    }
    if (pi_.hThread) {
        CloseHandle(pi_.hThread);
        pi_.hThread = nullptr;
    }
    if (reader_.joinable()) reader_.join();
}