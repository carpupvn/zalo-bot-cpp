#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <cctype>

namespace vi {

inline std::string toLower(const std::string& s) {
    static const std::unordered_map<std::string, std::string> T = {
        {"\xC4\x90","\xC4\x91"},
        {"\xC4\x82","\xC4\x83"}, {"\xC3\x82","\xC3\xA2"}, {"\xC3\x8A","\xC3\xAA"},
        {"\xC3\x94","\xC3\xB4"}, {"\xC6\xA0","\xC6\xA1"}, {"\xC6\xAF","\xC6\xB0"},
        {"\xC3\x81","\xC3\xA1"}, {"\xC3\x80","\xC3\xA0"}, {"\xE1\xBA\xA2","\xE1\xBA\xA3"},
        {"\xC3\x83","\xC3\xA3"}, {"\xE1\xBA\xA0","\xE1\xBA\xA1"},
        {"\xE1\xBA\xAE","\xE1\xBA\xAF"}, {"\xE1\xBA\xB0","\xE1\xBA\xB1"},
        {"\xE1\xBA\xB2","\xE1\xBA\xB3"}, {"\xE1\xBA\xB4","\xE1\xBA\xB5"},
        {"\xE1\xBA\xB6","\xE1\xBA\xB7"},
        {"\xE1\xBA\xA4","\xE1\xBA\xA5"}, {"\xE1\xBA\xA6","\xE1\xBA\xA7"},
        {"\xE1\xBA\xA8","\xE1\xBA\xA9"}, {"\xE1\xBA\xAA","\xE1\xBA\xAB"},
        {"\xE1\xBA\xAC","\xE1\xBA\xAD"},
        {"\xC3\x89","\xC3\xA9"}, {"\xC3\x88","\xC3\xA8"}, {"\xE1\xBA\xBA","\xE1\xBA\xBB"},
        {"\xE1\xBA\xBC","\xE1\xBA\xBD"}, {"\xE1\xBA\xB8","\xE1\xBA\xB9"},
        {"\xE1\xBA\xBE","\xE1\xBA\xBF"}, {"\xE1\xBB\x80","\xE1\xBB\x81"},
        {"\xE1\xBB\x82","\xE1\xBB\x83"}, {"\xE1\xBB\x84","\xE1\xBB\x85"},
        {"\xE1\xBB\x86","\xE1\xBB\x87"},
        {"\xC3\x8D","\xC3\xAD"}, {"\xC3\x8C","\xC3\xAC"}, {"\xE1\xBB\x88","\xE1\xBB\x89"},
        {"\xE1\xBB\x8A","\xE1\xBB\x8B"}, {"\xE1\xBB\x8C","\xE1\xBB\x8D"},
        {"\xC3\x93","\xC3\xB3"}, {"\xC3\x92","\xC3\xB2"}, {"\xE1\xBB\x8E","\xE1\xBB\x8F"},
        {"\xC3\x95","\xC3\xB5"}, {"\xE1\xBB\x8C","\xE1\xBB\x8D"},
        {"\xE1\xBB\x90","\xE1\xBB\x91"}, {"\xE1\xBB\x92","\xE1\xBB\x93"},
        {"\xE1\xBB\x94","\xE1\xBB\x95"}, {"\xE1\xBB\x96","\xE1\xBB\x97"},
        {"\xE1\xBB\x98","\xE1\xBB\x99"},
        {"\xE1\xBB\x9A","\xE1\xBB\x9B"}, {"\xE1\xBB\x9C","\xE1\xBB\x9D"},
        {"\xE1\xBB\x9E","\xE1\xBB\x9F"}, {"\xE1\xBB\xA0","\xE1\xBB\xA1"},
        {"\xE1\xBB\xA2","\xE1\xBB\xA3"},
        {"\xC3\x9A","\xC3\xBA"}, {"\xC3\x99","\xC3\xB9"}, {"\xE1\xBB\xA6","\xE1\xBB\xA7"},
        {"\xE1\xBB\xA8","\xE1\xBB\xA9"}, {"\xE1\xBB\xA4","\xE1\xBB\xA5"},
        {"\xE1\xBB\xAA","\xE1\xBB\xAB"}, {"\xE1\xBB\xAC","\xE1\xBB\xAD"},
        {"\xE1\xBB\xAE","\xE1\xBB\xAF"}, {"\xE1\xBB\xB0","\xE1\xBB\xB1"},
        {"\xE1\xBB\xB2","\xE1\xBB\xB3"},
        {"\xC3\x9D","\xC3\xBD"}, {"\xE1\xBB\xB2","\xE1\xBB\xB3"}, {"\xE1\xBB\xB6","\xE1\xBB\xB7"},
        {"\xE1\xBB\xB8","\xE1\xBB\xB9"}, {"\xE1\xBB\xB4","\xE1\xBB\xB5"},
    };
    std::string r; r.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { r += (char)std::tolower(c); ++i; }
        else {
            int len = 1;
            if      ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            std::string seq = s.substr(i, len);
            auto it = T.find(seq);
            r += (it != T.end()) ? it->second : seq;
            i += len;
        }
    }
    return r;
}

inline std::string normalizeWord(const std::string& w) {
    std::string r; bool sp = true;
    for (char c : w) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!sp) { r += ' '; sp = true; }
        } else { r += c; sp = false; }
    }
    if (!r.empty() && r.back() == ' ') r.pop_back();
    return toLower(r);
}

inline std::vector<std::string> splitSyllables(const std::string& w) {
    std::vector<std::string> r;
    std::istringstream iss(w);
    std::string t;
    while (iss >> t) r.push_back(t);
    return r;
}

inline bool isValidWord(const std::string& w) {
    return splitSyllables(w).size() == 2;
}

inline std::vector<std::string> splitUtf8(const std::string& s) {
    std::vector<std::string> r;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        int len = 1;
        if      ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        r.push_back(s.substr(i, len));
        i += len;
    }
    return r;
}

inline std::pair<int,int> vietKey(const std::string& ch) {
    static const std::unordered_map<std::string, std::pair<int,int>> M = {
        {"a",{1,0}},{"\xC3\xA0",{1,1}},{"\xC3\xA1",{1,2}},{"\xE1\xBA\xA3",{1,3}},{"\xC3\xA3",{1,4}},{"\xE1\xBA\xA1",{1,5}},
        {"\xC4\x83",{2,0}},{"\xE1\xBA\xB1",{2,1}},{"\xE1\xBA\xAF",{2,2}},{"\xE1\xBA\xB3",{2,3}},{"\xE1\xBA\xB5",{2,4}},{"\xE1\xBA\xB7",{2,5}},
        {"\xC3\xA2",{3,0}},{"\xE1\xBA\xA7",{3,1}},{"\xE1\xBA\xA5",{3,2}},{"\xE1\xBA\xA9",{3,3}},{"\xE1\xBA\xAB",{3,4}},{"\xE1\xBA\xAD",{3,5}},
        {"b",{4,0}},{"c",{5,0}},
        {"d",{6,0}},{"\xC4\x91",{7,0}},
        {"e",{8,0}},{"\xC3\xA8",{8,1}},{"\xC3\xA9",{8,2}},{"\xE1\xBA\xBB",{8,3}},{"\xE1\xBA\xBD",{8,4}},{"\xE1\xBA\xB9",{8,5}},
        {"\xC3\xAA",{9,0}},{"\xE1\xBB\x81",{9,1}},{"\xE1\xBA\xBF",{9,2}},{"\xE1\xBB\x83",{9,3}},{"\xE1\xBB\x85",{9,4}},{"\xE1\xBB\x87",{9,5}},
        {"g",{10,0}},{"h",{11,0}},
        {"i",{12,0}},{"\xC3\xAC",{12,1}},{"\xC3\xAD",{12,2}},{"\xE1\xBB\x89",{12,3}},{"\xE1\xBB\x8B",{12,4}},{"\xE1\xBB\x8D",{12,5}},
        {"k",{13,0}},{"l",{14,0}},{"m",{15,0}},{"n",{16,0}},
        {"o",{17,0}},{"\xC3\xB2",{17,1}},{"\xC3\xB3",{17,2}},{"\xE1\xBB\x8F",{17,3}},{"\xC3\xB5",{17,4}},{"\xE1\xBB\x8D",{17,5}},
        {"\xC3\xB4",{18,0}},{"\xE1\xBB\x93",{18,1}},{"\xE1\xBB\x91",{18,2}},{"\xE1\xBB\x95",{18,3}},{"\xE1\xBB\x97",{18,4}},{"\xE1\xBB\x99",{18,5}},
        {"\xC6\xA1",{19,0}},{"\xE1\xBB\x9D",{19,1}},{"\xE1\xBB\x9B",{19,2}},{"\xE1\xBB\x9F",{19,3}},{"\xE1\xBB\xA1",{19,4}},{"\xE1\xBB\xA3",{19,5}},
        {"p",{20,0}},{"q",{21,0}},{"r",{22,0}},{"s",{23,0}},{"t",{24,0}},
        {"u",{25,0}},{"\xC3\xB9",{25,1}},{"\xC3\xBA",{25,2}},{"\xE1\xBB\xA7",{25,3}},{"\xE1\xBB\xA9",{25,4}},{"\xE1\xBB\xA5",{25,5}},
        {"\xC6\xB0",{26,0}},{"\xE1\xBB\xAB",{26,1}},{"\xE1\xBB\xA9",{26,2}},{"\xE1\xBB\xAD",{26,3}},{"\xE1\xBB\xAF",{26,4}},{"\xE1\xBB\xB1",{26,5}},
        {"v",{27,0}},{"x",{28,0}},
        {"y",{29,0}},{"\xE1\xBB\xB3",{29,1}},{"\xC3\xBD",{29,2}},{"\xE1\xBB\xB7",{29,3}},{"\xE1\xBB\xB9",{29,4}},{"\xE1\xBB\xB5",{29,5}},
    };
    auto it = M.find(ch);
    if (it != M.end()) return it->second;
    return {1000, 0};
}

inline bool vietLess(const std::string& a, const std::string& b) {
    auto va = splitUtf8(a), vb = splitUtf8(b);
    size_t n = std::min(va.size(), vb.size());
    for (size_t i = 0; i < n; ++i) {
        auto ka = vietKey(va[i]), kb = vietKey(vb[i]);
        if (ka != kb) return ka < kb;
    }
    return va.size() < vb.size();
}

} // namespace vi