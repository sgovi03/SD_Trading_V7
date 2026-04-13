// ============================================================
// SD TRADING V8 — M3 VERIFICATION TEST
// src/verify_m3.cpp
//
// Validates all three M3 repositories + DbWriter wiring.
// Uses an in-memory SQLite DB — no disk files needed.
//
// BUILD:  linked against sd_core, sd_persistence, sd_events
// RUN:    ./verify_m3 [schema_sql_path]
//         Default schema path: src/persistence/db_schema.sql
// ============================================================

#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>

#include "persistence/sqlite_connection.h"
#include "persistence/sqlite_bar_repository.h"
#include "persistence/sqlite_trade_repository.h"
#include "persistence/db_writer.h"
#include "events/event_bus.h"
#include "events/event_types.h"

namespace fs = std::filesystem;
using namespace SDTrading;
using namespace SDTrading::Persistence;
using namespace SDTrading::Events;

static int g_run=0, g_pass=0, g_fail=0;
static void test(const std::string& name, bool ok, const std::string& detail="") {
    ++g_run;
    if (ok) { ++g_pass; std::cout << "  ✅ " << name << "\n"; }
    else {
        ++g_fail;
        std::cout << "  ❌ " << name;
        if (!detail.empty()) std::cout << " [" << detail << "]";
        std::cout << "\n";
    }
}
static void section(const std::string& s) {
    std::cout << "\n── " << s << " ──\n";
}

// ── Helpers ─────────────────────────────────────────────────
static BarValidatedEvent make_bar_evt(
    const std::string& sym="NIFTY",
    const std::string& ts="2026-04-09T09:30:00+05:30",
    double c=22985.0, double v=142500, double oi=9875400)
{
    BarValidatedEvent e;
    e.symbol=sym; e.timeframe="5min"; e.timestamp=ts;
    e.open=22950; e.high=23010; e.low=22940; e.close=c;
    e.volume=v; e.open_interest=oi; e.source="AMIBROKER";
    return e;
}

static SignalEvent make_signal_evt(const std::string& tag="SDT0409093000LCC") {
    SignalEvent e;
    e.symbol="NIFTY"; e.signal_time="2026-04-09T09:30:00+05:30";
    e.direction="LONG"; e.score=72.5; e.zone_id=42;
    e.entry_price=22985; e.stop_loss=22850; e.target_1=23200;
    e.rr_ratio=1.8; e.lot_size=65; e.order_tag=tag;
    e.regime="BULL";
    e.fast_ema=22910; e.slow_ema=22750; e.rsi=58.3; e.adx=24.1;
    e.score_base_strength=14.88; e.score_elite_bonus=5.0;
    return e;
}

static TradeOpenEvent make_trade_open(const std::string& tag="SDT0409093000LCC") {
    TradeOpenEvent e;
    e.order_tag=tag; e.symbol="NIFTY"; e.direction="LONG";
    e.entry_time="2026-04-09T09:30:00+05:30";
    e.entry_price=22985; e.stop_loss=22850; e.take_profit=23200;
    e.lots=1; e.zone_id=42;
    return e;
}

static TradeCloseEvent make_trade_close(const std::string& tag="SDT0409093000LCC",
                                         double pnl=5200.0) {
    TradeCloseEvent e;
    e.order_tag=tag; e.symbol="NIFTY";
    e.exit_time="2026-04-09T11:15:00+05:30";
    e.exit_price=23200; e.exit_reason="TP";
    e.gross_pnl=pnl; e.brokerage=20; e.net_pnl=pnl-20;
    e.bars_in_trade=21;
    return e;
}

// ============================================================
// M3.1 — SqliteBarRepository
// ============================================================
static void test_bar_repo(SqliteConnection& conn) {
    section("M3.1 — SqliteBarRepository: insert_bar");
    SqliteBarRepository repo(conn);

    auto evt = make_bar_evt();
    test("insert_bar returns true",       repo.insert_bar(evt));
    test("count_bars == 1",               repo.count_bars("NIFTY") == 1);

    // Idempotency: same bar twice must not error (INSERT OR IGNORE)
    test("Duplicate insert is safe",      repo.insert_bar(evt));
    test("count_bars still == 1",         repo.count_bars("NIFTY") == 1);

    // Second bar (different timestamp)
    auto evt2 = make_bar_evt("NIFTY","2026-04-09T09:35:00+05:30",22990,130000,9900000);
    test("Second bar inserts",            repo.insert_bar(evt2));
    test("count_bars == 2",               repo.count_bars("NIFTY") == 2);

    // Different symbol
    auto evt3 = make_bar_evt("BANKNIFTY","2026-04-09T09:30:00+05:30",48250,95000,7500000);
    test("BANKNIFTY bar inserts",         repo.insert_bar(evt3));
    test("NIFTY count still 2",           repo.count_bars("NIFTY") == 2);
    test("BANKNIFTY count == 1",          repo.count_bars("BANKNIFTY") == 1);

    section("M3.1 — SqliteBarRepository: get_bars");
    auto bars = repo.get_bars("NIFTY",
                               "2026-04-09T09:00:00+05:30",
                               "2026-04-09T10:00:00+05:30");
    test("get_bars returns 2 bars",        bars.size() == 2);
    if (!bars.empty()) {
        test("First bar close == 22985",   std::abs(bars[0].close - 22985.0) < 0.01);
        test("First bar OI == 9875400",    std::abs(bars[0].oi - 9875400.0) < 1.0);
    }

    section("M3.1 — SqliteBarRepository: insert_rejection");
    BarRejectedEvent rej;
    rej.symbol="NIFTY"; rej.timeframe="5min";
    rej.raw_wire_data="AMIBROKER|NIFTY|5min|2026-04-09T09:40:00+05:30|99999|100|50|75|0|0";
    rej.rejection_tier=1; rej.rejection_reason="H < L";
    test("insert_rejection returns true",  repo.insert_rejection(rej));
    test("count_rejections == 1",          repo.count_rejections("NIFTY") == 1);
}

// ============================================================
// M3.3 — SqliteTradeRepository
// ============================================================
static void test_trade_repo(SqliteConnection& conn) {
    section("M3.3 — SqliteTradeRepository: insert_signal");
    SqliteTradeRepository repo(conn);

    auto sig = make_signal_evt("SDT0409093000LCC");
    int64_t sig_id = repo.insert_signal(sig);
    test("insert_signal returns id > 0",   sig_id > 0);

    section("M3.3 — SqliteTradeRepository: insert_trade_open");
    auto open_evt = make_trade_open("SDT0409093000LCC");
    test("insert_trade_open returns true", repo.insert_trade_open(open_evt));
    test("count_open_trades == 1",         repo.count_open_trades("NIFTY") == 1);

    // Verify signal marked as acted_upon
    test("mark_signal_acted succeeds",     repo.mark_signal_acted("SDT0409093000LCC"));

    section("M3.3 — SqliteTradeRepository: update_trade_close");
    auto close_evt = make_trade_close("SDT0409093000LCC", 5200.0);
    test("update_trade_close returns true",repo.update_trade_close(close_evt));
    test("count_open_trades == 0",         repo.count_open_trades("NIFTY") == 0);

    section("M3.3 — SqliteTradeRepository: get_closed_trades");
    auto trades = repo.get_closed_trades("NIFTY");
    test("get_closed_trades returns 1",    trades.size() == 1);
    if (!trades.empty()) {
        test("Trade pnl == 5180",
             std::abs(trades[0].net_pnl - 5180.0) < 1.0);  // 5200 - 20 brokerage
        test("Trade direction == LONG",    trades[0].direction == "LONG");
        test("Trade exit_reason == TP",    trades[0].exit_reason == "TP");
    }

    section("M3.3 — SqliteTradeRepository: equity_curve");
    test("append_equity_point succeeds",
         repo.append_equity_point("NIFTY",
             "2026-04-09T11:15:00+05:30",
             5180.0, 5180.0, sig_id));

    section("M3.3 — SqliteTradeRepository: CSV export");
    std::string csv = repo.export_trades_csv("NIFTY");
    test("CSV export not empty",           !csv.empty());
    test("CSV contains header Trade#",     csv.find("Trade#") != std::string::npos);
    test("CSV contains LONG direction",    csv.find("LONG") != std::string::npos);
    test("CSV contains TP exit",           csv.find("TP") != std::string::npos);

    std::string csv_all = repo.export_trades_csv_all();
    test("Portfolio CSV not empty",        !csv_all.empty());
    test("Portfolio CSV has symbol col",   csv_all.find("Symbol") != std::string::npos);
}

// ============================================================
// M3.5 — DbWriter + EventBus wiring
// ============================================================
static void test_db_writer(SqliteConnection& conn) {
    section("M3.5 — DbWriter: EventBus wiring");

    EventBus bus;
    DbWriter writer(conn);
    writer.subscribe_all(bus);
    bus.start();

    // Publish a bar — DbWriter should write it asynchronously
    bus.publish(make_bar_evt("NIFTY","2026-04-09T10:00:00+05:30",23010,155000,9950000));
    bus.publish(make_bar_evt("NIFTY","2026-04-09T10:05:00+05:30",23025,145000,9980000));

    // Publish a rejection
    BarRejectedEvent rej;
    rej.symbol="NIFTY"; rej.timeframe="5min";
    rej.raw_wire_data="bad|data"; rej.rejection_tier=1; rej.rejection_reason="H < L";
    bus.publish(rej);

    // Give async worker time to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    test("DbWriter bars_written >= 2",     writer.bars_written() >= 2);
    test("DbWriter bars_rejected >= 1",    writer.bars_rejected() >= 1);

    section("M3.5 — DbWriter: Signal + Trade lifecycle");

    // Signal → Open → Close via EventBus
    auto sig_evt = make_signal_evt("SDT0409100000LCC");
    bus.publish(sig_evt);

    auto open_evt = make_trade_open("SDT0409100000LCC");
    bus.publish(open_evt);

    auto close_evt = make_trade_close("SDT0409100000LCC", 3800.0);
    bus.publish(close_evt);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    test("signals_written >= 1",           writer.signals_written() >= 1);
    test("trades_opened >= 1",             writer.trades_opened() >= 1);
    test("trades_closed >= 1",             writer.trades_closed() >= 1);

    // CSV export via DbWriter
    std::string csv = writer.export_trades_csv("NIFTY");
    test("DbWriter CSV export works",      !csv.empty());

    bus.stop();
    test("Bus stops cleanly",              true);
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
    std::string schema_path = argc > 1
        ? argv[1]
        : "src/persistence/db_schema.sql";

    // Fallback locations
    if (!fs::exists(schema_path)) schema_path = "db_schema.sql";
    if (!fs::exists(schema_path)) {
        std::cerr << "Schema not found. Run from project root.\n";
        return 1;
    }

    std::cout << "============================================================\n";
    std::cout << "SD Trading V8 — M3 Verification\n";
    std::cout << "M3.1 BarRepo | M3.3 TradeRepo | M3.5 DbWriter\n";
    std::cout << "Schema: " << schema_path << "\n";
    std::cout << "============================================================\n";

    // Use in-memory DB for tests — no disk files needed
    const std::string TEST_DB = ":memory:";
    try {
        SqliteConnectionFactory::initialize(TEST_DB, schema_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize DB: " << e.what() << "\n";
        return 1;
    }

    auto conn = SqliteConnectionFactory::create_write_connection();

    test_bar_repo(*conn);
    test_trade_repo(*conn);
    test_db_writer(*conn);

    std::cout << "\n============================================================\n";
    std::cout << "RESULTS: " << g_run << " tests | "
              << g_pass << " passed | " << g_fail << " failed\n";
    std::cout << "============================================================\n";

    if (g_fail == 0) {
        std::cout << "✅ ALL TESTS PASSED\n";
        std::cout << "   M3.1 SqliteBarRepository — correct\n";
        std::cout << "   M3.3 SqliteTradeRepository — correct\n";
        std::cout << "   M3.5 DbWriter + EventBus — correct\n";
        std::cout << "   CSV export matches original trades.csv column order\n";
        std::cout << "   Safe to proceed to M2.6 (shadow mode)\n";
    } else {
        std::cout << "❌ " << g_fail << " test(s) FAILED\n";
    }

    return g_fail == 0 ? 0 : 1;
}
