#include "GameManager.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

GameManager::GameManager(EconomyManager& economy)
    : economy(economy), rng(std::random_device{}()) {}

// ============================================================
// Helper
// ============================================================

int GameManager::roll_dice(int sides) {
    if (sides <= 0) return 0;
    std::uniform_int_distribution<int> dist(1, sides);
    return dist(rng);
}

void GameManager::end_game(const std::string& user_id, bool /*won*/, cpp_int /*payout*/) {
    auto it = active_games.find(user_id);
    if (it != active_games.end()) {
        it->second.is_active = false;
        active_games.erase(it);
    }
}

// ============================================================
// Khởi tạo game
// ============================================================

void GameManager::start_game(GameType type, const std::string& user_id,
                             const std::string& thread_id, int thread_type,
                             cpp_int bet_amount, const std::string& choice) {
    if (has_active_game(user_id)) {
        cancel_game(user_id);
    }

    if (!economy.place_bet(user_id, bet_amount)) {
        return; // không đủ tiền
    }

    GameState state;
    state.type = type;
    state.player_id = user_id;
    state.thread_id = thread_id;
    state.thread_type = thread_type;
    state.bet_amount = bet_amount;
    state.is_active = true;
    state.start_time = std::time(nullptr);

    switch (type) {
        case GameType::CHAN_LE:
            state.game_data["choice"] = choice;
            break;
        case GameType::TAI_XIU:
            state.game_data["choice"] = choice;
            break;
        case GameType::BAU_CUA:
            state.game_data["choice"] = choice;
            break;
        case GameType::DOAN_SO:
            state.game_data["secret"] = std::to_string(roll_dice(100));
            state.game_data["attempts"] = "0";
            break;
        case GameType::NOI_TU:
            state.game_data["last_word"] = "";
            break;
        case GameType::CO_CA_RO:
            break; // xử lý riêng bằng create_caro_game
    }

    active_games[user_id] = state;
}

// ============================================================
// Xử lý input cho game đang chạy
// ============================================================

bool GameManager::handle_input(const std::string& user_id, const std::string& input,
                               const std::string& /*thread_id*/) {
    auto it = active_games.find(user_id);
    if (it == active_games.end() || !it->second.is_active) return false;

    GameState& state = it->second;
    bool won = false;
    cpp_int payout = 0;
    std::string result_message;

    switch (state.type) {

        // -------------------- CHẴN LẺ --------------------
        case GameType::CHAN_LE: {
            int dice = roll_dice(6);
            bool is_even = (dice % 2 == 0);
            bool bet_even = (state.game_data["choice"] == "chan");

            if ((is_even && bet_even) || (!is_even && !bet_even)) {
                won = true;
                payout = state.bet_amount * 2;
            }

            if (won) economy.subtract_from_husk("chan_le", state.bet_amount);
            else     economy.add_to_husk("chan_le", state.bet_amount);

            result_message = "🎲 Xúc xắc: " + std::to_string(dice) +
                             (is_even ? " (CHẴN)" : " (LẺ)") +
                             (won ? "\n🎉 Bạn THẮNG! +" + payout.convert_to<std::string>()
                                  : "\n😢 Bạn THUA! -" + state.bet_amount.convert_to<std::string>());
            break;
        }

        // -------------------- TÀI XỈU --------------------
        case GameType::TAI_XIU: {
            int d1 = roll_dice(6), d2 = roll_dice(6), d3 = roll_dice(6);
            int total = d1 + d2 + d3;
            bool is_tai = (total >= 11);
            bool bet_tai = (state.game_data["choice"] == "tai");

            if ((is_tai && bet_tai) || (!is_tai && !bet_tai)) {
                won = true;
                payout = state.bet_amount * 2;
            }

            if (won) economy.subtract_from_husk("tai_xiu", state.bet_amount);
            else     economy.add_to_husk("tai_xiu", state.bet_amount);

            result_message = "🎲 Xúc xắc: " + std::to_string(d1) + "-" +
                             std::to_string(d2) + "-" + std::to_string(d3) +
                             " = " + std::to_string(total) +
                             (is_tai ? " (TÀI)" : " (XỈU)") +
                             (won ? "\n🎉 Bạn THẮNG! +" + payout.convert_to<std::string>()
                                  : "\n😢 Bạn THUA! -" + state.bet_amount.convert_to<std::string>());
            break;
        }

        // -------------------- BẦU CUA --------------------
        case GameType::BAU_CUA: {
            const std::vector<std::string> items = {"bau", "cua", "tom", "ca", "ga", "nua"};
            const std::vector<std::string> names = {"Bầu", "Cua", "Tôm", "Cá", "Gà", "Nai"};

            int r1 = roll_dice(6) - 1;
            int r2 = roll_dice(6) - 1;
            int r3 = roll_dice(6) - 1;

            std::string player_choice = state.game_data["choice"];
            int count = 0;
            if (items[r1] == player_choice) count++;
            if (items[r2] == player_choice) count++;
            if (items[r3] == player_choice) count++;

            if (count > 0) {
                won = true;
                payout = state.bet_amount * (count + 1);
            }

            if (won) economy.subtract_from_husk("bau_cua", state.bet_amount);
            else     economy.add_to_husk("bau_cua", state.bet_amount);

            result_message = "🎲 Kết quả: " + names[r1] + " | " + names[r2] + " | " + names[r3] +
                             "\nBạn chọn: " + player_choice +
                             "\nTrúng " + std::to_string(count) + " lần." +
                             (won ? "\n🎉 Bạn THẮNG! +" + payout.convert_to<std::string>()
                                  : "\n😢 Bạn THUA! -" + state.bet_amount.convert_to<std::string>());
            break;
        }

        // -------------------- ĐOÁN SỐ --------------------
        case GameType::DOAN_SO: {
            int secret = 0;
            int guess = 0;
            try {
                secret = std::stoi(state.game_data["secret"]);
                guess = std::stoi(input);
            } catch (...) {
                return true; // input không phải số, không tính lượt
            }

            if (guess < 1 || guess > 100) return true;

            int attempts = std::stoi(state.game_data["attempts"]) + 1;
            state.game_data["attempts"] = std::to_string(attempts);

            if (guess == secret) {
                won = true;
                payout = state.bet_amount * 10;
                result_message = "🎉 CHÍNH XÁC! Số bí mật là " + std::to_string(secret) +
                                 "\nBạn thắng " + payout.convert_to<std::string>() + "!";
            } else if (attempts >= 5) {
                result_message = "😢 Hết lượt! Số bí mật là " + std::to_string(secret) +
                                 "\nBạn thua " + state.bet_amount.convert_to<std::string>() + ".";
                state.game_data["last_message"] = result_message;
                state.is_active = false;
                active_games.erase(it);
                return true;
            } else {
                std::string hint = (guess < secret)
                                    ? "⬆️ Số bí mật LỚN HƠN!"
                                    : "⬇️ Số bí mật NHỎ HƠN!";
                result_message = hint + "\nCòn " + std::to_string(5 - attempts) + " lượt.";
                state.game_data["last_message"] = result_message;
                return true;
            }
            break;
        }

        // -------------------- NỐI TỪ --------------------
        case GameType::NOI_TU: {
            std::string last = state.game_data["last_word"];

            if (!last.empty()) {
                if (input.empty() || input.front() != last.back()) {
                    result_message = "❌ Từ phải bắt đầu bằng '" +
                                     std::string(1, last.back()) + "'!";
                    state.game_data["last_message"] = result_message;
                    return true;
                }
            }

            state.game_data["last_word"] = input;
            result_message = "✅ Chấp nhận: " + input +
                             "\nLượt tiếp theo bắt đầu bằng '" +
                             std::string(1, input.back()) + "'";
            state.game_data["last_message"] = result_message;
            return true;
        }

        // -------------------- CỜ CARO --------------------
        case GameType::CO_CA_RO:
            return false;
    }

    if (won) {
        economy.add_winnings(user_id, payout);
    }

    state.game_data["last_message"] = result_message;
    state.is_active = false;
    active_games.erase(it);
    return true;
}

// ============================================================
// Truy vấn
// ============================================================

bool GameManager::has_active_game(const std::string& user_id) {
    auto it = active_games.find(user_id);
    return it != active_games.end() && it->second.is_active;
}

bool GameManager::has_active_game_in_thread(const std::string& thread_id) {
    for (auto& kv : active_games) {
        if (kv.second.thread_id == thread_id && kv.second.is_active) return true;
    }
    for (auto& kv : caro_games) {
        if (kv.first == thread_id && kv.second.is_active) return true;
    }
    return false;
}

void GameManager::cancel_game(const std::string& user_id) {
    auto it = active_games.find(user_id);
    if (it != active_games.end()) {
        economy.buff(user_id, it->second.bet_amount);
        active_games.erase(it);
    }
}

// ============================================================
// Cờ caro
// ============================================================

void GameManager::create_caro_game(const std::string& p1_id, const std::string& p2_id,
                                   const std::string& thread_id, int /*thread_type*/,
                                   cpp_int bet_amount) {
    CaroBoard board;
    for (int i = 0; i < 15; i++)
        for (int j = 0; j < 15; j++)
            board.board[i][j] = '.';
    board.player1_id = p1_id;
    board.player2_id = p2_id;
    board.current_turn = 1;
    board.is_active = true;
    board.bet_amount = bet_amount;

    caro_games[thread_id] = board;
}

bool GameManager::caro_move(const std::string& user_id, int row, int col) {
    std::string thread_id;
    for (auto& kv : caro_games) {
        if ((kv.second.player1_id == user_id || kv.second.player2_id == user_id) &&
            kv.second.is_active) {
            thread_id = kv.first;
            break;
        }
    }
    if (thread_id.empty()) return false;

    CaroBoard& board = caro_games[thread_id];
    if (!board.is_active) return false;
    if (row < 0 || row >= 15 || col < 0 || col >= 15) return false;
    if (board.board[row][col] != '.') return false;

    char player = (board.current_turn == 1) ? 'X' : 'O';
    std::string current_player = (board.current_turn == 1)
                                    ? board.player1_id : board.player2_id;
    if (current_player != user_id) return false;

    board.board[row][col] = player;

    if (check_caro_win(thread_id, row, col, player)) {
        board.is_active = false;
        economy.add_winnings(user_id, board.bet_amount * 2);
        return true;
    }

    board.current_turn = 3 - board.current_turn;
    return false;
}

bool GameManager::check_caro_win(const std::string& thread_id, int row, int col, char player) {
    auto it = caro_games.find(thread_id);
    if (it == caro_games.end()) return false;
    CaroBoard& board = it->second;

    const int directions[4][2] = {{0,1}, {1,0}, {1,1}, {1,-1}};

    for (auto& dir : directions) {
        int count = 1;
        for (int d = 1; d <= 4; d++) {
            int r = row + dir[0] * d;
            int c = col + dir[1] * d;
            if (r >= 0 && r < 15 && c >= 0 && c < 15 && board.board[r][c] == player) count++;
            else break;
        }
        for (int d = 1; d <= 4; d++) {
            int r = row - dir[0] * d;
            int c = col - dir[1] * d;
            if (r >= 0 && r < 15 && c >= 0 && c < 15 && board.board[r][c] == player) count++;
            else break;
        }
        if (count >= 5) return true;
    }
    return false;
}

std::string GameManager::format_board(const CaroBoard& board) {
    std::stringstream ss;
    ss << "   ";
    for (int i = 0; i < 15; i++) ss << (i % 10) << " ";
    ss << "\n";
    for (int i = 0; i < 15; i++) {
        ss << (i < 10 ? " " : "") << i << " ";
        for (int j = 0; j < 15; j++) {
            ss << board.board[i][j] << " ";
        }
        ss << "\n";
    }
    return ss.str();
}

// ============================================================
// Nối từ
// ============================================================

void GameManager::start_word_chain(const std::string& user_id, const std::string& thread_id,
                                   int thread_type, cpp_int bet_amount) {
    GameState state;
    state.type = GameType::NOI_TU;
    state.player_id = user_id;
    state.thread_id = thread_id;
    state.thread_type = thread_type;
    state.bet_amount = bet_amount;
    state.is_active = true;
    state.start_time = std::time(nullptr);
    state.game_data["last_word"] = "";
    active_games[user_id] = state;
}

bool GameManager::check_word_chain(const std::string& word, const std::string& last_word) {
    if (word.empty()) return false;
    if (last_word.empty()) return true;
    return word.front() == last_word.back();
}