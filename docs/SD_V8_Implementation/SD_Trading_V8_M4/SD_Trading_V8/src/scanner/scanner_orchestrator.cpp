// ============================================================
// SD TRADING V8 - SCANNER ORCHESTRATOR IMPLEMENTATION
// src/scanner/scanner_orchestrator.cpp
// Milestone M4.3
// ============================================================

#include "scanner_orchestrator.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace SDTrading {
namespace Scanner {

using json = nlohmann::json;

// ============================================================
// Constructor
// ============================================================

ScannerOrchestrator::ScannerOrchestrator(
    const ScannerConfig&           cfg,
    Events::EventBus&              bus,
    Persistence::SqliteConnection& conn)
    : cfg_(cfg)
    , bus_(bus)
    , conn_(conn)
{
    LOG_INFO("[ScannerOrchestrator] Created.");
    LOG_INFO("[ScannerOrchestrator]"
             << " min_score=" << cfg_.signal_min_score
             << " rank_by=" << cfg_.rank_by
             << " max_positions=" << cfg_.max_portfolio_positions
             << " backend=" << cfg_.compute_backend);
}

ScannerOrchestrator::~ScannerOrchestrator() {
    LOG_INFO("[ScannerOrchestrator] Stopped."
             << " Signals emitted: " << signals_total_.load());
}

// ============================================================
// initialize
// ============================================================

bool ScannerOrchestrator::initialize() {
    LOG_INFO("[ScannerOrchestrator] Initializing...");

    // ── Load symbol registry ─────────────────────────────
    std::ifstream reg_file(cfg_.symbol_registry_file);
    if (!reg_file.is_open()) {
        LOG_ERROR("[ScannerOrchestrator] Cannot open symbol registry: "
                  << cfg_.symbol_registry_file);
        return false;
    }

    json reg;
    try { reg_file >> reg; }
    catch (const std::exception& e) {
        LOG_ERROR("[ScannerOrchestrator] Registry parse error: " << e.what());
        return false;
    }

    // ── Bootstrap each active symbol ─────────────────────
    int bootstrapped = 0;
    for (const auto& sym_json : reg["symbols"]) {
        bool active = sym_json.value("active", false);
        if (!active) continue;

        Core::SymbolProfile profile;
        profile.symbol         = sym_json.value("symbol", "");
        profile.asset_class    = sym_json.value("asset_class", "INDEX_FUTURES");
        profile.exchange       = sym_json.value("exchange", "NSE");
        profile.lot_size       = sym_json.value("lot_size", 65);
        profile.tick_size      = sym_json.value("tick_size", 0.05);
        profile.margin_per_lot = sym_json.value("margin_per_lot", 50000.0);
        profile.expiry_day     = sym_json.value("expiry_day", "THURSDAY");
        profile.live_ticker    = sym_json.value("live_feed_ticker", "");
        profile.config_file    = sym_json.value("config_file_path", "");

        if (profile.symbol.empty()) continue;

        LOG_INFO("[ScannerOrchestrator] Bootstrapping: " << profile.symbol
                 << " (lot=" << profile.lot_size << ")");

        if (bootstrap_symbol(profile)) {
            ++bootstrapped;
            LOG_INFO("[ScannerOrchestrator] " << profile.symbol << " ready.");
        } else {
            LOG_WARN("[ScannerOrchestrator] " << profile.symbol
                     << " bootstrap failed — skipped.");
        }
    }

    if (bootstrapped == 0) {
        LOG_ERROR("[ScannerOrchestrator] No symbols bootstrapped successfully.");
        return false;
    }

    LOG_INFO("[ScannerOrchestrator] " << bootstrapped << " symbols ready.");

    // ── Create compute backend ───────────────────────────
    cpu_backend_ = std::make_unique<CpuBackend>(cfg_.cpu_thread_pool_size);

    // ── Subscribe to BarValidatedEvent (SYNC) ────────────
    bus_.subscribe_sync<Events::BarValidatedEvent>("scanner",
        [this](const Events::BarValidatedEvent& e) {
            on_bar_validated(e);
        });

    LOG_INFO("[ScannerOrchestrator] Subscribed to BarValidatedEvent (SYNC).");
    return true;
}

// ============================================================
// bootstrap_symbol — load config, create context, load zones
// ============================================================

bool ScannerOrchestrator::bootstrap_symbol(const Core::SymbolProfile& profile) {
    // ── Resolve config cascade ───────────────────────────
    Core::ConfigCascadeResolver resolver(cfg_.config_root_dir);
    Core::ResolvedConfig resolved;
    try {
        resolved = resolver.resolve(profile.symbol,
                                    profile.asset_class,
                                    profile);
    } catch (const std::exception& e) {
        LOG_ERROR("[ScannerOrchestrator] Config cascade for "
                  << profile.symbol << " failed: " << e.what());
        return false;
    }

    // ── Create SymbolEngineContext ────────────────────────
    auto ctx = std::make_unique<SymbolEngineContext>(resolved);

    // ── Load historical bars for zone bootstrap ───────────
    Persistence::SqliteBarRepository bar_repo(conn_);
    std::vector<Core::Bar> hist_bars;

    // Try DB first
    hist_bars = bar_repo.get_bars(
        profile.symbol,
        "2000-01-01T00:00:00+05:30",    // far past
        "9999-12-31T23:59:59+05:30",    // far future
        "5min");

    // Limit to lookback_bars most recent
    if ((int)hist_bars.size() > cfg_.lookback_bars) {
        hist_bars.erase(hist_bars.begin(),
                        hist_bars.end() - cfg_.lookback_bars);
    }

    if (hist_bars.empty()) {
        LOG_WARN("[ScannerOrchestrator] No historical bars in DB for "
                 << profile.symbol << ". Zone state will build live.");
    } else {
        LOG_INFO("[ScannerOrchestrator] Loaded " << hist_bars.size()
                 << " historical bars for " << profile.symbol);
    }

    // ── Load zones ───────────────────────────────────────
    if (!ctx->load_zones(hist_bars)) {
        LOG_ERROR("[ScannerOrchestrator] Zone load failed for " << profile.symbol);
        return false;
    }

    // ── Register context ─────────────────────────────────
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    contexts_[profile.symbol] = std::move(ctx);
    return true;
}

// ============================================================
// on_bar_validated — SYNC handler (called on pipe thread)
// ============================================================

void ScannerOrchestrator::on_bar_validated(
    const Events::BarValidatedEvent& evt)
{
    // Convert to Core::Bar
    Core::Bar bar = bar_from_event(evt);

    // Store in pending map (one bar per symbol per cycle)
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_bars_[evt.symbol] = bar;
    }

    // ── Collect all symbols that have a pending bar ───────
    std::vector<SymbolEngineContext*> ready_contexts;
    std::vector<Core::Bar>           ready_bars;

    {
        std::lock_guard<std::mutex> clock(contexts_mutex_);
        std::lock_guard<std::mutex> plock(pending_mutex_);

        for (auto& [sym, ctx] : contexts_) {
            auto it = pending_bars_.find(sym);
            if (it == pending_bars_.end()) continue;
            if (!ctx->is_ready()) continue;

            ready_contexts.push_back(ctx.get());
            ready_bars.push_back(it->second);
            pending_bars_.erase(it);
        }
    }

    if (ready_contexts.empty()) return;

    // ── Parallel scoring via CpuBackend ──────────────────
    auto results = cpu_backend_->score_symbols(ready_contexts, ready_bars);

    // ── Filter to signals only ───────────────────────────
    std::vector<SignalResult> signals;
    for (auto& r : results) {
        if (r.has_signal && r.score >= cfg_.signal_min_score) {
            signals.push_back(r);
        }
    }

    if (signals.empty()) return;

    // ── Rank signals ─────────────────────────────────────
    auto ranked = rank_results(signals);

    // ── Apply portfolio guard + emit ─────────────────────
    for (const auto& result : ranked) {
        if (open_positions_.load() >= cfg_.max_portfolio_positions) {
            LOG_INFO("[ScannerOrchestrator] Portfolio full ("
                     << open_positions_.load() << "/"
                     << cfg_.max_portfolio_positions
                     << ") — skipping " << result.symbol);
            break;
        }
        emit_signal(result);
    }
}

// ============================================================
// emit_signal
// ============================================================

void ScannerOrchestrator::emit_signal(const SignalResult& result) {
    Events::SignalEvent evt = signal_event_from_result(result);
    bus_.publish(evt);
    ++signals_total_;

    LOG_INFO("[ScannerOrchestrator] SIGNAL EMITTED:"
             << " " << result.symbol
             << " " << result.direction
             << " score=" << result.score
             << " @" << result.entry_price
             << " tag=" << result.order_tag);
}

// ============================================================
// rank_results
// ============================================================

std::vector<SignalResult> ScannerOrchestrator::rank_results(
    std::vector<SignalResult>& results) const
{
    if (cfg_.rank_by == "RR") {
        std::stable_sort(results.begin(), results.end(),
            [](const SignalResult& a, const SignalResult& b) {
                return a.rr_ratio > b.rr_ratio;
            });
    } else if (cfg_.rank_by == "ZONE_STRENGTH") {
        std::stable_sort(results.begin(), results.end(),
            [](const SignalResult& a, const SignalResult& b) {
                return a.score_base_strength > b.score_base_strength;
            });
    } else {
        // Default: SCORE
        std::stable_sort(results.begin(), results.end(),
            [](const SignalResult& a, const SignalResult& b) {
                return a.score > b.score;
            });
    }
    return results;
}

// ============================================================
// activate / deactivate symbol at runtime
// ============================================================

bool ScannerOrchestrator::activate_symbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    auto it = contexts_.find(symbol);
    if (it == contexts_.end()) {
        LOG_WARN("[ScannerOrchestrator] activate_symbol: unknown " << symbol);
        return false;
    }
    it->second->set_state(EngineState::READY);
    LOG_INFO("[ScannerOrchestrator] " << symbol << " activated.");
    return true;
}

bool ScannerOrchestrator::deactivate_symbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    auto it = contexts_.find(symbol);
    if (it == contexts_.end()) return false;
    it->second->set_state(EngineState::STOPPED);
    LOG_INFO("[ScannerOrchestrator] " << symbol << " deactivated.");
    return true;
}

void ScannerOrchestrator::reset_suspension(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    auto it = contexts_.find(symbol);
    if (it == contexts_.end()) return;
    if (it->second->state() == EngineState::SUSPENDED) {
        it->second->set_state(EngineState::READY);
        LOG_INFO("[ScannerOrchestrator] " << symbol
                 << " suspension reset — trading resumed.");
    }
}

// ============================================================
// Portfolio tracking
// ============================================================

void ScannerOrchestrator::on_trade_opened(const std::string& symbol) {
    ++open_positions_;
    LOG_INFO("[ScannerOrchestrator] Trade opened on " << symbol
             << ". Open positions: " << open_positions_.load());
}

void ScannerOrchestrator::on_trade_closed(const std::string& symbol) {
    if (open_positions_.load() > 0) --open_positions_;
    LOG_INFO("[ScannerOrchestrator] Trade closed on " << symbol
             << ". Open positions: " << open_positions_.load());
}

int ScannerOrchestrator::active_symbol_count() const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    int count = 0;
    for (const auto& [sym, ctx] : contexts_) {
        if (ctx->is_ready()) ++count;
    }
    return count;
}

// ============================================================
// Static helpers
// ============================================================

Core::Bar ScannerOrchestrator::bar_from_event(
    const Events::BarValidatedEvent& evt)
{
    Core::Bar bar;
    bar.datetime = evt.timestamp;
    bar.open     = evt.open;
    bar.high     = evt.high;
    bar.low      = evt.low;
    bar.close    = evt.close;
    bar.volume   = static_cast<long long>(evt.volume);
    bar.oi       = evt.open_interest;
    return bar;
}

Events::SignalEvent ScannerOrchestrator::signal_event_from_result(
    const SignalResult& r)
{
    Events::SignalEvent e;
    e.symbol        = r.symbol;
    e.signal_time   = r.bar_timestamp;
    e.direction     = r.direction;
    e.score         = r.score;
    e.zone_id       = r.zone_id;
    e.zone_type     = r.zone_type;
    e.entry_price   = r.entry_price;
    e.stop_loss     = r.stop_loss;
    e.target_1      = r.target_1;
    e.target_2      = r.target_2;
    e.rr_ratio      = r.rr_ratio;
    e.lot_size      = r.lot_size;
    e.order_tag     = r.order_tag;
    e.regime        = r.regime;
    e.score_base_strength  = r.score_base_strength;
    e.score_elite_bonus    = r.score_elite_bonus;
    e.score_swing          = r.score_swing;
    e.score_regime         = r.score_regime;
    e.score_freshness      = r.score_freshness;
    e.score_rejection      = r.score_rejection;
    e.score_rationale      = r.score_rationale;
    e.fast_ema             = r.fast_ema;
    e.slow_ema             = r.slow_ema;
    e.rsi                  = r.rsi;
    e.adx                  = r.adx;
    e.di_plus              = r.di_plus;
    e.di_minus             = r.di_minus;
    return e;
}

} // namespace Scanner
} // namespace SDTrading
