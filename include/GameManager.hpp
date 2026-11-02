#pragma once
#include <string>
#include <map>
#include <vector>
#include <random>
#include <ctime>
#include "EconomyManager.hpp"
#include <boost/multiprecision/cpp_int.hpp>

// Enum class - bắt buộc phải có "class"
enum class GameType {
    CHAN_LE,
    TAI_XIU,
    BAU_CUA,
    NOI_TU,
    DOAN_SO,
    CO_CA_RO
};

struct GameState {
    GameType type = GameType::CHAN_LE;
    std::string player_id;
    std::string thread_id;
    int thread_type = 0;
    cpp_int bet_amount = 0;
    bool is_active = false;
    std::map<std::string, std::string> game_data;
    std::time_t start_time = 0;
};

struct CaroBoard {
    char board[15][15];
    std::string player1_id;
    std::string player2_id;
    int current_turn = 1;      // 1 hoặc 2
    bool is_active = false;
    cpp_int bet_amount = 0;  // <-- đã thêm
};

class GameManager {
public:
    explicit GameManager(EconomyManager& economy);

    // Khởi tạo game
    void start_game(GameType type, const std::string& user_id,
                    const std::string& thread_id, int thread_type,
                    cpp_int bet_amount, const std::string& choice = "");

    // Xử lý input cho game đang hoạt động
    bool handle_input(const std::string& user_id, const std::string& input,
                      const std::string& thread_id);

    // Truy vấn
    bool has_active_game(const std::string& user_id);
    bool has_active_game_in_thread(const std::string& thread_id);

    // Hủy game
    void cancel_game(const std::string& user_id);

    // Cờ caro
    void create_caro_game(const std::string& p1_id, const std::string& p2_id,
                          const std::string& thread_id, int thread_type,
                          cpp_int bet_amount);
    bool caro_move(const std::string& user_id, int row, int col);
    bool check_caro_win(const std::string& thread_id, int row, int col, char player);
    std::string format_board(const CaroBoard& board);

    // Nối từ
    void start_word_chain(const std::string& user_id, const std::string& thread_id,
                          int thread_type, cpp_int bet_amount);
    bool check_word_chain(const std::string& word, const std::string& last_word);

    // Truy cập map (cho CommandHandler / main)
    const std::map<std::string, GameState>& get_active_games() const { return active_games; }
    const std::map<std::string, CaroBoard>& get_caro_games() const { return caro_games; }

    // Chan-le jackpot + history
    cpp_int get_chanle_jackpot() const { return chanle_jackpot_; }
    void set_chanle_jackpot(cpp_int v) { chanle_jackpot_ = v; if (chanle_jackpot_ < 1000000) chanle_jackpot_ = 1000000; }
    void add_chanle_jackpot(cpp_int delta) { chanle_jackpot_ += delta; if (chanle_jackpot_ < 1000000) chanle_jackpot_ = 1000000; }
    const std::vector<bool>& get_chanle_history() const { return chanle_history_; }
    void push_chanle_history(bool won) {
        chanle_history_.push_back(won);
        if (chanle_history_.size() > 20) chanle_history_.erase(chanle_history_.begin());
    }

private:
    EconomyManager& economy;
    std::map<std::string, GameState> active_games;   // user_id -> game
    std::map<std::string, CaroBoard> caro_games;     // thread_id -> board
    std::map<std::string, std::string> word_chain_last; // thread_id -> last word

    std::mt19937 rng;

    cpp_int chanle_jackpot_ = 1000000;
    std::vector<bool> chanle_history_;

    int roll_dice(int sides);
    void end_game(const std::string& user_id, bool won, cpp_int payout);
};