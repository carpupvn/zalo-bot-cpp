#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <mutex>
#include <chrono>

class WordChainDictionary {
public:
    bool load(const std::string& path);
    bool save(const std::string& path);
    bool saveSorted(const std::string& path);
    bool contains(const std::string& word) const;
    bool isCommon(const std::string& word) const;
    const std::vector<std::string>* getByFirst(const std::string& syllable) const;
    size_t size() const { return words_.size(); }
    size_t commonCount() const { return commonSet_.size(); }
    const std::vector<std::string>& allWords() const { return words_; }
    bool addWord(const std::string& raw, bool defaultCommon = true);
    bool removeWord(const std::string& raw);
    void clear();
private:
    std::vector<std::string> words_;
    std::unordered_set<std::string> wordSet_;
    std::unordered_set<std::string> commonSet_;
    std::unordered_map<std::string, std::vector<std::string>> byFirst_;
    std::string path_;
};

struct WordChainState {
    std::string thread_id;
    std::string last_phrase;
    std::unordered_set<std::string> used_words;
    std::chrono::steady_clock::time_point started_at;
    std::string started_by_uid;
    std::string last_player_uid;
    int chain_length = 0;
};

enum class PlayResult {
    OK, WIN, NOT_ACTIVE,
    WORD_EMPTY, WORD_NOT_TWO_SYLLABLES, WORD_NOT_IN_DICT,
    WORD_WRONG_START, WORD_ALREADY_USED
};

struct PlayOutcome {
    PlayResult result = PlayResult::NOT_ACTIVE;
    std::string bot_reply;
    std::string required_start;
    int chain_length = 0;
    bool player_won = false;
    std::string error_msg;
};

class WordChainEngine {
public:
    bool loadDictionary(const std::string& path);
    bool isLoaded() const { return loaded_; }
    void startGame(const std::string& thread_id, const std::string& uid);
    PlayOutcome startGameBotFirst(const std::string& thread_id, const std::string& uid);
    bool cancelGame(const std::string& thread_id);
    bool hasActiveGame(const std::string& thread_id) const;
    const WordChainState* getState(const std::string& thread_id) const;
    PlayOutcome playWord(const std::string& thread_id, const std::string& uid,
                         const std::string& raw_word);
    const WordChainDictionary& dictionary() const { return dict_; }
    WordChainDictionary& dictionary() { return dict_; }
    bool saveDictionary();
    bool sortDictionary();
    size_t dictionarySize() const { return dict_.size(); }
    size_t commonCount() const { return dict_.commonCount(); }
private:
    WordChainDictionary dict_;
    std::unordered_map<std::string, WordChainState> states_;
    std::string dict_path_ = "data/dictionary.txt";
    mutable std::mutex mutex_;
    std::mt19937 rng_{std::random_device{}()};
    bool loaded_ = false;
    std::string botChoose(const std::string& need, const WordChainState& state);
};