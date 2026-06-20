#include <algorithm>
#include "state.hpp"
#include "minimax.hpp"

TTEntry g_tt[TT_SIZE];

/* ============================================================
 * Killer moves: 2 quiet moves per ply that caused beta cutoffs.
 * Tried after TT move and captures, before other quiet moves.
 * ============================================================ */
static constexpr int MAX_PLY = 128;
static Move g_killers[MAX_PLY][2];


static void store_killer(int ply, const Move& m) {
    if (ply >= MAX_PLY) return;
    if (m == g_killers[ply][0]) return;
    g_killers[ply][1] = g_killers[ply][0];
    g_killers[ply][0] = m;
}

/* ============================================================
 * Move ordering scores:
 *   captures  → MVV-LVA (large positive, best first)
 *   killers   → -1      (just above quiet moves)
 *   quiet     → -10
 * ============================================================ */
// Ratios match the OFFICIAL adjudication table: P=1, R=5, N=3, B=3, Q=9 (rook > minors).
static const int piece_val[7]   = {0,  1,  5,  3,  3,  9, 100};
static const int capture_val[7] = {0, 20, 100, 60, 65, 200, 1000};

using MoveIter = std::vector<Move>::iterator;

static void order_moves(MoveIter begin, MoveIter end, const State* state, int ply = -1) {
    int opp  = 1 - state->player;
    int self = state->player;
    std::stable_sort(begin, end, [&](const Move& a, const Move& b) {
        int av = state->board.board[opp][a.second.first][a.second.second];
        int bv = state->board.board[opp][b.second.first][b.second.second];
        int aa = state->board.board[self][a.first.first][a.first.second];
        int ba = state->board.board[self][b.first.first][b.first.second];
        bool a_kill = (ply >= 0 && ply < MAX_PLY &&
                       (a == g_killers[ply][0] || a == g_killers[ply][1]));
        bool b_kill = (ply >= 0 && ply < MAX_PLY &&
                       (b == g_killers[ply][0] || b == g_killers[ply][1]));
        int as = av ? (piece_val[av]*10 - piece_val[aa]) : (a_kill ? -1 : -10);
        int bs = bv ? (piece_val[bv]*10 - piece_val[ba]) : (b_kill ? -1 : -10);
        return as > bs;
    });
}


/* ============================================================
 * MiniMax — plain negamax, no pruning
 * ============================================================ */
int MiniMax::eval_ctx(
    State* state,
    int depth,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
) {
    ctx.nodes++;
    if (ctx.stop) return 0;
    if (ply > ctx.seldepth) ctx.seldepth = ply;

    if (state->legal_actions.empty() && state->game_state == UNKNOWN)
        state->get_legal_actions();

    if (state->game_state == WIN)  return P_MAX - ply;
    if (state->game_state == DRAW) return 0;

    int rep_score;
    if (state->check_repetition(history, rep_score)) return rep_score;
    history.push(state->hash());

    if (depth <= 0) {
        int score = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        history.pop(state->hash());
        return score;
    }

    int best = M_MAX;
    for (auto& action : state->legal_actions) {
        State* next = state->next_state(action);
        int score = -eval_ctx(next, depth - 1, history, ply + 1, ctx, p);
        delete next;
        if (score > best) best = score;
    }

    history.pop(state->hash());
    return best;
}

SearchResult MiniMax::search(
    State* state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
) {
    ctx.reset();
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if (!state->legal_actions.size())
        state->get_legal_actions();

    int best_score = M_MAX - 10;
    int move_index = 0, total_moves = (int)state->legal_actions.size();

    for (auto& action : state->legal_actions) {
        State* next = state->next_state(action);
        int score = -eval_ctx(next, depth - 1, history, 1, ctx, p);
        delete next;

        if (score > best_score) {
            best_score = score;
            result.best_move = action;
            if (p.report_partial && ctx.on_root_update)
                ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
        }
        move_index++;
    }

    result.score = best_score;
    return result;
}

ParamMap MiniMax::default_params() {
    return {{"UseKPEval","true"},{"UseEvalMobility","true"},{"ReportPartial","true"}};
}
std::vector<ParamDef> MiniMax::param_defs() {
    return {{"UseKPEval",ParamDef::CHECK,"true"},{"UseEvalMobility",ParamDef::CHECK,"true"},{"ReportPartial",ParamDef::CHECK,"true"}};
}


/* ============================================================
 * AlphaBeta — negamax with alpha-beta pruning
 * ============================================================ */
int AlphaBeta::eval_ctx(
    State* state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
) {
    ctx.nodes++;
    if (ctx.stop) return 0;
    if (ply > ctx.seldepth) ctx.seldepth = ply;

    if (state->legal_actions.empty() && state->game_state == UNKNOWN)
        state->get_legal_actions();

    if (state->game_state == WIN)  return P_MAX - ply;
    if (state->game_state == DRAW) return 0;

    int rep_score;
    if (state->check_repetition(history, rep_score)) return rep_score;
    history.push(state->hash());

    if (depth <= 0) {
        int score = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        history.pop(state->hash());
        return score;
    }

    auto moves = state->legal_actions;
    order_moves(moves.begin(), moves.end(), state);

    int best = M_MAX;
    for (auto& action : moves) {
        if (ctx.stop) break;
        State* next = state->next_state(action);
        int score = -eval_ctx(next, depth - 1, -beta, -alpha, history, ply + 1, ctx, p);
        delete next;

        if (score > best)  best  = score;
        if (score > alpha) alpha = score;
        if (alpha >= beta) break;
    }

    history.pop(state->hash());
    return best;
}

SearchResult AlphaBeta::search(
    State* state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
) {
    ctx.reset();
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if (!state->legal_actions.size())
        state->get_legal_actions();

    auto moves = state->legal_actions;
    order_moves(moves.begin(), moves.end(), state);

    int alpha = M_MAX, beta = P_MAX;
    int move_index = 0, total_moves = (int)moves.size();

    for (auto& action : moves) {
        if (ctx.stop) break;
        State* next = state->next_state(action);
        int score = -eval_ctx(next, depth - 1, -beta, -alpha, history, 1, ctx, p);
        delete next;

        if (score > alpha) {
            alpha = score;
            result.best_move = action;
            if (p.report_partial && ctx.on_root_update)
                ctx.on_root_update({result.best_move, alpha, depth, move_index + 1, total_moves});
        }
        move_index++;
    }

    result.score = alpha;
    return result;
}

ParamMap AlphaBeta::default_params() {
    return {{"UseKPEval","true"},{"UseEvalMobility","true"},{"ReportPartial","true"}};
}
std::vector<ParamDef> AlphaBeta::param_defs() {
    return {{"UseKPEval",ParamDef::CHECK,"true"},{"UseEvalMobility",ParamDef::CHECK,"true"},{"ReportPartial",ParamDef::CHECK,"true"}};
}


/* ============================================================
 * PVS — Principal Variation Search + Quiescence + Killers + LMR
 * ============================================================ */
static constexpr int MAX_QDEPTH = 8;

int PVS::quiescence(
    State* state,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p,
    int qdepth
) {
    ctx.nodes++;
    if (ctx.stop) return 0;

    if (state->legal_actions.empty() && state->game_state == UNKNOWN)
        state->get_legal_actions();

    if (state->game_state == WIN)  return P_MAX - ply;
    if (state->game_state == DRAW) return 0;

    int stand_pat = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
    if (stand_pat >= beta)  return beta;
    if (stand_pat > alpha)  alpha = stand_pat;

    if (qdepth >= MAX_QDEPTH) return alpha;

    int opp = 1 - state->player;
    std::vector<Move> captures;
    captures.reserve(16);
    for (auto& m : state->legal_actions)
        if (state->board.board[opp][(int)m.second.first][(int)m.second.second] != 0)
            captures.push_back(m);
    order_moves(captures.begin(), captures.end(), state);

    for (auto& action : captures) {
        if (ctx.stop) break;
        // Delta pruning: skip captures that can't raise alpha even with a margin
        int cap_piece = state->board.board[opp][action.second.first][action.second.second];
        if (stand_pat + capture_val[cap_piece] + 50 < alpha) continue;

        State* next = state->next_state(action);
        int score = -quiescence(next, -beta, -alpha, history, ply + 1, ctx, p, qdepth + 1);
        delete next;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

int PVS::eval_ctx(
    State* state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
) {
    ctx.nodes++;
    if (ctx.stop) return 0;
    if (ply > ctx.seldepth) ctx.seldepth = ply;

    if (state->legal_actions.empty() && state->game_state == UNKNOWN)
        state->get_legal_actions();

    if (state->game_state == WIN)  return P_MAX - ply;
    if (state->game_state == DRAW) return 0;

    int rep_score;
    if (state->check_repetition(history, rep_score)) {
        if (rep_score == 0) {
            // Adjust draw value: penalise when ahead, accept when behind.
            int mat = 0;
            for (int r = 0; r < BOARD_H; ++r)
                for (int c = 0; c < BOARD_W; ++c) {
                    int sp = state->board.board[state->player][r][c];
                    int op = state->board.board[1-state->player][r][c];
                    if (sp && sp < 6) mat += piece_val[sp];
                    if (op && op < 6) mat -= piece_val[op];
                }
            if      (mat >  3) rep_score = -40;  // ahead: avoid draw
            else if (mat < -3) rep_score =  20;  // behind: draw beats losing
        }
        return rep_score;
    }

    uint64_t key = state->hash();

    // TT probe
    Move tt_move = {};
    bool has_tt_move = false;
    TTEntry& tte = g_tt[key & TT_MASK];
    if (tte.depth > 0 && tte.flag != TT_NONE && tte.key == key) {
        if (tte.best_move != 0) {
            tt_move = unpack_move(tte.best_move);
            has_tt_move = true;
        }
        if (tte.depth >= (uint8_t)depth) {
            int s = tte.score;
            if      (s >  P_MAX - 200) s -= ply;
            else if (s < -P_MAX + 200) s += ply;
            if (tte.flag == TT_EXACT)              return s;
            if (tte.flag == TT_LOWER && s > alpha) alpha = s;
            if (tte.flag == TT_UPPER && s < beta)  beta  = s;
            if (alpha >= beta) return s;
        }
    }

    history.push(key);

    if (depth <= 0) {
        int score = quiescence(state, alpha, beta, history, ply, ctx, p, 0);
        history.pop(key);
        return score;
    }

    // Null Move Pruning: pass our turn; if the result is still >= beta the
    // position is so strong we can prune without searching our own moves.
    // Skip when: near mate (score could swing wildly), or likely zugzwang
    // (fewer than 2 non-pawn pieces — endgame with only pawns & king).
    if (depth >= 3 && beta < P_MAX - 100) {
        int non_pawn = 0;
        for (int r = 0; r < BOARD_H; ++r)
            for (int c = 0; c < BOARD_W; ++c)
                if (state->board.board[state->player][r][c] > 1) non_pawn++;
        if (non_pawn >= 2) {
            State* null_s = static_cast<State*>(state->create_null_state());
            if (null_s->game_state != WIN) {
                int R = (depth >= 6) ? 4 : 3;
                int null_score = -eval_ctx(null_s, depth - 1 - R, -beta, -beta + 1,
                                           history, ply + 1, ctx, p);
                delete null_s;
                if (!ctx.stop && null_score >= beta) {
                    history.pop(key);
                    return beta;
                }
            } else {
                delete null_s;
            }
        }
    }

    auto moves = state->legal_actions;
    if (has_tt_move) {
        auto it = std::find(moves.begin(), moves.end(), tt_move);
        if (it != moves.end()) std::rotate(moves.begin(), it, it + 1);
        order_moves(std::next(moves.begin()), moves.end(), state, ply);
    } else {
        order_moves(moves.begin(), moves.end(), state, ply);
    }

    int orig_alpha = alpha;
    int best = M_MAX;
    Move best_move = {};
    bool first = true;
    int move_index = 0;

    for (auto& action : moves) {
        if (ctx.stop) break;
        bool capture = (state->board.board[1 - state->player][action.second.first][action.second.second] != 0);
        State* next = state->next_state(action);

        // Check extension: if our move leaves the opponent's king attacked,
        // search 1 ply deeper — opponent must respond to the threat.
        int ext = (ply < 20 && next->is_king_attacked()) ? 1 : 0;

        int score;
        if (first) {
            score = -eval_ctx(next, depth - 1 + ext, -beta, -alpha, history, ply + 1, ctx, p);
            first = false;
        } else {
            int reduction = 0;
            if (!ext && depth >= 3 && move_index >= 3 && !capture)
                reduction = (depth >= 6) ? 2 : 1;

            score = -eval_ctx(next, depth - 1 + ext - reduction, -alpha - 1, -alpha, history, ply + 1, ctx, p);
            if (score > alpha && !ctx.stop) {
                if (reduction > 0)
                    score = -eval_ctx(next, depth - 1 + ext, -alpha - 1, -alpha, history, ply + 1, ctx, p);
                if (score > alpha && score < beta && !ctx.stop)
                    score = -eval_ctx(next, depth - 1 + ext, -beta, -alpha, history, ply + 1, ctx, p);
            }
        }
        delete next;
        move_index++;

        if (score > best) { best = score; best_move = action; }
        if (score > alpha) alpha = score;
        if (alpha >= beta) {
            if (!capture) store_killer(ply, action);
            break;
        }
    }

    history.pop(key);

    // TT store (depth-preferred)
    if (!ctx.stop && best_move != Move{}) {
        uint8_t new_depth = (uint8_t)(depth < 255 ? depth : 255);
        if (tte.flag == TT_NONE || tte.key != key || new_depth >= tte.depth) {
            int stored_score = best;
            if      (stored_score >  P_MAX - 200) stored_score += ply;
            else if (stored_score < -P_MAX + 200) stored_score -= ply;

            TTEntry store;
            store.key       = key;
            store.score     = stored_score;
            store.depth     = new_depth;
            store.best_move = pack_move(best_move);
            if      (best <= orig_alpha) store.flag = TT_UPPER;
            else if (best >= beta)       store.flag = TT_LOWER;
            else                         store.flag = TT_EXACT;
            tte = store;
        }
    }

    return best;
}

SearchResult PVS::search(
    State* state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
) {
    ctx.reset();
    // Clear killers at the start of each move decision (depth==1 is the first call
    // in iterative deepening for a new board position).
    if (depth == 1) {
        for (int i = 0; i < MAX_PLY; i++)
            g_killers[i][0] = g_killers[i][1] = Move{};
    }
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if (!state->legal_actions.size())
        state->get_legal_actions();

    auto moves = state->legal_actions;

    // Probe TT for best move from previous iteration
    uint64_t root_key = state->hash();
    TTEntry& root_tte = g_tt[root_key & TT_MASK];
    if (root_tte.depth > 0 && root_tte.flag != TT_NONE && root_tte.key == root_key && root_tte.best_move != 0) {
        Move root_tt_move = unpack_move(root_tte.best_move);
        auto it = std::find(moves.begin(), moves.end(), root_tt_move);
        if (it != moves.end()) {
            std::rotate(moves.begin(), it, it + 1);
            order_moves(std::next(moves.begin()), moves.end(), state);
        } else {
            order_moves(moves.begin(), moves.end(), state);
        }
    } else {
        order_moves(moves.begin(), moves.end(), state);
    }

    // Aspiration windows: start with ±50 window around previous depth's score
    bool use_asp = (depth >= 2 && root_tte.flag == TT_EXACT &&
                    root_tte.key == root_key && root_tte.depth > 0);
    int center = use_asp ? root_tte.score : 0;
    // Don't aspirate near mate — score can swing wildly
    if (center > P_MAX - 200 || center < -P_MAX + 200) use_asp = false;
    int asp_delta = 50;

    for (;;) {
        int alpha = use_asp ? center - asp_delta : M_MAX;
        int beta  = use_asp ? center + asp_delta : P_MAX;
        int init_alpha = alpha;

        bool first = true;
        int move_index = 0, total_moves = (int)moves.size();
        Move best_this = {};

        for (auto& action : moves) {
            if (ctx.stop) break;
            State* next = state->next_state(action);
            int score;

            if (first) {
                score = -eval_ctx(next, depth - 1, -beta, -alpha, history, 1, ctx, p);
                first = false;
            } else {
                bool cap = (state->board.board[1 - state->player][action.second.first][action.second.second] != 0);
                int reduction = (depth >= 3 && move_index >= 3 && !cap) ? 1 : 0;
                score = -eval_ctx(next, depth - 1 - reduction, -alpha - 1, -alpha, history, 1, ctx, p);
                if (score > alpha && !ctx.stop) {
                    if (reduction > 0)
                        score = -eval_ctx(next, depth - 1, -alpha - 1, -alpha, history, 1, ctx, p);
                    if (score > alpha && score < beta && !ctx.stop)
                        score = -eval_ctx(next, depth - 1, -beta, -alpha, history, 1, ctx, p);
                }
            }
            delete next;

            if (score > alpha) {
                alpha = score;
                best_this = action;
                if (p.report_partial && ctx.on_root_update)
                    ctx.on_root_update({best_this, alpha, depth, move_index + 1, total_moves});
            }
            move_index++;
            if (alpha >= beta) break;
        }

        if (ctx.stop) {
            if (best_this != Move{}) result.best_move = best_this;
            result.score = alpha;
            break;
        }

        if (use_asp) {
            if (alpha <= init_alpha) {
                // Fail-low: widen lower bound and retry
                asp_delta *= 3;
                if (asp_delta >= P_MAX) use_asp = false;
                continue;
            }
            if (alpha >= beta) {
                // Fail-high: widen upper bound and retry
                asp_delta *= 3;
                center = alpha;
                if (asp_delta >= P_MAX) use_asp = false;
                continue;
            }
        }

        result.best_move = best_this;
        result.score = alpha;
        break;
    }

    // Store the root result in the TT. Without this, every iterative-deepening
    // iteration re-orders the root from scratch (MVV-LVA), so a tempting but
    // losing capture is tried first every time. If the time limit then kills
    // the search mid-iteration, the partial "currmove" reported is that losing
    // first move. Storing the root best move means each new iteration searches
    // (and reports) the genuinely best move first, so an interrupted search
    // still yields a sound move.
    if (result.best_move != Move{} && !ctx.stop) {
        TTEntry& rt = g_tt[root_key & TT_MASK];
        if (rt.flag == TT_NONE || rt.key != root_key || (uint8_t)depth >= rt.depth) {
            rt.key       = root_key;
            rt.score     = result.score;   // root ply == 0, no mate normalization
            rt.depth     = (uint8_t)(depth < 255 ? depth : 255);
            rt.best_move = pack_move(result.best_move);
            rt.flag      = TT_EXACT;
        }
    }

    if (result.best_move != Move{})
        result.pv = {result.best_move};
    return result;
}

ParamMap PVS::default_params() {
    return {{"UseKPEval","true"},{"UseEvalMobility","true"},{"ReportPartial","true"}};
}
std::vector<ParamDef> PVS::param_defs() {
    return {{"UseKPEval",ParamDef::CHECK,"true"},{"UseEvalMobility",ParamDef::CHECK,"true"},{"ReportPartial",ParamDef::CHECK,"true"}};
}
