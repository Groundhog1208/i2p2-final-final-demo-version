#include <iostream>
#include <sstream>
#include <cstdint>
#include <cstdlib>

#include "./state.hpp"
#include "config.hpp"
#include "../../policy/game_history.hpp"


/*============================================================
 * KP (King-Piece) Evaluation tables
 *
 * Always compiled. Toggled at runtime via use_kp_eval param.
 *============================================================*/

// KP material (10x scale for fine positional granularity)
// Ratios match the OFFICIAL MAX_STEP adjudication table: P=1, R=5, N=3, B=3,
// Q=9, K=0 (standard chess values; rook > minors). Long games (esp. vs weak)
// reach move 100 and are decided by this count, so eval material must agree.
// NOTE: the template's gui/games/minichess_engine.py _material_table currently
// disagrees (minors > rook) — that local tester should be fixed to match.
static const int kp_material[7] = {0, 20, 100, 60, 65, 200, 1000};

// Material-only (simple scale)
static const int simple_material[7] = {0, 2, 6, 7, 8, 20, 100};

// Piece-Square Tables (white perspective, row 0 = opponent's back rank; mirror for black)
static const int pst[6][BOARD_H][BOARD_W] = {
    // Pawn — reward advancement heavily; center files slightly preferred
    {{ 0,  0,  0,  0,  0},   // row 0: promotion rank (already promoted, unreachable)
     {40, 45, 45, 45, 40},   // row 1: one step from promotion
     {14, 17, 21, 17, 14},   // row 2: advanced
     { 4,  6,  9,  6,  4},   // row 3: midboard
     { 0,  2,  3,  2,  0},   // row 4: starting rank
     { 0,  0,  0,  0,  0}},  // row 5: impossible for white pawn

    // Rook — prefer 7th rank, central files, open files rewarded separately
    {{ 6,  6,  8,  6,  6},   // row 0: 7th-rank equivalent
     { 4,  4,  5,  4,  4},   // row 1
     { 0,  0,  2,  0,  0},   // row 2
     { 0,  0,  2,  0,  0},   // row 3
     { 0,  0,  2,  0,  0},   // row 4
     {-2,  0,  2,  0, -2}},  // row 5: back rank

    // Knight — strongly prefer the center of the 6×5 board
    {{-6, -4,  0, -4, -6},
     {-4,  4,  6,  4, -4},
     { 0,  6, 10,  6,  0},
     { 0,  6, 10,  6,  0},
     {-4,  4,  6,  4, -4},
     {-6, -4,  0, -4, -6}},

    // Bishop — prefer long diagonals and central squares
    {{-2,  0,  0,  0, -2},
     { 0,  4,  2,  4,  0},
     { 2,  6,  4,  6,  2},
     { 2,  6,  4,  6,  2},
     { 0,  4,  2,  4,  0},
     {-2,  0,  0,  0, -2}},

    // Queen — flexible; moderate center bonus
    {{-2,  0,  2,  0, -2},
     { 0,  2,  4,  2,  0},
     { 2,  4,  8,  4,  2},
     { 2,  4,  8,  4,  2},
     { 0,  2,  4,  2,  0},
     {-2,  0,  2,  0, -2}},

    // King — strongly prefer back-rank corners; penalize exposure
    {{-8, -8,-10, -8, -8},
     {-6, -6, -8, -6, -6},
     {-4, -4, -6, -4, -4},
     {-4, -4, -6, -4, -4},
     { 4,  6,  0,  6,  4},
     { 8, 10,  4, 10,  8}},
};

// King tropism weights (how strongly each piece threatens the enemy king)
static const int tropism_w[7] = {0, 1, 4, 4, 3, 6, 0};

// Passed-pawn bonus indexed by PST row (row 0 = promotion side)
// A passed pawn with no blocking opponent pawns is worth significantly more.
static const int passed_bonus[BOARD_H] = {0, 60, 30, 14, 4, 0};

// Pawn structure penalties
static const int doubled_pawn_penalty  = 15;  // per extra pawn on same file
static const int isolated_pawn_penalty = 10;  // pawn with no friendly pawn on adjacent files


/*============================================================
 * Rook open-file bonus
 *   fully open file  → +12
 *   half-open file   → +6  (no own pawn, but opponent pawn present)
 *============================================================*/
static int rook_file_bonus(
    int c,
    const char self_board[BOARD_H][BOARD_W],
    const char oppn_board[BOARD_H][BOARD_W]
){
    bool own_pawn = false, opp_pawn = false;
    for(int r = 0; r < BOARD_H; ++r){
        if(self_board[r][c] == 1) own_pawn = true;
        if(oppn_board[r][c] == 1) opp_pawn = true;
    }
    if(!own_pawn && !opp_pawn) return 12;
    if(!own_pawn)              return 6;
    return 0;
}


/*============================================================
 * Fast pseudo-mobility count
 *
 * Counts reachable squares for every piece without calling
 * get_legal_actions(), avoiding the cost of 3× full move
 * generation per leaf-node evaluation.
 *============================================================*/
static int fast_mobility(int player, const char self[BOARD_H][BOARD_W], const char opp[BOARD_H][BOARD_W]){
    static const int kn_dr[8] = { 2, 1,-1,-2,-2,-1, 1, 2};
    static const int kn_dc[8] = { 1, 2, 2, 1,-1,-2,-2,-1};
    static const int ki_dr[8] = { 1, 0,-1, 0, 1, 1,-1,-1};
    static const int ki_dc[8] = { 0, 1, 0,-1, 1,-1, 1,-1};
    static const int sl_dr[8] = { 0, 0, 1,-1, 1, 1,-1,-1};
    static const int sl_dc[8] = { 1,-1, 0, 0, 1,-1, 1,-1};

    int mob = 0;
    for(int r = 0; r < BOARD_H; ++r){
        for(int c = 0; c < BOARD_W; ++c){
            int piece = self[r][c];
            if(!piece) continue;

            switch(piece){
                case 1: { // Pawn
                    int dr = player ? 1 : -1;
                    int nr = r + dr;
                    if(nr >= 0 && nr < BOARD_H){
                        if(!self[nr][c] && !opp[nr][c]) mob++;
                        if(c > 0         && opp[nr][c-1]) mob++;
                        if(c < BOARD_W-1 && opp[nr][c+1]) mob++;
                    }
                    break;
                }
                case 3: // Knight
                    for(int d = 0; d < 8; ++d){
                        int nr = r+kn_dr[d], nc = c+kn_dc[d];
                        if(nr>=0 && nr<BOARD_H && nc>=0 && nc<BOARD_W && !self[nr][nc]) mob++;
                    }
                    break;
                case 6: // King
                    for(int d = 0; d < 8; ++d){
                        int nr = r+ki_dr[d], nc = c+ki_dc[d];
                        if(nr>=0 && nr<BOARD_H && nc>=0 && nc<BOARD_W && !self[nr][nc]) mob++;
                    }
                    break;
                case 2: // Rook
                case 4: // Bishop
                case 5: { // Queen
                    int ds = (piece==4)?4:0, de = (piece==2)?4:8;
                    for(int d = ds; d < de; ++d){
                        int cr = r+sl_dr[d], cc = c+sl_dc[d];
                        while(cr>=0 && cr<BOARD_H && cc>=0 && cc<BOARD_W){
                            if(self[cr][cc]) break;
                            mob++;
                            if(opp[cr][cc]) break;
                            cr+=sl_dr[d]; cc+=sl_dc[d];
                        }
                    }
                    break;
                }
            }
        }
    }
    return mob;
}

static int king_tropism(
    int piece_type,
    int pr, int pc,
    int ekr, int ekc
){
    int dist = std::max(std::abs(pr - ekr), std::abs(pc - ekc));
    if(dist <= 2){
        return tropism_w[piece_type] * (3 - dist);
    }
    return 0;
}


/*============================================================
 * is_king_attacked() — check if current player's king is
 * attacked by any opponent piece. Used for check extensions.
 *============================================================*/
bool State::is_king_attacked() const {
    const auto& self = board.board[player];
    const auto& opp  = board.board[1 - player];

    // Find our king
    int kr = -1, kc = -1;
    for (int r = 0; r < BOARD_H && kr < 0; ++r)
        for (int c = 0; c < BOARD_W && kr < 0; ++c)
            if (self[r][c] == 6) { kr = r; kc = c; }
    if (kr < 0) return false;

    // Opponent pawn attacks: opp pawn at (kr - opp_dir, kc±1) hits our king
    int opp_dir = (1 - player) ? 1 : -1;
    int pr = kr - opp_dir;
    if (pr >= 0 && pr < BOARD_H) {
        if (kc > 0          && opp[pr][kc - 1] == 1) return true;
        if (kc < BOARD_W-1  && opp[pr][kc + 1] == 1) return true;
    }

    // Knight attacks
    static const int kn_dr[8] = { 2, 1,-1,-2,-2,-1, 1, 2};
    static const int kn_dc[8] = { 1, 2, 2, 1,-1,-2,-2,-1};
    for (int d = 0; d < 8; ++d) {
        int nr = kr + kn_dr[d], nc = kc + kn_dc[d];
        if (nr>=0&&nr<BOARD_H&&nc>=0&&nc<BOARD_W&&opp[nr][nc]==3) return true;
    }

    // Sliding pieces (rook, bishop, queen) + adjacent king
    static const int sl_dr[8] = { 0, 0, 1,-1, 1, 1,-1,-1};
    static const int sl_dc[8] = { 1,-1, 0, 0, 1,-1, 1,-1};
    for (int d = 0; d < 8; ++d) {
        bool diag = (d >= 4);
        int cr = kr + sl_dr[d], cc = kc + sl_dc[d];
        bool adjacent = true;
        while (cr>=0&&cr<BOARD_H&&cc>=0&&cc<BOARD_W) {
            int op = opp[cr][cc];
            if (op) {
                if (op == 5) return true;               // queen: all directions
                if (!diag && op == 2) return true;      // rook: cardinal
                if (diag  && op == 4) return true;      // bishop: diagonal
                if (adjacent && op == 6) return true;   // king: one step only
                break;
            }
            if (self[cr][cc]) break;  // own piece blocks
            cr += sl_dr[d]; cc += sl_dc[d];
            adjacent = false;
        }
    }
    return false;
}


/*============================================================
 * evaluate() — runtime-selectable eval strategy
 *============================================================*/

int State::evaluate(
    bool use_kp_eval,
    bool use_mobility,
    const GameHistory* history
){
    (void)history; // just to suppress warning

    // [ Hackathon TODO 1-1 ]
    // if in win state, return max score(you can check base_state.hpp for max score)
    if (this->game_state == WIN) {
        // MAX_SCORE is usually defined in base_state.hpp, fallback to 100000 if not found
        return P_MAX;
    }

    auto self_board = this->board.board[this->player];
    auto oppn_board = this->board.board[1 - this->player];
    int self_score = 0, oppn_score = 0;

    if(use_kp_eval){
        /* === KP eval: material + PST + tropism === */

        int self_kr = -1, self_kc = -1;
        int oppn_kr = -1, oppn_kc = -1;
        // [ Hackathon TODO 1-3 ]
        // get the position for player's king and opponent's king
        for(int i=0; i<BOARD_H; ++i){
            for(int j=0; j<BOARD_W; ++j){
                if(self_board[i][j] == 6){ self_kr = i; self_kc = j; }
                if(oppn_board[i][j] == 6){ oppn_kr = i; oppn_kc = j; }
            }
        }

        // Sum material + PST (all pieces incl. king) + tropism + passed-pawn + rook open-file
        // Player
        for(int i=0; i<BOARD_H; ++i){
            for(int j=0; j<BOARD_W; ++j){
                int piece = self_board[i][j];
                if(!piece) continue;

                self_score += kp_material[piece];

                int pst_row = this->player ? (BOARD_H-1-i) : i;
                self_score += pst[piece-1][pst_row][j];

                // Passed pawn bonus
                if(piece == 1){
                    bool blocked = false;
                    for(int fc = std::max(0,j-1); fc <= std::min(BOARD_W-1,j+1) && !blocked; ++fc){
                        int rs = this->player ? i+1 : 0;
                        int re = this->player ? BOARD_H : i;
                        for(int fr = rs; fr < re; ++fr)
                            if(oppn_board[fr][fc]==1){ blocked=true; break; }
                    }
                    if(!blocked) self_score += passed_bonus[pst_row];
                }

                // Rook open-file bonus
                if(piece == 2){
                    self_score += rook_file_bonus(j, self_board, oppn_board);
                }

                if(oppn_kr != -1 && oppn_kc != -1)
                    self_score += king_tropism(piece, i, j, oppn_kr, oppn_kc);
            }
        }
        // Opponent
        for(int i=0; i<BOARD_H; ++i){
            for(int j=0; j<BOARD_W; ++j){
                int piece = oppn_board[i][j];
                if(!piece) continue;

                oppn_score += kp_material[piece];

                int pst_row = (1-this->player) ? (BOARD_H-1-i) : i;
                oppn_score += pst[piece-1][pst_row][j];

                // Passed pawn bonus
                if(piece == 1){
                    bool blocked = false;
                    for(int fc = std::max(0,j-1); fc <= std::min(BOARD_W-1,j+1) && !blocked; ++fc){
                        int rs = (1-this->player) ? i+1 : 0;
                        int re = (1-this->player) ? BOARD_H : i;
                        for(int fr = rs; fr < re; ++fr)
                            if(self_board[fr][fc]==1){ blocked=true; break; }
                    }
                    if(!blocked) oppn_score += passed_bonus[pst_row];
                }

                // Rook open-file bonus
                if(piece == 2){
                    oppn_score += rook_file_bonus(j, oppn_board, self_board);
                }

                if(self_kr != -1 && self_kc != -1)
                    oppn_score += king_tropism(piece, i, j, self_kr, self_kc);
            }
        }

    }else{
        /* === Simple material-only eval === */

        // [ Hackathon TODO 1-2 ]
        // Simply add each piece's value to score
        for(int i=0; i<BOARD_H; ++i){
            for(int j=0; j<BOARD_W; ++j){
                int piece = self_board[i][j];
                if(piece) self_score += simple_material[piece];
            }
        }
        for(int i=0; i<BOARD_H; ++i){
            for(int j=0; j<BOARD_W; ++j){
                int piece = oppn_board[i][j];
                if(piece) oppn_score += simple_material[piece];
            }
        }
    }

    /* === Pawn structure: doubled and isolated penalties === */
    if(use_kp_eval){
        for(int player = 0; player < 2; player++){
            auto& bd = this->board.board[player];
            int pawn_count[BOARD_W] = {};
            for(int i = 0; i < BOARD_H; ++i)
                for(int j = 0; j < BOARD_W; ++j)
                    if(bd[i][j] == 1) pawn_count[j]++;

            int penalty = 0;
            for(int j = 0; j < BOARD_W; ++j){
                if(pawn_count[j] > 1)
                    penalty += (pawn_count[j] - 1) * doubled_pawn_penalty;
                if(pawn_count[j] > 0){
                    bool isolated = (j == 0           || pawn_count[j-1] == 0) &&
                                    (j == BOARD_W - 1 || pawn_count[j+1] == 0);
                    if(isolated) penalty += isolated_pawn_penalty;
                }
            }
            if(player == this->player) self_score  -= penalty;
            else                       oppn_score  -= penalty;
        }
    }

    int bonus = 0;

    /* === Mobility bonus (fast pseudo-mobility, no move generation) === */
    if(use_mobility){
        int self_mobility = fast_mobility(
            this->player, self_board, oppn_board);
        int oppn_mobility = fast_mobility(
            1-this->player, oppn_board, self_board);
        bonus += 3 * (self_mobility - oppn_mobility);
    }

    return self_score - oppn_score + bonus;
}



/*============================================================
 * Zobrist hash for transposition table
 *============================================================*/
static uint64_t zobrist_piece[2][7][BOARD_H][BOARD_W];
static uint64_t zobrist_side;
static bool zobrist_ready = false;

static void init_zobrist(){
    uint64_t s = 0x7A35C9D1E4F02B68ULL;
    auto rand64 = [&s]() -> uint64_t {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s;
    };
    for(int p = 0; p < 2; p++){
        for(int t = 0; t < 7; t++){
            for(int r = 0; r < BOARD_H; r++){
                for(int c = 0; c < BOARD_W; c++){
                    zobrist_piece[p][t][r][c] = rand64();
                }
            }
        }
    }
    zobrist_side = rand64();
    zobrist_ready = true;
}

uint64_t State::compute_hash_full() const{
    if(!zobrist_ready){
        init_zobrist();
    }
    uint64_t h = 0;
    for(int p = 0; p < 2; p++){
        for(int r = 0; r < BOARD_H; r++){
            for(int c = 0; c < BOARD_W; c++){
                int piece = this->board.board[p][r][c];
                if(piece){
                    h ^= zobrist_piece[p][piece][r][c];
                }
            }
        }
    }
    if(this->player){
        h ^= zobrist_side;
    }
    return h;
}


/**
 * @brief return next state after the move
 *
 * @param move
 * @return State*
 */
State* State::next_state(const Move& move){
    if(!zobrist_ready){ init_zobrist(); }

    Board next = this->board;
    Point from = move.first, to = move.second;
    int p = this->player;
    int opp = 1 - p;

    int8_t orig_piece = next.board[p][from.first][from.second];
    int8_t moved = orig_piece;
    //promotion for pawn
    if(moved == 1 && (to.first==BOARD_H-1 || to.first==0)){
        moved = 5;
    }

    /* Incremental hash update */
    uint64_t h = this->hash();
    h ^= zobrist_side;  /* toggle side to move */

    /* XOR out piece from source */
    h ^= zobrist_piece[p][orig_piece][from.first][from.second];

    /* XOR out captured piece at destination */
    int8_t captured = next.board[opp][to.first][to.second];
    if(captured){
        h ^= zobrist_piece[opp][captured][to.first][to.second];
        next.board[opp][to.first][to.second] = 0;
    }

    /* XOR in piece at destination */
    h ^= zobrist_piece[p][moved][to.first][to.second];

    next.board[p][from.first][from.second] = 0;
    next.board[p][to.first][to.second] = moved;

    State* ns = new State(next, opp);
    ns->zobrist_hash = h;
    ns->zobrist_valid = true;
    return ns;
}


static const int move_table_rook_bishop[8][7][2] = {
  {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}},
  {{0, -1}, {0, -2}, {0, -3}, {0, -4}, {0, -5}, {0, -6}, {0, -7}},
  {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}},
  {{-1, 0}, {-2, 0}, {-3, 0}, {-4, 0}, {-5, 0}, {-6, 0}, {-7, 0}},
  {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}},
  {{1, -1}, {2, -2}, {3, -3}, {4, -4}, {5, -5}, {6, -6}, {7, -7}},
  {{-1, 1}, {-2, 2}, {-3, 3}, {-4, 4}, {-5, 5}, {-6, 6}, {-7, 7}},
  {{-1, -1}, {-2, -2}, {-3, -3}, {-4, -4}, {-5, -5}, {-6, -6}, {-7, -7}},
};

// [ Hackathon TODO 2-1 ]
// fill the knight move table
static const int move_table_knight[8][2] = {
    {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
    {-2, -1}, {-1, -2}, {1, -2}, {2, -1}
};
static const int move_table_king[8][2] = {
  {1, 0}, {0, 1}, {-1, 0}, {0, -1}, 
  {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
};


/*============================================================
 * Naive move generation (array-based, branch-heavy)
 *============================================================*/
void State::get_legal_actions_naive(){
    this->game_state = NONE;
    std::vector<Move> all_actions;
    all_actions.reserve(64);
    auto self_board = this->board.board[this->player];
    auto oppn_board = this->board.board[1 - this->player];

    int now_piece, oppn_piece;
    for(int i=0; i<BOARD_H; i+=1){
        for(int j=0; j<BOARD_W; j+=1){
            if((now_piece=self_board[i][j])){
                switch(now_piece){
                    case 1: //pawn
                        if(this->player && i<BOARD_H-1){
                            //black
                            if(!oppn_board[i+1][j] && !self_board[i+1][j]){
                                all_actions.push_back(Move(Point(i, j), Point(i+1, j)));
                            }
                            if(j<BOARD_W-1 && (oppn_piece=oppn_board[i+1][j+1])>0){
                                all_actions.push_back(Move(Point(i, j), Point(i+1, j+1)));
                                if(oppn_piece==6){
                                    this->game_state = WIN;
                                    this->legal_actions = all_actions;
                                    return;
                                }
                            }
                            if(j>0 && (oppn_piece=oppn_board[i+1][j-1])>0){
                                all_actions.push_back(Move(Point(i, j), Point(i+1, j-1)));
                                if(oppn_piece==6){
                                    this->game_state = WIN;
                                    this->legal_actions = all_actions;
                                    return;
                                }
                            }
                        }else if(!this->player && i>0){
                            //white
                            if(!oppn_board[i-1][j] && !self_board[i-1][j]){
                                all_actions.push_back(Move(Point(i, j), Point(i-1, j)));
                            }
                            if(j<BOARD_W-1 && (oppn_piece=oppn_board[i-1][j+1])>0){
                                all_actions.push_back(Move(Point(i, j), Point(i-1, j+1)));
                                if(oppn_piece==6){
                                    this->game_state = WIN;
                                    this->legal_actions = all_actions;
                                    return;
                                }
                            }
                            if(j>0 && (oppn_piece=oppn_board[i-1][j-1])>0){
                                all_actions.push_back(Move(Point(i, j), Point(i-1, j-1)));
                                if(oppn_piece==6){
                                    this->game_state = WIN;
                                    this->legal_actions = all_actions;
                                    return;
                                }
                            }
                        }
                        break;

                    case 2: //rook
                    case 4: //bishop
                    case 5: //queen
                        int st, end;
                        switch(now_piece){
                            case 2: st=0; end=4; break; //rook
                            case 4: st=4; end=8; break; //bishop
                            case 5: st=0; end=8; break; //queen
                            default: st=0; end=-1;
                        }
                        for(int part=st; part<end; part+=1){
                            auto move_list = move_table_rook_bishop[part];
                            for(int k=0; k<std::max(BOARD_H, BOARD_W); k+=1){
                                int p[2] = {move_list[k][0] + i, move_list[k][1] + j};

                                if(p[0]>=BOARD_H || p[0]<0 || p[1]>=BOARD_W || p[1]<0){
                                    break;
                                }
                                now_piece = self_board[p[0]][p[1]];
                                if(now_piece){
                                    break;
                                }

                                all_actions.push_back(Move(Point(i, j), Point(p[0], p[1])));

                                oppn_piece = oppn_board[p[0]][p[1]];
                                if(oppn_piece){
                                    if(oppn_piece==6){
                                        this->game_state = WIN;
                                        this->legal_actions = all_actions;
                                        return;
                                    }else{
                                        break;
                                    }
                                };
                            }
                        }
                        break;

                    case 3: //knight
                    // [ Hackathon TODO 2-2 ]
                        for(auto move: move_table_knight){
                            int p[2] = {move[0] + i, move[1] + j};
                            if(p[0]>=BOARD_H || p[0]<0 || p[1]>=BOARD_W || p[1]<0){
                                continue;
                            }
                            now_piece = self_board[p[0]][p[1]];
                            if(now_piece){
                                continue;
                            }
                            all_actions.push_back(Move(Point(i, j), Point(p[0], p[1])));
                            oppn_piece = oppn_board[p[0]][p[1]];
                            if(oppn_piece==6){
                                this->game_state = WIN;
                                this->legal_actions = all_actions;
                                return;
                            }
                        }
                        break;

                    case 6: //king
                        for(auto move: move_table_king){
                            int p[2] = {move[0] + i, move[1] + j};

                            if(p[0]>=BOARD_H || p[0]<0 || p[1]>=BOARD_W || p[1]<0){
                                continue;
                            }
                            now_piece = self_board[p[0]][p[1]];
                            if(now_piece){
                                continue;
                            }

                            all_actions.push_back(Move(Point(i, j), Point(p[0], p[1])));

                            oppn_piece = oppn_board[p[0]][p[1]];
                            if(oppn_piece==6){
                                this->game_state = WIN;
                                this->legal_actions = all_actions;
                                return;
                            }
                        }
                        break;
                }
            }
        }
    }
    this->legal_actions = all_actions;
}


/*============================================================
 * Bitboard move generation
 *
 * 6x5 = 30 squares fit in a uint32_t.
 * Square (r,c) -> bit index r*5+c.
 * Precomputed attack masks for leapers (knight, king, pawn).
 * Bit-scan loop (__builtin_ctz) replaces nested array iteration.
 *============================================================*/
#define BB_SQ(r, c)  ((r) * BOARD_W + (c))
#define BB_ROW(sq)   ((sq) / BOARD_W)
#define BB_COL(sq)   ((sq) % BOARD_W)

// Precomputed attack tables (initialized once)
static uint32_t bb_knight[30];       // knight attack mask per square
static uint32_t bb_king[30];         // king attack mask per square
static uint32_t bb_pawn_push[2][30]; // pawn push target per player/square
static uint32_t bb_pawn_cap[2][30];  // pawn capture targets per player/square
static bool bb_ready = false;

// Sliding piece direction vectors (0-3: rook, 4-7: bishop, 0-7: queen)
static const int bb_dr[8] = {0, 0, 1, -1, 1, 1, -1, -1};
static const int bb_dc[8] = {1, -1, 0, 0, 1, -1, 1, -1};

static void bb_init(){
    static const int kn_dr[8] = {1, 1, -1, -1, 2, 2, -2, -2};
    static const int kn_dc[8] = {2, -2, 2, -2, 1, -1, 1, -1};
    static const int ki_dr[8] = {1, 0, -1, 0, 1, 1, -1, -1};
    static const int ki_dc[8] = {0, 1, 0, -1, 1, -1, 1, -1};

    for(int r = 0; r < BOARD_H; r++){
        for(int c = 0; c < BOARD_W; c++){
            int sq = BB_SQ(r, c);

            // Knight
            bb_knight[sq] = 0;
            for(int d = 0; d < 8; d++){
                int nr = r + kn_dr[d], nc = c + kn_dc[d];
                if(nr >= 0 && nr < BOARD_H && nc >= 0 && nc < BOARD_W){
                    bb_knight[sq] |= 1u << BB_SQ(nr, nc);
                }
            }

            // King
            bb_king[sq] = 0;
            for(int d = 0; d < 8; d++){
                int nr = r + ki_dr[d], nc = c + ki_dc[d];
                if(nr >= 0 && nr < BOARD_H && nc >= 0 && nc < BOARD_W){
                    bb_king[sq] |= 1u << BB_SQ(nr, nc);
                }
            }

            // Pawn (player 0 = white, advances up = row-1)
            bb_pawn_push[0][sq] = 0;
            bb_pawn_cap[0][sq] = 0;
            if(r > 0){
                bb_pawn_push[0][sq] = 1u << BB_SQ(r-1, c);
                if(c > 0){
                    bb_pawn_cap[0][sq] |= 1u << BB_SQ(r-1, c-1);
                }
                if(c < BOARD_W-1){
                    bb_pawn_cap[0][sq] |= 1u << BB_SQ(r-1, c+1);
                }
            }

            // Pawn (player 1 = black, advances down = row+1)
            bb_pawn_push[1][sq] = 0;
            bb_pawn_cap[1][sq] = 0;
            if(r < BOARD_H-1){
                bb_pawn_push[1][sq] = 1u << BB_SQ(r+1, c);
                if(c > 0){
                    bb_pawn_cap[1][sq] |= 1u << BB_SQ(r+1, c-1);
                }
                if(c < BOARD_W-1){
                    bb_pawn_cap[1][sq] |= 1u << BB_SQ(r+1, c+1);
                }
            }
        }
    }
    bb_ready = true;
}

void State::get_legal_actions_bitboard(){
    if(!bb_ready){
        bb_init();
    }

    this->game_state = NONE;
    this->legal_actions.clear();
    this->legal_actions.reserve(64);

    int self = this->player;
    int oppn = 1 - self;

    // Build occupancy bitmasks and piece-type lookup
    uint32_t self_occ = 0, oppn_occ = 0;
    int self_pt[30] = {};  // piece type at each square (self)
    int oppn_pt[30] = {};  // piece type at each square (opponent)

    for(int r = 0; r < BOARD_H; r++){
        for(int c = 0; c < BOARD_W; c++){
            int sq = BB_SQ(r, c);
            if(this->board.board[self][r][c]){
                self_occ |= 1u << sq;
                self_pt[sq] = this->board.board[self][r][c];
            }
            if(this->board.board[oppn][r][c]){
                oppn_occ |= 1u << sq;
                oppn_pt[sq] = this->board.board[oppn][r][c];
            }
        }
    }

    uint32_t all_occ = self_occ | oppn_occ;

    // Iterate own pieces via bit scan
    uint32_t pieces = self_occ;
    while(pieces){
        int sq = __builtin_ctz(pieces);
        pieces &= pieces - 1;
        int r = BB_ROW(sq), c = BB_COL(sq);
        int piece = self_pt[sq];
        uint32_t targets = 0;

        switch(piece){
            case 1: { // Pawn
                uint32_t push = bb_pawn_push[self][sq] & ~all_occ;
                uint32_t cap = bb_pawn_cap[self][sq] & oppn_occ;
                // Check for king capture in captures
                uint32_t cap_scan = cap;
                while(cap_scan){
                    int to = __builtin_ctz(cap_scan);
                    cap_scan &= cap_scan - 1;
                    if(oppn_pt[to] == 6){
                        this->game_state = WIN;
                        this->legal_actions.push_back(
                            Move(Point(r, c), Point(BB_ROW(to), BB_COL(to))));
                        return;
                    }
                }
                targets = push | cap;
                break;
            }

            case 3: { // Knight
                targets = bb_knight[sq] & ~self_occ;
                uint32_t opp_targets = targets & oppn_occ;
                while(opp_targets){
                    int to = __builtin_ctz(opp_targets);
                    opp_targets &= opp_targets - 1;
                    if(oppn_pt[to] == 6){
                        this->game_state = WIN;
                        this->legal_actions.push_back(
                            Move(Point(r, c), Point(BB_ROW(to), BB_COL(to))));
                        return;
                    }
                }
                break;
            }

            case 6: { // King
                targets = bb_king[sq] & ~self_occ;
                uint32_t opp_targets = targets & oppn_occ;
                while(opp_targets){
                    int to = __builtin_ctz(opp_targets);
                    opp_targets &= opp_targets - 1;
                    if(oppn_pt[to] == 6){
                        this->game_state = WIN;
                        this->legal_actions.push_back(
                            Move(Point(r, c), Point(BB_ROW(to), BB_COL(to))));
                        return;
                    }
                }
                break;
            }

            case 2: // Rook
            case 4: // Bishop
            case 5: { // Queen
                int d_start = (piece == 4) ? 4 : 0;
                int d_end   = (piece == 2) ? 4 : 8;
                for(int d = d_start; d < d_end; d++){
                    int cr = r + bb_dr[d], cc = c + bb_dc[d];
                    while(cr >= 0 && cr < BOARD_H && cc >= 0 && cc < BOARD_W){
                        int to = BB_SQ(cr, cc);
                        uint32_t to_bit = 1u << to;
                        if(self_occ & to_bit){
                            break; // own piece blocks
                        }

                        if((oppn_occ & to_bit) && oppn_pt[to] == 6){
                            this->game_state = WIN;
                            this->legal_actions.push_back(
                                Move(Point(r, c), Point(cr, cc)));
                            return;
                        }

                        targets |= to_bit;
                        if(oppn_occ & to_bit){
                            break; // captured, stop sliding
                        }
                        cr += bb_dr[d]; cc += bb_dc[d];
                    }
                }
                break;
            }
        }

        // Convert target bitmask to Move objects
        while(targets){
            int to = __builtin_ctz(targets);
            targets &= targets - 1;
            this->legal_actions.push_back(
                Move(Point(r, c), Point(BB_ROW(to), BB_COL(to))));
        }
    }
}


/*============================================================
 * Dispatcher
 *============================================================*/
void State::get_legal_actions(){
    #ifdef USE_BITBOARD
    get_legal_actions_bitboard();
    #else
    get_legal_actions_naive();
    #endif
}


const char piece_table[2][7][5] = {
  {" ", "♙", "♖", "♘", "♗", "♕", "♔"},
  {" ", "♟", "♜", "♞", "♝", "♛", "♚"}
};
/**
 * @brief encode the output for command line output
 * 
 * @return std::string 
 */
std::string State::encode_output() const{
    std::stringstream ss;
    int now_piece;
    for(int i=0; i<BOARD_H; i+=1){
        for(int j=0; j<BOARD_W; j+=1){
            if((now_piece = this->board.board[0][i][j])){
                ss << std::string(piece_table[0][now_piece]);
            }else if((now_piece = this->board.board[1][i][j])){
                ss << std::string(piece_table[1][now_piece]);
            }else{
                ss << " ";
            }
            ss << " ";
        }
        ss << "\n";
    }
    return ss.str();
}


/**
 * @brief encode the state to the format for player
 * 
 * @return std::string 
 */
std::string State::encode_state(){
    std::stringstream ss;
    ss << this->player;
    ss << "\n";
    for(int pl=0; pl<2; pl+=1){
        for(int i=0; i<BOARD_H; i+=1){
            for(int j=0; j<BOARD_W; j+=1){
                ss << int(this->board.board[pl][i][j]);
                ss << " ";
            }
            ss << "\n";
        }
        ss << "\n";
    }
    return ss.str();
}


BaseState* State::create_null_state() const{
    State* s = new State(this->board, 1 - this->player);
    s->get_legal_actions();
    return s;
}


/* === Board serialization === */
static const char* piece_chars = ".PRNBQK";
static const char* piece_chars_lower = ".prnbqk";

std::string State::encode_board() const{
    std::string s;
    for(int r = 0; r < BOARD_H; r++){
        if(r > 0){
            s += '/';
        }
        for(int c = 0; c < BOARD_W; c++){
            int w = board.board[0][r][c];
            int b = board.board[1][r][c];
            if(w > 0 && w <= 6){
                s += piece_chars[w];
            }else if(b > 0 && b <= 6){
                s += piece_chars_lower[b];
            }else{
                s += '.';
            }
        }
    }
    return s;
}

void State::decode_board(const std::string& s, int side_to_move){
    player = side_to_move;
    game_state = UNKNOWN;
    zobrist_valid = false;
    board = Board{};
    int r = 0, c = 0;
    for(char ch : s){
        if(ch == '/'){
            r++;
            c = 0;
            continue;
        }
        if(r >= BOARD_H || c >= BOARD_W){
            break;
        }
        if(ch >= 'A' && ch <= 'Z'){
            for(int p = 1; p <= 6; p++){
                if(piece_chars[p] == ch){
                    board.board[0][r][c] = p;
                    break;
                }
            }
        }else if(ch >= 'a' && ch <= 'z'){
            for(int p = 1; p <= 6; p++){
                if(piece_chars_lower[p] == ch){
                    board.board[1][r][c] = p;
                    break;
                }
            }
        }
        c++;
    }
    get_legal_actions();
}


/* (Zobrist tables moved above next_state) */


/*============================================================
 * Cell display for protocol (d command)
 *============================================================*/
std::string State::cell_display(int row, int col) const{
    int w = static_cast<int>(board.board[0][row][col]);
    int b = static_cast<int>(board.board[1][row][col]);
    if(w){
        const char* names = ".PRNBQK";
        return std::string(" ") + names[w] + " ";
    }else if(b){
        const char* names = ".prnbqk";
        return std::string(" ") + names[b] + " ";
    }else{
        return " . ";
    }
}

/* === Repetition: chess 3-fold rule === */
bool State::check_repetition(const GameHistory& history, int& out_score) const {
    if(history.count(hash()) >= 3){
        out_score = 0;  /* draw */
        return true;
    }
    return false;
}
