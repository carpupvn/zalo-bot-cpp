#include "SafeMemory.hpp"
#include <cstring>

namespace safemem {

bool is_continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

std::size_t utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;   // 0xxxxxxx ASCII
    if ((c & 0xE0) == 0xC0) return 2;   // 110xxxxx
    if ((c & 0xF0) == 0xE0) return 3;   // 1110xxxx
    if ((c & 0xF8) == 0xF0) return 4;   // 11110xxx (emoji)
    return 0;                            // 10xxxxxx hoac 11111xxx: invalid lead
}

std::string sanitize_utf8(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    std::size_t i = 0;
    while (i < input.size()) {
        unsigned char c = (unsigned char)input[i];
        std::size_t len = utf8_char_len(c);
        if (len == 0) {
            result += '?';
            ++i;
            continue;
        }
        if (i + len > input.size()) {
            result += '?';
            ++i;
            continue;
        }
        bool valid = true;
        for (std::size_t k = 1; k < len; ++k) {
            if (!is_continuation((unsigned char)input[i + k])) {
                valid = false;
                break;
            }
        }
        if (valid) {
            result.append(input, i, len);
            i += len;
        } else {
            result += '?';
            ++i;
        }
    }
    return result;
}

std::string truncate_utf8(const std::string& input, std::size_t max_bytes) {
    if (input.size() <= max_bytes) return input;
    std::size_t len = max_bytes;
    // Lui lai den khi gap byte dau cua ky tu (khong phai continuation)
    while (len > 0 && is_continuation((unsigned char)input[len])) {
        --len;
    }
    return input.substr(0, len);
}

std::vector<std::string> split_utf8(const std::string& input, std::size_t max_bytes) {
    std::vector<std::string> result;
    if (max_bytes == 0) return result;
    std::size_t pos = 0;
    while (pos < input.size()) {
        std::size_t remain = input.size() - pos;
        if (remain <= max_bytes) {
            result.push_back(input.substr(pos));
            break;
        }
        std::size_t split = pos + max_bytes;
        // Lui ve ranh gioi UTF-8
        while (split > pos && is_continuation((unsigned char)input[split])) {
            --split;
        }
        // Tim newline hoac space gan nhat (trong 200 byte cuoi)
        std::size_t search_start = (split > pos + 200) ? (split - 200) : pos;
        std::size_t best_split = split;
        for (std::size_t s = split; s > search_start; --s) {
            if (input[s - 1] == '\n' || input[s - 1] == ' ') {
                best_split = s;
                break;
            }
        }
        result.push_back(input.substr(pos, best_split - pos));
        pos = best_split;
    }
    return result;
}

void force_clear(std::string& s) {
    std::string().swap(s);
}

} // namespace safemem