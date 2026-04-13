// ============================================================
// SD TRADING V8 - SCANNER ORCHESTRATOR
// src/scanner/scanner_orchestrator.h
// Milestone M4.3
//
// Coordinates all active symbols.
// Subscribes to EventBus (BarValidatedEvent, SYNC).
// Routes each bar to the correct SymbolEngineContext.
// Calls CpuBackend::score_symbols() for parallel scoring.
// Publishes SignalEvent to EventBus for each result.
// Applies portfolio position guard before publishing.
//
// Startup sequence:
//   1. Load symbol_registry.json → active symbols
//   2. For each symbol:
//        a. Resolve config (ConfigCascadeResolver)
//        b. Create SymbolEngineContext
//        c. Load historical bars from SqliteBarRepository
//        d. Call ctx->load_zones(bars)
//   3. Create CpuBackend (or AUTO-select CudaBackend in M7)
//   4. Subscribe to EventBus SYNC for BarValidatedEvent
//   5. bus.start()
//
// On BarValidatedEvent:
//   - Look up context by symbol
//   - Convert BarValidatedEvent → Core::Bar
//   - Dispatch to CpuBackend (scores all due symbols in batch)
//   - Rank results by score
//   - Apply portfolio guard (skip if max open positions reached)
//   - For each signal above threshold: publish SignalEvent
// ============================================================

#ifndef SDTRADING_SCANNER_ORCHESTRATOR_H
#define SDTRADING_SCANNER_ORCHESTRATOR_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "symbol_engine_context.h"
#include "cpu_backend.h"
#include "../core/config_cascade.h"
#include "../events/event_bus.h"
#include "../events/event_types.h"
#include "../persistence/sqlite_connection.h"
#include "../persistence/sqlite_bar_repository.h"
#include "common_types.h"
#include "../utils/logger.h"

namespace SDTrading {
namespace Scanner {

// ============================================================
// SCANNER CONFIG (from SystemConfig v8 section)
// ============================================================
struct ScannerConfig {
    std::string symbol_registry_file = "config/symbol_registry.json";
    std::string config_root_dir      = "config";
    double      signal_min_score     = 65.0;
    std::string rank_by              = "SCORE";   // "SCORE"|"RR"|"ZONE_STRENGTH"
    int         max_portfolio_positions = 3;
    int         cpu_thread_pool_size = 8;
    std::string compute_backend      = "AUTO";    // "AUTO"|"CPU"|"CUDA"
    int         lookback_bars        = 500;       // Historical bars for zone bootstrap
};

// ============================================================
// SCANNER ORCHESTRATOR
// ============================================================
class ScannerOrchestrator {
public:
    ScannerOrchestrator(const ScannerConfig&      cfg,
                        Events::EventBus&          bus,
                        Persistence::SqliteConnection& conn);
    ~ScannerOrchestrator();

    // Non-copyable
    ScannerOrchestrator(const ScannerOrchestrator&) = delete;
    ScannerOrchestrator& operator=(const ScannerOrchestrator&) = delete;

    // --------------------------------------------------------
    // Initialize — load symbols, bootstrap zones, subscribe.
    // Returns true if at least one symbol is ready.
    // --------------------------------------------------------
    bool initialize();

    // --------------------------------------------------------
    // Activate / deactivate a symbol at runtime.
    // --------------------------------------------------------
    bool activate_symbol(const std::string& symbol);
    bool deactivate_symbol(const std::string& symbol);

    // ── Status ──────────────────────────────────────────────
    int  active_symbol_count()    const;
    int  signals_emitted_total()  const { return signals_total_; }
    int  open_positions()         const { return open_positions_; }

    // Called by Spring Boot on trade open/close to track positions
    void on_trade_opened(const std::string& symbol);
    void on_trade_closed(const std::string& symbol);

    // ── Reset loss-protection suspension ────────────────────
    void reset_suspension(const std::string& symbol);

private:
    ScannerConfig              cfg_;
    Events::EventBus&          bus_;
    Persistence::SqliteConnection& conn_;

    // Per-symbol contexts (symbol → context)
    std::unordered_map<std::string,
        std::unique_ptr<SymbolEngineContext>> contexts_;
    mutable std::mutex contexts_mutex_;

    // Pending bar delivery: symbol → latest bar (from pipe)
    std::unordered_map<std::string, Core::Bar> pending_bars_;
    std::mutex pending_mutex_;

    // Compute backend
    std::unique_ptr<CpuBackend> cpu_backend_;

    // Portfolio state
    std::atomic<int> open_positions_ { 0 };
    std::atomic<int> signals_total_  { 0 };

    // ── EventBus handler (SYNC, called on pipe-listener thread) ──
    void on_bar_validated(const Events::BarValidatedEvent& evt);

    // ── Signal emission ─────────────────────────────────────
    void emit_signal(const SignalResult& result);

    // ── Ranking ─────────────────────────────────────────────
    std::vector<SignalResult> rank_results(
        std::vector<SignalResult>& results) const;

    // ── Config + zone bootstrap ─────────────────────────────
    bool bootstrap_symbol(const Core::SymbolProfile& profile);

    // ── Convert event to Core::Bar ───────────────────────────
    static Core::Bar bar_from_event(const Events::BarValidatedEvent& evt);

    // ── Convert SignalResult → SignalEvent ───────────────────
    static Events::SignalEvent signal_event_from_result(
        const SignalResult& result);
};

} // namespace Scanner
} // namespace SDTrading

#endif // SDTRADING_SCANNER_ORCHESTRATOR_H
