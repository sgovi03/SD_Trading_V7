// ============================================================
// SD TRADING V8 - SYMBOL CONTEXT IMPLEMENTATION
// src/scanner/symbol_context.cpp
// ============================================================

#include "scanner/symbol_context.h"
#include "common_types.h"
#include "core/config_cascade.h"
#include "persistence/sqlite_bar_repository.h"
#include "persistence/sqlite_connection.h"
// SqliteConnection lives in SDTrading::Persistence namespace
#include <stdexcept>

namespace SDTrading {
namespace Scanner {

using namespace Utils;

// ============================================================
// Construction
// ============================================================

SymbolContext::SymbolContext(
    const SymbolProfile&            profile,
    const Live::OrderSubmitConfig&  order_cfg,
    PortfolioGuard&                 portfolio,
    const std::string&              cascade_root,
    const std::string&              output_dir,
    const std::string&              db_path,
    Events::EventBus*               bus)
    : profile_   (profile)
    , portfolio_ (portfolio)
    , bus_       (bus)
{
    LOG_INFO("[SymbolContext:" << profile_.symbol << "] Initialising...");

    // ── Step 1: Resolve 3-tier config for this symbol ────────
    // ConfigCascadeResolver is a static utility class.
    // Build a SymbolProfile for the resolver.
    Core::SymbolProfile sym_profile;
    sym_profile.symbol       = profile_.symbol;
    sym_profile.asset_class  = "INDEX_FUTURES";
    sym_profile.lot_size     = profile_.lot_size;

    Core::ResolvedConfig resolved = Core::ConfigCascadeResolver::resolve(
        cascade_root, sym_profile);
    Core::Config config = resolved.config;

    // Override lot_size from symbol registry (authoritative source)
    config.lot_size = profile_.lot_size;

    LOG_INFO("[SymbolContext:" << profile_.symbol << "]"
             << " lot_size="          << config.lot_size
             << " min_zone_strength=" << config.min_zone_strength
             << " config_hash="       << resolved.report.config_hash);

    // ── Step 2: Create PipeBroker with PortfolioGuard callbacks
    //
    // Three lambdas wire PortfolioGuard into PipeBroker without
    // creating a circular dependency:
    //
    //   get_balance    → PipeBroker calls this for get_account_balance()
    //   entry_allowed  → PipeBroker calls this for is_entry_allowed()
    //   on_order_ok    → PipeBroker calls this after successful order
    // ────────────────────────────────────────────────────────────
    const std::string sym = profile_.symbol;

    auto get_balance = [&portfolio]() -> double {
        return portfolio.capital_available();
    };

    auto entry_allowed = [&portfolio, sym]() -> bool {
        return portfolio.entry_allowed(sym);
    };

    auto on_order_ok = [&portfolio, sym](int lots, double entry_price) {
        // Direction is determined inside LiveEngine — for portfolio
        // tracking purposes we record the opening here.
        // LiveEngine notifies close via the TradeCloseListener path.
        portfolio.notify_position_opened(sym, "UNKNOWN", lots, entry_price);
    };

    // Create PipeBroker then transfer ownership to LiveEngine.
    // Keep a raw pointer for push_bar() access — safe because
    // LiveEngine (which owns the unique_ptr) is destroyed before
    // SymbolContext completes its destructor.
    auto broker_uptr = std::make_unique<Live::PipeBroker>(
        profile_.symbol,
        order_cfg,
        std::move(get_balance),
        std::move(entry_allowed),
        std::move(on_order_ok));

    broker_ = broker_uptr.get();   // raw observer pointer for push_bar()

    // ── Step 3: Create LiveEngine ────────────────────────────
    engine_ = std::make_unique<Live::LiveEngine>(
        config,
        std::move(broker_uptr),
        profile_.symbol,
        "5",
        output_dir,
        "");   // no CSV — V8 bootstraps from SQLite below

    // ── Step 4: Bootstrap bar_history from SQLite ─────────────
    // Load previously persisted bars so LiveEngine can detect zones
    // on restart without needing AFL to replay historical data.
    if (!db_path.empty()) {
        try {
            Persistence::SqliteConnection conn(db_path, /*read_only=*/true);
            Persistence::SqliteBarRepository repo(conn);

            int bar_count = repo.count_bars(profile_.symbol);
            LOG_INFO("[SymbolContext:" << profile_.symbol << "] SQLite has "
                     << bar_count << " bars — loading for zone bootstrap...");

            if (bar_count > 0) {
                // Load all available bars (from epoch to far future)
                auto bars = repo.get_bars(
                    profile_.symbol,
                    "2000-01-01T00:00:00",
                    "2099-12-31T23:59:59",
                    "5min");

                if (!bars.empty()) {
                    engine_->preload_bar_history(bars);
                    LOG_INFO("[SymbolContext:" << profile_.symbol << "] Preloaded "
                             << bars.size() << " bars into LiveEngine.");
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("[SymbolContext:" << profile_.symbol
                     << "] SQLite bar load failed: " << e.what()
                     << " — starting with empty history.");
        }
    }

    LOG_INFO("[SymbolContext:" << profile_.symbol << "] Ready.");
}

SymbolContext::~SymbolContext() {
    stop();
}

// ============================================================
// Lifecycle
// ============================================================

void SymbolContext::start() {
    if (running_.load()) {
        LOG_WARN("[SymbolContext:" << profile_.symbol << "] Already running.");
        return;
    }

    running_.store(true);
    // broker_->connect() is called by LiveEngine during initialize()
    engine_thread_ = std::thread([this] { run_engine(); });

    LOG_INFO("[SymbolContext:" << profile_.symbol
             << "] Engine thread started. Waiting for bars...");
}

void SymbolContext::stop() {
    if (!running_.load()) return;

    LOG_INFO("[SymbolContext:" << profile_.symbol << "] Stopping...");

    running_.store(false);
    engine_->stop();        // Signal LiveEngine to exit its run loop
    if (broker_) broker_->shutdown();  // Unblock get_latest_bar() if waiting

    if (engine_thread_.joinable()) {
        engine_thread_.join();
    }

    LOG_INFO("[SymbolContext:" << profile_.symbol << "] Stopped.");
}

// ============================================================
// Bar delivery
// ============================================================

void SymbolContext::push_bar(const Core::Bar& bar) {
    if (!running_.load()) {
        LOG_WARN("[SymbolContext:" << profile_.symbol
                 << "] push_bar called but not running — dropped.");
        return;
    }
    broker_->push_bar(bar);
}

// ============================================================
// Engine thread
// ============================================================

// ── Zone publish helper ─────────────────────────────────────
static Events::ZoneSnapshotEvent zone_to_event(
    const Core::Zone& z, const std::string& symbol)
{
    Events::ZoneSnapshotEvent e;
    e.symbol               = symbol;
    e.zone_id              = z.zone_id;
    e.zone_type            = (z.type == Core::ZoneType::DEMAND) ? "DEMAND" : "SUPPLY";
    e.base_low             = z.base_low;
    e.base_high            = z.base_high;
    e.distal_line          = z.distal_line;
    e.proximal_line        = z.proximal_line;
    e.formation_bar        = z.formation_bar;
    e.formation_datetime   = z.formation_datetime;
    e.strength             = z.strength;

    // State
    switch (z.state) {
        case Core::ZoneState::FRESH:     e.state = "FRESH";     break;
        case Core::ZoneState::TESTED:    e.state = "TESTED";    break;
        case Core::ZoneState::VIOLATED:  e.state = "VIOLATED";  break;
        case Core::ZoneState::RECLAIMED: e.state = "RECLAIMED"; break;
        case Core::ZoneState::EXHAUSTED: e.state = "EXHAUSTED"; break;
        default:                         e.state = "FRESH";     break;
    }
    e.touch_count            = z.touch_count;
    e.consecutive_losses     = z.consecutive_losses;
    e.exhausted_at_datetime  = z.exhausted_at_datetime;
    e.is_elite               = z.is_elite;
    e.departure_imbalance    = z.departure_imbalance;
    e.retest_speed           = z.retest_speed;
    e.bars_to_retest         = z.bars_to_retest;
    e.was_swept              = z.was_swept;
    e.sweep_bar              = z.sweep_bar;
    e.bars_inside_after_sweep = z.bars_inside_after_sweep;
    e.reclaim_eligible       = z.reclaim_eligible;
    e.is_flipped_zone        = z.is_flipped_zone;
    e.parent_zone_id         = z.parent_zone_id;
    e.swing_percentile       = z.swing_analysis.swing_percentile;
    e.is_at_swing_high       = z.swing_analysis.is_at_swing_high;
    e.is_at_swing_low        = z.swing_analysis.is_at_swing_low;
    e.swing_score            = z.swing_analysis.swing_score;
    e.volume_ratio           = z.volume_profile.volume_ratio;
    e.departure_volume_ratio = z.volume_profile.departure_volume_ratio;
    e.zone_is_initiative     = z.volume_profile.is_initiative;
    e.zone_has_vol_climax    = z.volume_profile.has_volume_climax;
    e.zone_vol_score         = z.volume_profile.volume_score;
    e.institutional_index    = z.institutional_index;
    e.oi_formation           = static_cast<double>(z.oi_profile.formation_oi);
    e.oi_change_pct          = z.oi_profile.oi_change_percent;
    e.oi_market_phase        = market_phase_to_string(z.oi_profile.market_phase);
    e.oi_score               = z.oi_profile.oi_score;
    e.is_active              = z.is_active;
    e.entry_retry_count      = z.entry_retry_count;
    e.zone_json              = "";  // future: serialise full zone JSON
    return e;
}

void SymbolContext::run_engine() {
    LOG_INFO("[SymbolContext:" << profile_.symbol << "] Engine thread running.");
    try {
        if (!engine_->initialize()) {
            LOG_ERROR("[SymbolContext:" << profile_.symbol
                      << "] Engine initialize() failed.");
            running_.store(false);
            return;
        }

        if (bus_) {
            // ── Zone state update callback ─────────────────────
            engine_->set_zone_update_callback(
                [this](const std::string& sym, int zone_id,
                       const std::string& new_state, int touch, int consec) {
                    Events::ZoneUpdateEvent evt;
                    evt.symbol             = sym;
                    evt.zone_id            = zone_id;
                    evt.new_state          = new_state;
                    evt.touch_count        = touch;
                    evt.consecutive_losses = consec;
                    evt.updated_at         = "";
                    bus_->publish(evt);
                });

            // ── Zone snapshot callback (fired post-bootstrap & shutdown) ──
            engine_->set_zone_snapshot_callback(
                [this](const std::vector<Core::Zone>& active,
                       const std::vector<Core::Zone>& inactive) {
                    int published = 0;
                    for (const auto& z : active) {
                        bus_->publish(zone_to_event(z, profile_.symbol));
                        ++published;
                    }
                    for (const auto& z : inactive) {
                        bus_->publish(zone_to_event(z, profile_.symbol));
                        ++published;
                    }
                    LOG_INFO("[SymbolContext:" << profile_.symbol
                             << "] Published " << published
                             << " zone(s) to DB via EventBus (post-bootstrap).");
                });

            // ── Signal event callback ───────────────────────────
            engine_->set_signal_pub_callback(
                [this](const Events::SignalEvent& evt) {
                    bus_->publish(evt);
                });

            // ── Trade open callback ─────────────────────────────
            engine_->set_trade_open_callback(
                [this](const Events::TradeOpenEvent& evt) {
                    bus_->publish(evt);
                });

            // ── Trade close callback ────────────────────────────
            engine_->set_trade_close_callback(
                [this](const Events::TradeCloseEvent& evt) {
                    bus_->publish(evt);
                });
        }

        engine_->run();     // Blocks until stop() is called
    } catch (const std::exception& ex) {
        LOG_ERROR("[SymbolContext:" << profile_.symbol
                  << "] Engine thread exception: " << ex.what());
    } catch (...) {
        LOG_ERROR("[SymbolContext:" << profile_.symbol
                  << "] Engine thread unknown exception.");
    }
    running_.store(false);
    LOG_INFO("[SymbolContext:" << profile_.symbol << "] Engine thread exited.");
}

} // namespace Scanner
} // namespace SDTrading