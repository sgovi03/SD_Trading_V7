// ============================================================
// SD TRADING V8 - SYMBOL ENGINE CONTEXT
// src/scanner/symbol_engine_context.h
// Milestone M4.1
//
// One SymbolEngineContext per active symbol.
// Owned by ScannerOrchestrator.
// Holds all per-symbol state: resolved config, zone detector,
// scorer, entry engine, bar buffer, active zones.
//
// The C++ engine components (ZoneDetector, ZoneScorer,
// EntryDecisionEngine) are instantiated once per symbol
// and are NOT shared across symbols — this is what makes
// the thread-pool approach safe.
// ============================================================

#ifndef SDTRADING_SYMBOL_ENGINE_CONTEXT_H
#define SDTRADING_SYMBOL_ENGINE_CONTEXT_H

#include <string>
#include <vector>
#include <memory>
#include <deque>
#include <chrono>
#include "common_types.h"
#include "../core/config_cascade.h"
#include "../zones/zone_detector.h"
#include "../scoring/zone_scorer.h"
#include "../scoring/entry_decision_engine.h"
#include "../core/zone_state_manager.h"
#include "../events/event_types.h"

namespace SDTrading {
namespace Scanner {

// Engine state machine
enum class EngineState {
    UNINITIALIZED,
    READY,          // Zones loaded, waiting for bars
    PROCESSING,     // Currently scoring a bar (thread-pool task)
    SUSPENDED,      // Loss protection pause active
    STOPPED         // Session ended or error
};

inline std::string engine_state_str(EngineState s) {
    switch (s) {
        case EngineState::UNINITIALIZED: return "UNINITIALIZED";
        case EngineState::READY:         return "READY";
        case EngineState::PROCESSING:    return "PROCESSING";
        case EngineState::SUSPENDED:     return "SUSPENDED";
        case EngineState::STOPPED:       return "STOPPED";
        default:                         return "UNKNOWN";
    }
}

// ============================================================
// SIGNAL RESULT
// Produced by the engine scoring step for one symbol.
// ============================================================
struct SignalResult {
    std::string symbol;
    bool        has_signal      = false;
    std::string direction;                 // "LONG" | "SHORT"
    double      score           = 0.0;
    int         zone_id         = -1;
    std::string zone_type;                 // "DEMAND" | "SUPPLY"
    double      entry_price     = 0.0;
    double      stop_loss       = 0.0;
    double      target_1        = 0.0;
    double      target_2        = 0.0;
    double      rr_ratio        = 0.0;
    int         lot_size        = 0;
    std::string order_tag;
    std::string bar_timestamp;

    // Score breakdown for database
    double      score_base_strength    = 0.0;
    double      score_elite_bonus      = 0.0;
    double      score_swing            = 0.0;
    double      score_regime           = 0.0;
    double      score_freshness        = 0.0;
    double      score_rejection        = 0.0;
    std::string score_rationale;
    std::string regime;

    // Market state snapshot
    double      fast_ema        = 0.0;
    double      slow_ema        = 0.0;
    double      rsi             = 0.0;
    double      adx             = 0.0;
    double      di_plus         = 0.0;
    double      di_minus        = 0.0;

    // Zone state change flags
    bool        zones_changed   = false;
    std::vector<Core::Zone> updated_zones;
};

// ============================================================
// SYMBOL ENGINE CONTEXT
// ============================================================
class SymbolEngineContext {
public:
    // --------------------------------------------------------
    // Constructor — takes fully resolved config + profile.
    // Does NOT load zones — call load_zones() after construction.
    // --------------------------------------------------------
    explicit SymbolEngineContext(const Core::ResolvedConfig& resolved);

    // Non-copyable (owns engine component instances)
    SymbolEngineContext(const SymbolEngineContext&) = delete;
    SymbolEngineContext& operator=(const SymbolEngineContext&) = delete;

    // --------------------------------------------------------
    // Lifecycle
    // --------------------------------------------------------

    // Load initial zone state from DB (called at startup).
    // Replays historical bars through zone detector to align state.
    bool load_zones(const std::vector<Core::Bar>& historical_bars);

    // --------------------------------------------------------
    // on_bar() — called by ScannerOrchestrator on each
    // BarValidatedEvent for this symbol.
    // Appends bar to buffer, runs scoring, returns SignalResult.
    // Thread-safe: this method is called from thread-pool workers.
    // --------------------------------------------------------
    SignalResult on_bar(const Core::Bar& bar);

    // --------------------------------------------------------
    // Accessors
    // --------------------------------------------------------
    const std::string&         symbol()     const { return profile_.symbol; }
    const Core::SymbolProfile& profile()    const { return profile_; }
    const Core::Config&        config()     const { return config_; }
    EngineState                state()      const { return state_; }
    bool                       is_ready()   const { return state_ == EngineState::READY; }
    int                        bar_count()  const { return bar_count_; }
    const std::string&         last_bar_ts()const { return last_bar_timestamp_; }

    // Loss protection
    bool is_suspended() const { return state_ == EngineState::SUSPENDED; }
    void set_state(EngineState s) { state_ = s; }

    // Zone access (for DB persistence at session close)
    const std::vector<Core::Zone>& active_zones() const { return active_zones_; }

    // Session metrics
    int   signals_emitted()   const { return signals_emitted_; }
    int   consecutive_losses()const { return consecutive_losses_; }
    void  on_trade_win()  { consecutive_losses_ = 0; }
    void  on_trade_loss() {
        ++consecutive_losses_;
        if (consecutive_losses_ >= config_.max_consecutive_losses) {
            state_ = EngineState::SUSPENDED;
        }
    }

private:
    // Config (immutable after construction)
    Core::Config        config_;
    Core::SymbolProfile profile_;

    // Engine components — one set per symbol, never shared
    Core::ZoneDetector          detector_;
    Core::ZoneScorer            scorer_;
    Core::EntryDecisionEngine   entry_engine_;
    Core::ZoneStateManager      zone_state_mgr_;

    // Runtime state
    std::vector<Core::Zone>     active_zones_;
    int                         next_zone_id_  = 1;
    EngineState                 state_         = EngineState::UNINITIALIZED;
    int                         bar_count_     = 0;
    std::string                 last_bar_timestamp_;
    int                         signals_emitted_    = 0;
    int                         consecutive_losses_ = 0;

    // Internal helpers
    Core::MarketRegime  determine_regime() const;
    std::string         generate_order_tag(const std::string& direction,
                                           const std::string& timestamp) const;
};

} // namespace Scanner
} // namespace SDTrading

#endif // SDTRADING_SYMBOL_ENGINE_CONTEXT_H
