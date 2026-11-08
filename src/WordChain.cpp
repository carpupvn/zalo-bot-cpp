#include "WordChain.hpp"
#include "VietnameseUtils.hpp"
#include <fstream>
#include <algorithm>

bool WordChainDictionary::load(const std::string& path) {
    words_.clear(); wordSet_.clear(); commonSet_.clear(); byFirst_.clear();
    path_ = path;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        bool common = true;
        if (!line.empty() && line[0] == '*') { common = false; line = line.substr(1); }
        std::string w = vi::normalizeWord(line);
        if (w.empty() || !vi::isValidWord(w) || wordSet_.count(w)) continue;
        words_.push_back(w);
        wordSet_.insert(w);
        if (common) commonSet_.insert(w);
    }
    for (const auto& w : words_) {
        auto syl = vi::splitSyllables(w);
        byFirst_[syl[0]].push_back(w);
    }
    return true;
}

bool WordChainDictionary::save(const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const auto& w : words_) {
        if (!commonSet_.count(w)) out << '*';
        out << w << '\n';
    }
    return (bool)out;
}

bool WordChainDictionary::saveSorted(const std::string& path) {
    auto sorted = words_;
    std::sort(sorted.begin(), sorted.end(), vi::vietLess);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const auto& w : sorted) {
        if (!commonSet_.count(w)) out << '*';
        out << w << '\n';
    }
    return (bool)out;
}

bool WordChainDictionary::contains(const std::string& word) const { return wordSet_.count(word) > 0; }
bool WordChainDictionary::isCommon(const std::string& word) const { return commonSet_.count(word) > 0; }

const std::vector<std::string>* WordChainDictionary::getByFirst(const std::string& syllable) const {
    auto it = byFirst_.find(syllable);
    return it == byFirst_.end() ? nullptr : &it->second;
}

bool WordChainDictionary::addWord(const std::string& raw, bool defaultCommon) {
    std::string s = raw;
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;
    s = s.substr(start);
    bool common = defaultCommon;
    if (!s.empty() && s[0] == '*') { common = false; s = s.substr(1); }
    std::string w = vi::normalizeWord(s);
    if (!vi::isValidWord(w) || wordSet_.count(w)) return false;
    words_.push_back(w);
    wordSet_.insert(w);
    if (common) commonSet_.insert(w);
    byFirst_[vi::splitSyllables(w)[0]].push_back(w);
    return true;
}

bool WordChainDictionary::removeWord(const std::string& raw) {
    std::string w = vi::normalizeWord(raw);
    if (!wordSet_.count(w)) return false;
    auto it = std::find(words_.begin(), words_.end(), w);
    if (it != words_.end()) words_.erase(it);
    wordSet_.erase(w);
    commonSet_.erase(w);
    auto syl = vi::splitSyllables(w);
    auto bit = byFirst_.find(syl[0]);
    if (bit != byFirst_.end()) {
        auto& vec = bit->second;
        vec.erase(std::remove(vec.begin(), vec.end(), w), vec.end());
        if (vec.empty()) byFirst_.erase(bit);
    }
    return true;
}

void WordChainDictionary::clear() {
    words_.clear(); wordSet_.clear(); commonSet_.clear(); byFirst_.clear();
}

bool WordChainEngine::loadDictionary(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    dict_path_ = path;
    loaded_ = dict_.load(path);
    return loaded_;
}

void WordChainEngine::startGame(const std::string& thread_id, const std::string& uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    WordChainState s;
    s.thread_id = thread_id;
    s.started_by_uid = uid;
    s.started_at = std::chrono::steady_clock::now();
    states_[thread_id] = std::move(s);
}

PlayOutcome WordChainEngine::startGameBotFirst(const std::string& thread_id, const std::string& uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    PlayOutcome o;

    // Chon tu mo man: phai la common va phai co tu common bat dau bang am tiet cuoi
    std::vector<const std::string*> candidates;
    for (const auto& w : dict_.allWords()) {
        if (!dict_.isCommon(w)) continue;
        auto syl = vi::splitSyllables(w);
        const auto* next = dict_.getByFirst(syl[1]);
        if (!next) continue;
        bool ok = false;
        for (const auto& nw : *next) {
            if (nw == w) continue;
            if (!dict_.isCommon(nw)) continue;
            ok = true; break;
        }
        if (ok) candidates.push_back(&w);
    }
    if (candidates.empty()) {
        o.result = PlayResult::NOT_ACTIVE;
        o.error_msg = "Kh\u00F4ng t\u00ECm \u0111\u01B0\u1EE3c t\u1EEB m\u1EDF m\u00E0n.";
        return o;
    }
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    std::string bot_word = *candidates[dist(rng_)];
    auto syl = vi::splitSyllables(bot_word);

    WordChainState s;
    s.thread_id = thread_id;
    s.started_by_uid = uid;
    s.started_at = std::chrono::steady_clock::now();
    s.used_words.insert(bot_word);
    s.last_phrase = bot_word;
    s.chain_length = 1;
    states_[thread_id] = std::move(s);

    o.result = PlayResult::OK;
    o.chain_length = 1;
    o.required_start = syl[1];
    o.bot_reply =
        "\U0001F916 Bot m\u1EDF m\u00E0n: \"" + bot_word + "\"\n"
        "\U0001F517 B\u1EA1n n\u1ED1i ti\u1EBFp b\u1EB1ng t\u1EEB b\u1EAFt \u0111\u1EA7u v\u1EDBi \"" + syl[1] + "\"";
    return o;
}

bool WordChainEngine::cancelGame(const std::string& thread_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_.erase(thread_id) > 0;
}

bool WordChainEngine::hasActiveGame(const std::string& thread_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_.count(thread_id) > 0;
}

const WordChainState* WordChainEngine::getState(const std::string& thread_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(thread_id);
    return it == states_.end() ? nullptr : &it->second;
}

bool WordChainEngine::saveDictionary() {
    std::lock_guard<std::mutex> lock(mutex_);
    return dict_.save(dict_path_);
}

bool WordChainEngine::sortDictionary() {
    std::lock_guard<std::mutex> lock(mutex_);
    return dict_.saveSorted(dict_path_);
}

// Bot CHI chon tu COMMON (khong co dau '*').
// Dam bao sau khi bot danh, nguoi choi con it nhat 1 tu (bat ky, ke ca '*')
// de noi tiep -> khong bao gio day user vao duong cung.
// Neu khong co nuoc an toan -> return "" -> caller choi thua (nguoi thang).
std::string WordChainEngine::botChoose(const std::string& need, const WordChainState& state) {
    const auto* vec = dict_.getByFirst(need);
    if (!vec) return std::string();

    std::vector<const std::string*> safe_common;
    for (const auto& w : *vec) {
        if (state.used_words.count(w)) continue;
        // Bot TUYET DOI khong dung tu '*'
        if (!dict_.isCommon(w)) continue;

        auto syl = vi::splitSyllables(w);
        const auto* next = dict_.getByFirst(syl[1]);
        bool hasNext = false;
        if (next) {
            for (const auto& nw : *next) {
                if (nw == w) continue;
                if (state.used_words.count(nw)) continue;
                // Cho phep user noi bang ca tu '*' (user co the biet va go)
                hasNext = true;
                break;
            }
        }
        if (hasNext) safe_common.push_back(&w);
    }

    if (safe_common.empty()) return std::string();

    std::uniform_int_distribution<size_t> dist(0, safe_common.size() - 1);
    return *safe_common[dist(rng_)];
}

PlayOutcome WordChainEngine::playWord(const std::string& thread_id,
                                       const std::string& uid,
                                       const std::string& raw_word) {
    std::lock_guard<std::mutex> lock(mutex_);
    PlayOutcome o;

    auto sit = states_.find(thread_id);
    if (sit == states_.end()) {
        o.error_msg = "Ch\u01B0a c\u00F3 v\u00E1n.";
        return o;
    }
    WordChainState& state = sit->second;
    std::string word = vi::normalizeWord(raw_word);

    if (word.empty()) {
        o.result = PlayResult::WORD_EMPTY;
        o.error_msg = "C\u1EA7n 1 c\u1EE5m t\u1EEB 2 \u00E2m ti\u1EBFt.";
        return o;
    }
    if (!vi::isValidWord(word)) {
        o.result = PlayResult::WORD_NOT_TWO_SYLLABLES;
        o.error_msg = "C\u1EE5m t\u1EEB ph\u1EA3i c\u00F3 \u0111\u00FAng 2 \u00E2m ti\u1EBFt.";
        return o;
    }
    if (!dict_.contains(word)) {
        o.result = PlayResult::WORD_NOT_IN_DICT;
        o.error_msg = "T\u1EEB kh\u00F4ng c\u00F3 trong t\u1EEB \u0111i\u1EC3n.";
        return o;
    }
    if (state.used_words.count(word)) {
        o.result = PlayResult::WORD_ALREADY_USED;
        o.error_msg = "T\u1EEB n\u00E0y \u0111\u00E3 d\u00F9ng r\u1ED3i.";
        return o;
    }

    auto syl = vi::splitSyllables(word);
    if (!state.last_phrase.empty()) {
        auto lastSyl = vi::splitSyllables(state.last_phrase);
        if (syl[0] != lastSyl.back()) {
            o.result = PlayResult::WORD_WRONG_START;
            o.required_start = lastSyl.back();
            o.error_msg = "Ph\u1EA3i b\u1EAFt \u0111\u1EA7u b\u1EB1ng '" + lastSyl.back() + "'.";
            return o;
        }
    }

    state.used_words.insert(word);
    state.last_phrase = word;
    state.last_player_uid = uid;
    state.chain_length++;
    o.chain_length = state.chain_length;

    std::string bot_word = botChoose(syl[1], state);
    if (bot_word.empty()) {
        o.result = PlayResult::WIN;
        o.player_won = true;
        o.bot_reply =
            "\u2705 \"" + word + "\" - h\u1EE3p l\u1EC7!\n"
            "\U0001F916 Bot b\u00ED! B\u1EA1n th\u1EAFng!\n"
            "Chu\u1ED7i: " + std::to_string(state.chain_length) + " t\u1EEB";
        states_.erase(thread_id);
        return o;
    }

    state.used_words.insert(bot_word);
    auto bSyl = vi::splitSyllables(bot_word);
    state.last_phrase = bot_word;
    state.chain_length++;
    o.chain_length = state.chain_length;

    bool playerCanContinue = false;
    const auto* nextVec = dict_.getByFirst(bSyl[1]);
    if (nextVec) {
        for (const auto& nw : *nextVec) {
            if (!state.used_words.count(nw)) { playerCanContinue = true; break; }
        }
    }

    if (!playerCanContinue) {
        o.result = PlayResult::WIN;
        o.player_won = false;
        o.bot_reply =
            "\u2705 \"" + word + "\" - h\u1EE3p l\u1EC7!\n"
            "\U0001F916 Bot: \"" + bot_word + "\"\n"
            "\U0001F517 Ti\u1EBFp: \"" + bSyl[1] + "\"\n"
            "\u26A0\uFE0F B\u1EA1n kh\u00F4ng c\u00F2n t\u1EEB n\u00E0o b\u1EAFt \u0111\u1EA7u b\u1EB1ng \"" + bSyl[1] + "\" \u2192 Bot th\u1EAFng!";
        states_.erase(thread_id);
        return o;
    }

    o.result = PlayResult::OK;
    o.bot_reply =
        "\u2705 \"" + word + "\" - h\u1EE3p l\u1EC7!\n"
        "\U0001F916 Bot: \"" + bot_word + "\"\n"
        "\U0001F517 Ti\u1EBFp: \"" + bSyl[1] + "\"\n"
        "Chu\u1ED7i: " + std::to_string(state.chain_length) + " t\u1EEB";
    return o;
}