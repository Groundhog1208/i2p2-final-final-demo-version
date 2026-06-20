#pragma once
#include <cstdint>
#include <atomic>
#include "search_types.hpp"
#include "game_history.hpp"

/* ============================================================
 * Transposition Table
 * ============================================================ */
enum TTFlag : uint8_t { TT_NONE = 0, TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3 };

// Pack a Move into 4 bytes: (from_row, from_col, to_row, to_col) each 0-7
inline uint32_t pack_move(const Move& m) {
    return ((uint32_t)m.first.first  << 24) | ((uint32_t)m.first.second  << 16)
         | ((uint32_t)m.second.first <<  8) | ((uint32_t)m.second.second);
}
inline Move unpack_move(uint32_t p) {
    return { { (p >> 24) & 0xFF, (p >> 16) & 0xFF },
             { (p >>  8) & 0xFF, (p      ) & 0xFF } };
}

// All fields default to 0 so the global array lives in BSS (no binary bloat).
// depth == 0 means the entry is invalid (we never store at depth 0).
struct TTEntry {
    uint64_t key       = 0;  // Zobrist key
    int32_t  score     = 0;
    uint8_t  depth     = 0;  // 0 = invalid
    TTFlag   flag      = TT_NONE;
    uint8_t  pad[2]    = {};
    uint32_t best_move = 0;  // packed; 0 treated as "no move"
};

static constexpr int TT_BITS = 20;                  // 1M entries ~= 24 MB
static constexpr size_t TT_SIZE = 1ULL << TT_BITS;
static constexpr size_t TT_MASK = TT_SIZE - 1;

// Defined in minimax.cpp
extern TTEntry g_tt[TT_SIZE];
inline void tt_clear() { for(auto& e : g_tt) e.flag = TT_NONE; }

/* ============================================================
 * Shared param struct (used by all three algorithms)
 * ============================================================ */
struct MMParams {
    bool use_kp_eval       = true;
    bool use_eval_mobility = true;
    bool report_partial    = true;

    static MMParams from_map(const ParamMap& m){
        MMParams p;
        p.use_kp_eval       = param_bool(m, "UseKPEval",       true);
        p.use_eval_mobility = param_bool(m, "UseEvalMobility", true);
        p.report_partial    = param_bool(m, "ReportPartial",   true);
        return p;
    }
};

/* ============================================================
 * MiniMax — plain negamax, no pruning
 * ============================================================ */
class MiniMax {
public:
    static int eval_ctx(
        State* state,
        int depth,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        const MMParams& p
    );
    static SearchResult search(
        State* state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );
    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};

/* ============================================================
 * AlphaBeta — negamax with alpha-beta pruning + MVV-LVA ordering
 * ============================================================ */
class AlphaBeta {
public:
    static int eval_ctx(
        State* state,
        int depth,
        int alpha,
        int beta,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        const MMParams& p
    );
    static SearchResult search(
        State* state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );
    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};

/* ============================================================
 * PVS — Principal Variation Search + Quiescence
 * ============================================================ */
class PVS {
public:
    static int quiescence(
        State* state,
        int alpha,
        int beta,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        const MMParams& p,
        int qdepth = 0
    );
    static int eval_ctx(
        State* state,
        int depth,
        int alpha,
        int beta,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        const MMParams& p
    );
    static SearchResult search(
        State* state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );
    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};
