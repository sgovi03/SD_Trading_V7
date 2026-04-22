// ============================================================
// SD TRADING V8.0 — LIVE RUNNER
// src/run_v8_live.cpp
//
// ARCHITECTURE (M6 refactor — EngineFactory + LiveEngine):
//
//   AmiBroker → PipeListener → BarRouter → PipeBroker_NIFTY
//                                        → PipeBroker_BNKFT
//                                               ↕
//                                        PortfolioGuard (shared)
//                                               ↕
//                                        LiveEngine_NIFTY  (V7 — unchanged)
//                                        LiveEngine_BNKFT  (V7 — unchanged)
//
// STARTUP ORDER:
//   1.  Load system_config.json
//   2.  Initialise SQLite DB (schema + WAL)
//   3.  Start EventBus + DbWriter
//   4.  Create PortfolioGuard (shared capital + risk rules)
//   5.  Create SymbolContext per active symbol
//         → resolves 3-tier config
//         → creates PipeBroker (wired to PortfolioGuard)
//         → creates LiveEngine (wired to PipeBroker)
//   6.  Create BarRouter (routes validated bars to PipeBrokers)
//   7.  Start PipeListeners (one per symbol)
//   8.  Start SymbolContexts (launches LiveEngine threads)
//   9.  Start Spring Boot bridge (order submission + close poll)
//  10.  Wait for Ctrl+C
//  11.  Shutdown: stop pipes → stop engines → drain bus → metrics
//
// USAGE:
//   run_v8_live.exe
//   run_v8_live.exe --dry-run   (signals logged, no orders placed)
// ============================================================

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>
#include <csignal>
#include <fstream>

#ifdef ERROR
#undef ERROR
#endif
#ifdef WARN
#undef WARN
#endif

// ── System ───────────────────────────────────────────────────
#include "utils/system_initializer.h"
#include "system_config.h"       // ::SystemConfig (jsoncpp-based, for order_submitter.enabled)
#include "utils/logger.h"

// ── Persistence ──────────────────────────────────────────────
#include "persistence/sqlite_connection.h"
#include "persistence/db_writer.h"

// ── Event infrastructure ─────────────────────────────────────
#include "events/event_bus.h"
#include "events/event_types.h"

// ── Data pipeline ────────────────────────────────────────────
#include "data/bar_validator.h"
#include "data/pipe_listener.h"

// ── V8 new architecture ───────────────────────────────────────
#include "scanner/portfolio_guard.h"
#include "scanner/symbol_context.h"
#include "scanner/bar_router.h"

// ── Spring Boot bridge ────────────────────────────────────────
#include "springboot/springboot_bridge.h"

// ── Analytics ────────────────────────────────────────────────
#include "analytics/metrics_aggregator.h"

// ── Symbol registry ──────────────────────────────────────────
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using namespace SDTrading;
using namespace SDTrading::Utils;
using json = nlohmann::json;

// ============================================================
// GLOBAL SHUTDOWN FLAG
// ============================================================
static std::atomic<bool> g_shutdown { false };

#ifdef _WIN32
#include <windows.h>
static BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        std::cout << "\n[SHUTDOWN] Ctrl+C received — stopping gracefully...\n";
        g_shutdown = true;
        return TRUE;
    }
    return FALSE;
}
#else
static void sig_handler(int) {
    std::cout << "\n[SHUTDOWN] Signal received — stopping gracefully...\n";
    g_shutdown = true;
}
#endif

// ============================================================
// Command-line flags
// ============================================================
struct RunConfig {
    bool dry_run = false;  // --dry-run: log signals, skip order submission
};

static RunConfig parse_args(int argc, char* argv[]) {
    RunConfig rc;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dry-run") rc.dry_run = true;
    }
    return rc;
}

// ============================================================
// Session summary
// ============================================================
static void print_summary(const Analytics::PortfolioMetrics& p) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║       SD TRADING V8 — SESSION SUMMARY        ║\n";
    std::cout << "╠══════════════════════════════════════════════╣\n";
    std::cout << "║  Total Trades:    "
              << std::left << std::setw(27) << p.total_trades    << "║\n";
    std::cout << "║  Win Rate:        "
              << std::setw(24)
              << (std::to_string((int)(p.portfolio_win_rate * 100)) + "%") << " ║\n";
    std::cout << "║  Profit Factor:   "
              << std::setw(27)
              << std::fixed << std::setprecision(2) << p.portfolio_profit_factor << "║\n";
    std::cout << "║  Net P&L:         ₹"
              << std::setw(26) << (int)p.total_net_pnl << "║\n";
    std::cout << "║  Max Drawdown:    "
              << std::setw(24)
              << (std::to_string((int)p.max_drawdown_pct) + "%") << " ║\n";
    std::cout << "╠══════════════════════════════════════════════╣\n";
    for (const auto& s : p.by_symbol) {
        std::cout << "║  " << std::left << std::setw(10) << s.symbol
                  << "  trades=" << std::setw(3)  << s.total_trades
                  << "  wr="     << std::setw(5)
                  << (std::to_string((int)(s.win_rate * 100)) + "%")
                  << "  pnl=₹"   << std::setw(8)  << (int)s.net_pnl
                  << " ║\n";
    }
    std::cout << "╚══════════════════════════════════════════════╝\n\n";
}

// ============================================================
// Load PortfolioConfig from resolved MASTER.config
// ============================================================
static Scanner::PortfolioConfig load_portfolio_config(
    const Core::SystemConfig& sys)
{
    Scanner::PortfolioConfig cfg;
    cfg.starting_capital      = sys.trading_starting_capital;
    cfg.risk_per_trade_pct    = 2.0;   // TODO: add to system_config.json
    cfg.max_position_lots     = 4;
    cfg.daily_loss_limit_pct  = 5.0;
    cfg.baseline_score        = 0.65;
    cfg.baseline_rr           = 1.75;
    cfg.max_positions_per_symbol = 1;
    return cfg;
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char* argv[]) {

    RunConfig rc = parse_args(argc, argv);

    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║     SD TRADING SYSTEM V8.0 — LIVE RUNNER      ║\n";
    std::cout << "║     Multi-Symbol | LiveEngine per Symbol       ║\n";
    if (rc.dry_run)
        std::cout << "║     *** DRY-RUN MODE — No orders placed ***    ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    // ── Register Ctrl+C handler ───────────────────────────────
#ifdef _WIN32
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);
#endif

    try {
        // ── STEP 1: Load system configuration ────────────────
        std::cout << "[1/9] Loading system configuration...\n";
        auto& sys_init = Utils::SystemInitializer::getInstance();
        if (!sys_init.initialize()) {
            std::cerr << "ERROR: " << sys_init.getLastError() << "\n";
            return 1;
        }
        const Core::SystemConfig& sys = sys_init.getConfig();
        sys_init.printConfig();

        // ── STEP 2: Initialise SQLite ─────────────────────────
        std::cout << "[2/9] Initialising SQLite database...\n";
        std::string db_path     = sys.get_full_path(sys.sqlite_db_path);
        std::string schema_path = sys.get_full_path(sys.sqlite_schema_path);
        fs::create_directories(fs::path(db_path).parent_path());

        Persistence::SqliteConnectionFactory::initialize(db_path, schema_path);
        auto conn = Persistence::SqliteConnectionFactory::create_write_connection();
        std::cout << "    DB: " << db_path << "\n";

        // ── STEP 3: EventBus + DbWriter ───────────────────────
        std::cout << "[3/9] Starting EventBus and DbWriter...\n";
        Events::EventBus        bus;
        Persistence::DbWriter   db_writer(*conn);
        db_writer.subscribe_all(bus);

        // ── STEP 4: PortfolioGuard ────────────────────────────
        std::cout << "[4/9] Creating PortfolioGuard...\n";
        Scanner::PortfolioConfig port_cfg = load_portfolio_config(sys);
        Scanner::PortfolioGuard  portfolio(port_cfg);

        LOG_INFO("[Main] PortfolioGuard: " << portfolio.status_string());

        // ── STEP 5: SymbolContexts ────────────────────────────
        // One LiveEngine + PipeBroker per active symbol.
        // Each context owns its engine thread.
        std::cout << "[5/9] Creating symbol contexts (LiveEngine per symbol)...\n";

        // Spring Boot order config
        // ⚠️  FIX: Core::SystemConfig (from system_config_loader.cpp) sometimes fails
        // to read order_submitter.enabled from JSON, leaving sys.order_submitter_enabled=false
        // even when the JSON has "enabled": true.
        // Workaround: read the order_submitter section directly using the OLD ::SystemConfig
        // class (jsoncpp-based, get_bool with explicit default=true), which is the same
        // approach LiveEngine uses internally and is known to work.
        ::SystemConfig direct_sys_cfg;
        {
            std::string cfg_path = Utils::SystemInitializer::getInstance().getConfigPath();
            if (!cfg_path.empty()) {
                try { direct_sys_cfg = ::SystemConfig(cfg_path); } catch (...) {}
            }
        }
        bool order_enabled = direct_sys_cfg.get_bool("order_submitter", "enabled", true);

        Live::OrderSubmitConfig order_cfg;
        order_cfg.base_url              = sys.order_submitter_base_url;
        order_cfg.long_strategy_number  = sys.order_submitter_long_strategy;
        order_cfg.short_strategy_number = sys.order_submitter_short_strategy;
        order_cfg.timeout_seconds       = sys.order_submitter_timeout;
        order_cfg.enable_submission     = order_enabled && !rc.dry_run;

        std::cout << "\n[OrderSubmitConfig] enable_submission=" << order_cfg.enable_submission
                  << "  (json_enabled=" << order_enabled
                  << "  sys.order_submitter_enabled=" << sys.order_submitter_enabled
                  << "  rc.dry_run=" << rc.dry_run << ")\n"
                  << "  base_url=" << order_cfg.base_url << "\n\n";
        std::cout.flush();

        // Load symbol registry
        std::string reg_path = sys.get_full_path(sys.symbol_registry_file);
        std::ifstream reg_file(reg_path);
        if (!reg_file.is_open()) {
            std::cerr << "ERROR: Cannot open symbol registry: " << reg_path << "\n";
            return 1;
        }
        json reg;
        reg_file >> reg;

        std::string cascade_root = sys.get_full_path(sys.config_root_dir);
        std::string output_dir   = sys.get_full_path(sys.live_trade_logs_dir);
        fs::create_directories(output_dir);

        // Create one SymbolContext per active symbol
        std::vector<std::unique_ptr<Scanner::SymbolContext>> contexts;

        for (const auto& sym_json : reg["symbols"]) {
            if (!sym_json.value("active", false)) continue;

            Scanner::SymbolProfile profile;
            profile.symbol       = sym_json.value("symbol",       "");
            profile.display_name = sym_json.value("display_name", profile.symbol);
            profile.lot_size     = sym_json.value("lot_size",     65);
            profile.config_path  = sym_json.value("config_file_path", "");

            if (profile.symbol.empty()) continue;

            std::cout << "    Creating context: " << profile.symbol
                      << " (lot=" << profile.lot_size << ")\n";

            auto ctx = std::make_unique<Scanner::SymbolContext>(
                profile,
                order_cfg,
                portfolio,
                cascade_root,
                output_dir,
                db_path,
                &bus);     // V8: publish zones to DB via EventBus

            contexts.push_back(std::move(ctx));
        }

        if (contexts.empty()) {
            std::cerr << "ERROR: No active symbols in registry: " << reg_path << "\n";
            return 1;
        }

        // ── STEP 6: BarRouter ─────────────────────────────────
        // Routes BarValidatedEvents → correct SymbolContext.push_bar()
        std::cout << "[6/9] Creating BarRouter...\n";
        Scanner::BarRouter bar_router(bus);
        for (auto& ctx : contexts) {
            bar_router.register_symbol(*ctx);
        }
        bar_router.subscribe(bus);

        // ── STEP 7: PipeListeners ─────────────────────────────
        std::cout << "[7/9] Starting AmiBroker pipe listeners...\n";

        Data::ValidatorConfig    val_cfg;
        Data::PipeListenerConfig pipe_cfg;

        // ── Read bar validation config from MASTER.config ──────────────
        // Parses bar_validation_* keys directly from the master config file.
        // All thresholds have sensible defaults already set in ValidatorConfig.
        {
            // Derive master config path from system config root
            // sys is available from STEP 1 (Core::SystemConfig).
            // config_root_dir points to the conf/ directory — same path the
            // cascade resolver uses for MASTER.config.
            const std::string master_path =
                sys.get_full_path(sys.config_root_dir) + "/MASTER.config";

            std::ifstream mf(master_path);
            if (mf.is_open()) {
                std::string line;
                while (std::getline(mf, line)) {
                    // Strip comments and whitespace
                    auto hash = line.find('#');
                    if (hash != std::string::npos) line = line.substr(0, hash);
                    line.erase(0, line.find_first_not_of(" \t\r\n"));
                    line.erase(line.find_last_not_of(" \t\r\n") + 1);
                    if (line.empty()) continue;

                    auto eq = line.find('=');
                    if (eq == std::string::npos) continue;
                    std::string k = line.substr(0, eq);
                    std::string v = line.substr(eq + 1);
                    k.erase(k.find_last_not_of(" \t") + 1);
                    v.erase(0, v.find_first_not_of(" \t"));

                    auto is_yes = [](const std::string& s) {
                        return s == "YES" || s == "yes" || s == "true" || s == "1";
                    };

                    if      (k == "bar_validation_enabled")
                        val_cfg.enabled               = is_yes(v);
                    else if (k == "bar_validation_tier1_structural_enabled")
                        val_cfg.tier1_structural_enabled = is_yes(v);
                    else if (k == "bar_validation_max_gap_pct")
                        val_cfg.max_gap_pct           = std::stod(v);
                    else if (k == "bar_validation_atr_spike")
                        val_cfg.atr_spike_multiplier  = std::stod(v);
                    else if (k == "bar_validation_vol_spike")
                        val_cfg.max_volume_spike_mult = std::stod(v);
                    else if (k == "bar_validation_oi_spike")
                        val_cfg.max_oi_spike_pct      = std::stod(v);
                    else if (k == "bar_validation_skip_09_15")
                        val_cfg.skip_first_bar_of_day = is_yes(v);
                    else if (k == "bar_validation_vol_min_bars")
                        val_cfg.volume_min_bars       = std::stoi(v);
                }
                LOG_INFO("[V8] Bar validation config loaded from " << master_path);
            } else {
                LOG_WARN("[V8] Could not open " << master_path
                         << " — using built-in bar validation defaults");
            }

            if (!val_cfg.enabled && !val_cfg.tier1_structural_enabled) {
                LOG_INFO("[V8] Bar validation: FULLY DISABLED (tier-1/2/3 bypassed)");
            } else if (!val_cfg.enabled) {
                LOG_INFO("[V8] Bar validation: PARTIAL (tier-2/3 bypassed, tier-1="
                         << (val_cfg.tier1_structural_enabled ? "ON" : "OFF") << ")");
            } else {
                LOG_INFO("[V8] Bar validation: ENABLED"
                         << " tier1=" << (val_cfg.tier1_structural_enabled ? "ON" : "OFF")
                         << " gap=" << val_cfg.max_gap_pct << "%"
                         << " atr=" << val_cfg.atr_spike_multiplier << "x"
                         << " vol=" << val_cfg.max_volume_spike_mult << "x"
                         << " oi="  << val_cfg.max_oi_spike_pct << "%"
                         << " skip_09:15=" << (val_cfg.skip_first_bar_of_day ? "YES" : "NO")
                         << " vol_min_bars=" << val_cfg.volume_min_bars);
            }
        }

        std::vector<std::unique_ptr<Data::PipeListener>> pipe_listeners;

        for (const auto& sym_json : reg["symbols"]) {
            if (!sym_json.value("active", false)) continue;
            std::string symbol = sym_json.value("symbol", "");
            if (symbol.empty()) continue;

            auto listener = std::make_unique<Data::PipeListener>(
                symbol, "5min", val_cfg, pipe_cfg, bus);
            listener->start();
            std::cout << "    Pipe listener: " << symbol
                      << " → \\\\.\\pipe\\SD_" << symbol << "_5min\n";
            pipe_listeners.push_back(std::move(listener));
        }

        // ── STEP 8: Start SymbolContexts (engine threads) ─────
        // Start EventBus BEFORE engine threads so zone snapshot events
        // published during LiveEngine::initialize() are processed immediately.
        bus.start();

        std::cout << "[8/9] Starting LiveEngine threads...\n";
        for (auto& ctx : contexts) {
            ctx->start();
            std::cout << "    LiveEngine started: " << ctx->symbol() << "\n";
        }

        // ── STEP 9: Spring Boot bridge ────────────────────────
        // TradeCloseListener polls SQLite for closed trades.
        // On close: notifies PortfolioGuard → unblocks next entry.
        std::cout << "[9/9] Starting Spring Boot bridge...\n";

        SpringBoot::SpringBootBridgeConfig bridge_cfg;
        bridge_cfg.signal.dry_run            = rc.dry_run;
        bridge_cfg.signal.enabled            = true;
        bridge_cfg.close_listener.poll_interval_seconds = 5;

        SpringBoot::SpringBootBridge bridge(bridge_cfg, order_cfg, *conn, bus);

        // Wire trade-close events to PortfolioGuard
        // TradeCloseListener publishes TradeCloseEvent → we subscribe
        bus.subscribe_async<Events::TradeCloseEvent>(
            "portfolio_close_sync",
            [&portfolio](const Events::TradeCloseEvent& evt) {
                portfolio.notify_position_closed(
                    evt.symbol,
                    evt.exit_price,
                    evt.net_pnl);
                LOG_INFO("[Main] PortfolioGuard updated on close: "
                         << portfolio.status_string());
            });

        bridge.start();

        // EventBus started earlier (before engine threads — see STEP 8)

        // ── Ready ─────────────────────────────────────────────
        std::cout << "\n";
        std::cout << "════════════════════════════════════════════════\n";
        std::cout << "  LIVE — Press Ctrl+C to stop\n";
        std::cout << "  Active symbols: " << contexts.size() << "\n";
        for (auto& ctx : contexts) {
            std::cout << "    • " << ctx->symbol() << "\n";
        }
        std::cout << "  Portfolio capital: ₹"
                  << std::fixed << std::setprecision(0)
                  << portfolio.capital_available() << "\n";
        if (rc.dry_run)
            std::cout << "  MODE: DRY-RUN (signals logged, no real orders)\n";
        std::cout << "════════════════════════════════════════════════\n\n";

        // ── STATUS line every 5 minutes ──────────────────────
        auto last_status = std::chrono::steady_clock::now();

        while (!g_shutdown) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
                now - last_status).count();
            if (elapsed >= 5) {
                last_status = now;
                std::cout << "[STATUS] " << portfolio.status_string()
                          << " | orders=" << bridge.orders_submitted()
                          << " closes="  << bridge.closes_detected()
                          << " db_queue="<< bus.async_queue_depth()
                          << "\n";
            }
        }

        // ── SHUTDOWN SEQUENCE ─────────────────────────────────
        std::cout << "\n[SHUTDOWN] Stopping components...\n";

        // 1. Stop pipe listeners — no new bars enter the system
        for (auto& pl : pipe_listeners) pl->stop();
        std::cout << "  Pipe listeners stopped.\n";

        // 2. Stop LiveEngine threads (signals PipeBroker shutdown)
        for (auto& ctx : contexts) ctx->stop();
        std::cout << "  LiveEngine threads stopped.\n";

        // 3. Stop Spring Boot bridge
        bridge.stop();
        std::cout << "  Spring Boot bridge stopped.\n";

        // 4. Drain EventBus async queue — all DB writes flushed
        bus.stop();
        std::cout << "  EventBus stopped. All DB writes flushed.\n";

        // ── SESSION METRICS ───────────────────────────────────
        std::cout << "\n[METRICS] Computing session metrics...\n";
        Analytics::MetricsAggregator agg(*conn);
        Analytics::PortfolioMetrics  portfolio_metrics =
            agg.compute_portfolio(sys.trading_starting_capital);
        agg.persist_portfolio(portfolio_metrics);
        print_summary(portfolio_metrics);

        // Export per-symbol trade CSV
        for (const auto& s : portfolio_metrics.by_symbol) {
            std::string csv_content = db_writer.export_trades_csv(s.symbol);
            std::string out_path    = output_dir + "/" + s.symbol + "_trades.csv";
            fs::create_directories(fs::path(out_path).parent_path());
            std::ofstream f(out_path);
            f << csv_content;
            std::cout << "  Trades CSV: " << out_path << "\n";
        }

        std::cout << "\n[DONE] Session complete.\n";
        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "\nFATAL ERROR: " << ex.what() << "\n";
        return 1;
    }
}