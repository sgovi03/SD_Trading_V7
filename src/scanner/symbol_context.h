// ============================================================
// SD TRADING V8 - SYMBOL CONTEXT
// src/scanner/symbol_context.h
//
// PURPOSE:
//   Owns all per-symbol runtime objects for one trading symbol.
//   Thin lifecycle wrapper — no trading logic.
//
// CONTAINS:
//   • Resolved 3-tier config for this symbol
//   • PipeBroker (bar queue + order adapter)
//   • LiveEngine (full V7 trading logic — unchanged)
//   • Dedicated engine thread
//
// LIFECYCLE:
//   start() → launches engine thread
//             LiveEngine.run() blocks on PipeBroker.get_latest_bar()
//   stop()  → signals PipeBroker shutdown
//             joins engine thread cleanly
// ============================================================

#ifndef SDTRADING_SYMBOL_CONTEXT_H
#define SDTRADING_SYMBOL_CONTEXT_H

#include "live/pipe_broker.h"
#include "live/live_engine.h"
#include "scanner/portfolio_guard.h"
#include "events/event_bus.h"
#include "events/event_types.h"
#include "core/config_cascade.h"
#include "utils/logger.h"
#include <string>
#include <thread>
#include <memory>
#include <atomic>

namespace SDTrading {
namespace Scanner {

// ============================================================
// SymbolProfile — static symbol metadata from registry
// ============================================================
struct SymbolProfile {
    std::string symbol;             // e.g. "NIFTY"
    std::string display_name;       // e.g. "Nifty 50 Futures"
    int         lot_size    = 65;   // lots per contract
    std::string config_path;        // path to symbol-level config
};

// ============================================================
// SymbolContext
// ============================================================
class SymbolContext {
public:
    // ----------------------------------------------------------
    // Construction
    // ----------------------------------------------------------
    // profile      : static symbol metadata
    // order_cfg    : Spring Boot connection config
    // portfolio    : shared PortfolioGuard (passed by reference)
    // cascade_root : root config path for 3-tier resolution
    // output_dir   : where LiveEngine writes trade logs / zones
    // db_path      : SQLite database path
    // ----------------------------------------------------------
    SymbolContext(
        const SymbolProfile&             profile,
        const Live::OrderSubmitConfig&   order_cfg,
        PortfolioGuard&                  portfolio,
        const std::string&               cascade_root,
        const std::string&               output_dir,
        const std::string&               db_path,
        Events::EventBus*                bus = nullptr);  // V8: for zone DB persistence

    ~SymbolContext();

    // Non-copyable, non-movable (owns thread + broker)
    SymbolContext(const SymbolContext&)            = delete;
    SymbolContext& operator=(const SymbolContext&) = delete;

    // ----------------------------------------------------------
    // Lifecycle
    // ----------------------------------------------------------

    // Start the engine thread.
    // LiveEngine.run() blocks on PipeBroker until first bar arrives.
    void start();

    // Signal shutdown and join engine thread.
    // Blocks until engine exits cleanly.
    void stop();

    // ----------------------------------------------------------
    // Bar delivery — called by BarRouter
    // ----------------------------------------------------------
    void push_bar(const Core::Bar& bar);

    // ----------------------------------------------------------
    // Accessors
    // ----------------------------------------------------------
    const std::string&   symbol()    const { return profile_.symbol; }
    bool                 is_running() const { return running_.load(); }
    Live::PipeBroker&    broker()          { return *broker_; }

private:
    SymbolProfile                       profile_;
    PortfolioGuard&                     portfolio_;

    Live::PipeBroker*                   broker_  = nullptr;  // observer ptr — owned by engine_
    std::unique_ptr<Live::LiveEngine>   engine_;
    std::thread                         engine_thread_;
    std::atomic<bool>                   running_ { false };
    Events::EventBus*                   bus_     = nullptr;  // V8: zone publish

    // ── Engine thread entry point ─────────────────────────────
    void run_engine();
};

} // namespace Scanner
} // namespace SDTrading

#endif // SDTRADING_SYMBOL_CONTEXT_H
