// ============================================================
// SD TRADING V8 — M0.2 + M1.1 VERIFICATION TEST
// src/verify_m0_m1.cpp
//
// PURPOSE:
//   Validates M0.2 (SQLite schema) and M1.1 (Config cascade)
//   are correct BEFORE touching any engine code.
//
// CRITICAL REGRESSION CHECK:
//   NIFTY cascade must produce a Config that is field-for-field
//   identical to loading the old flat phase_6_config_v0_7.txt.
//   Any difference = regression that must be fixed before going live.
//
// BUILD:
//   Linked against: sd_core, sd_persistence, sqlite3
//
// RUN:
//   ./verify_m0_m1 [config_root] [old_flat_config] [db_path]
//
//   Defaults:
//     config_root     = config
//     old_flat_config = conf/phase_6_config_v0_7.txt
//     db_path         = data/sd_trading_v8_test.db
// ============================================================

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cmath>

#include "core/config_cascade.h"
#include "core/config_loader.h"
#include "persistence/sqlite_connection.h"

namespace fs = std::filesystem;
using namespace SDTrading;
using namespace SDTrading::Core;
using namespace SDTrading::Persistence;

// ============================================================
// Test infrastructure
// ============================================================

static int g_run = 0, g_pass = 0, g_fail = 0;

static void test(const std::string& name, bool condition,
                 const std::string& detail = "") {
    ++g_run;
    if (condition) {
        ++g_pass;
        std::cout << "  ✅ " << name << "\n";
    } else {
        ++g_fail;
        std::cout << "  ❌ " << name;
        if (!detail.empty()) std::cout << " [" << detail << "]";
        std::cout << "\n";
    }
}

static void section(const std::string& name) {
    std::cout << "\n── " << name << " ──\n";
}

// ============================================================
// M0.2: SQLite schema tests
// ============================================================

static void test_schema(const std::string& db_path,
                         const std::string& schema_path) {
    section("M0.2 — SQLite Schema Creation");

    if (fs::exists(db_path)) fs::remove(db_path);

    // Factory initialisation
    try {
        SqliteConnectionFactory::initialize(db_path, schema_path);
        test("Factory initializes", true);
    } catch (const std::exception& e) {
        test("Factory initializes", false, e.what());
        return;
    }

    auto conn = SqliteConnectionFactory::create_write_connection();
    test("Write connection opens",    conn && conn->is_open());
    test("Schema version == 1",       conn->get_schema_version() == 1);

    // Table existence helper
    auto has_table = [&](const std::string& t) {
        try {
            auto s = conn->prepare(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?");
            s.bind_text(1, t);
            s.step();
            return s.column_int(0) == 1;
        } catch (...) { return false; }
    };

    auto has_view = [&](const std::string& v) {
        try {
            auto s = conn->prepare(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='view' AND name=?");
            s.bind_text(1, v);
            s.step();
            return s.column_int(0) == 1;
        } catch (...) { return false; }
    };

    section("M0.2 — Tables");
    test("symbol_registry",        has_table("symbol_registry"));
    test("bars",                   has_table("bars"));
    test("bar_rejections",         has_table("bar_rejections"));
    test("zones",                  has_table("zones"));
    test("signals",                has_table("signals"));
    test("trades",                 has_table("trades"));
    test("equity_curve",           has_table("equity_curve"));
    test("equity_curve_portfolio", has_table("equity_curve_portfolio"));
    test("metrics",                has_table("metrics"));
    test("session_state",          has_table("session_state"));
    test("config_audit",           has_table("config_audit"));
    test("zone_id_sequence",       has_table("zone_id_sequence"));
    test("schema_migrations",      has_table("schema_migrations"));

    section("M0.2 — Views");
    test("v_trades_master",        has_view("v_trades_master"));
    test("v_metrics_summary",      has_view("v_metrics_summary"));
    test("v_zone_health",          has_view("v_zone_health"));

    section("M0.2 — NIFTY Seed Data");
    {
        auto s = conn->prepare(
            "SELECT symbol, lot_size, active FROM symbol_registry WHERE symbol='NIFTY'");
        bool found = s.step();
        test("NIFTY row exists",       found);
        if (found) {
            test("NIFTY lot_size = 75", s.column_int(1) == 75);
            test("NIFTY active = 1",    s.column_bool(2));
        }
    }

    section("M0.2 — WAL Mode");
    {
        auto s = conn->prepare("PRAGMA journal_mode");
        s.step();
        test("WAL mode active", s.column_text(0) == "wal");
    }

    section("M0.2 — Idempotency");
    try {
        conn->apply_schema(schema_path);
        test("Re-apply schema is safe", true);
    } catch (const std::exception& e) {
        test("Re-apply schema is safe", false, e.what());
    }

    section("M0.2 — Transaction Round-trip");
    try {
        conn->with_transaction([&]() {
            conn->execute_sql(
                "INSERT OR IGNORE INTO symbol_registry "
                "(symbol,display_name,asset_class,exchange,lot_size,tick_size) "
                "VALUES ('ZTEST','Test','INDEX_FUTURES','NSE',1,0.05)");
        });
        auto s = conn->prepare(
            "SELECT COUNT(*) FROM symbol_registry WHERE symbol='ZTEST'");
        s.step();
        test("INSERT + COMMIT visible", s.column_int(0) == 1);
        conn->execute_sql("DELETE FROM symbol_registry WHERE symbol='ZTEST'");
    } catch (const std::exception& e) {
        test("INSERT + COMMIT visible", false, e.what());
    }

    section("M0.2 — Concurrent Read Connection (WAL)");
    try {
        auto rc = SqliteConnectionFactory::create_read_connection();
        test("Read connection opens", rc && rc->is_open());
        auto s = rc->prepare("SELECT COUNT(*) FROM symbol_registry");
        s.step();
        test("Read sees symbol_registry", s.column_int(0) >= 1);
    } catch (const std::exception& e) {
        test("Concurrent read connection", false, e.what());
    }

    section("M0.2 — Prepared Statement Binding");
    try {
        // Test each bind type in a single query
        auto s = conn->prepare(
            "SELECT ?, ?, ?, ?");
        s.bind_int(1, 42);
        s.bind_double(2, 3.14);
        s.bind_text(3, "hello");
        s.bind_bool(4, true);
        s.step();
        test("bind_int round-trip",    s.column_int(0) == 42);
        test("bind_double round-trip", std::abs(s.column_double(1) - 3.14) < 1e-9);
        test("bind_text round-trip",   s.column_text(2) == "hello");
        test("bind_bool round-trip",   s.column_bool(3));
    } catch (const std::exception& e) {
        test("Prepared statement binding", false, e.what());
    }
}

// ============================================================
// M1.1: Symbol registry tests
// ============================================================

static SymbolRegistry test_registry(const std::string& config_root) {
    section("M1.1 — Symbol Registry Load");

    std::string registry_path = config_root + "/symbol_registry.json";
    SymbolRegistry registry;

    try {
        registry = SymbolRegistry::load_from_json(registry_path);
        test("symbol_registry.json loads",      true);
    } catch (const std::exception& e) {
        test("symbol_registry.json loads",      false, e.what());
        return registry;
    }

    test("At least 1 symbol in registry",   !registry.symbols.empty());

    auto active = registry.get_active_symbols();
    test("At least 1 active symbol",        !active.empty());

    const SymbolProfile* nifty = registry.find("NIFTY");
    test("NIFTY found in registry",         nifty != nullptr);

    if (nifty) {
        test("NIFTY lot_size = 75",         nifty->lot_size == 75);
        test("NIFTY tick_size = 0.05",      std::abs(nifty->tick_size - 0.05) < 1e-9);
        test("NIFTY asset_class correct",   nifty->asset_class == "INDEX_FUTURES");
        test("NIFTY active = true",         nifty->active);
        test("NIFTY profile is_valid()",    nifty->is_valid());
        test("NIFTY expiry_day set",        !nifty->expiry_day.empty());
        test("NIFTY live_feed_ticker set",  !nifty->live_feed_ticker.empty());
    }

    // BANKNIFTY should be in registry but inactive
    const SymbolProfile* bnf = registry.find("BANKNIFTY");
    if (bnf) {
        test("BANKNIFTY lot_size = 15",     bnf->lot_size == 15);
        test("BANKNIFTY active = false",    !bnf->active);
    }

    return registry;
}

// ============================================================
// M1.1: Three-tier cascade tests
// ============================================================

static ResolvedConfig test_cascade(const std::string& config_root,
                                    const SymbolProfile& nifty_profile) {
    section("M1.1 — Three-Tier Cascade (NIFTY)");

    ResolvedConfig resolved;
    try {
        resolved = ConfigCascadeResolver::resolve(config_root, nifty_profile);
        test("resolve() completes without exception", true);
    } catch (const std::exception& e) {
        test("resolve() completes without exception", false, e.what());
        return resolved;
    }

    test("ResolvedConfig is_valid()",           resolved.is_valid());
    test("Profile symbol = NIFTY",              resolved.profile.symbol == "NIFTY");
    test("Profile lot_size = 75",               resolved.profile.lot_size == 75);
    test("Config hash not empty",               !resolved.report.config_hash.empty());
    test("Config hash = 16 hex chars",          resolved.report.config_hash.size() == 16);
    test("MASTER.config path recorded",         !resolved.report.master_path.empty());
    test("Not in fallback mode",                !resolved.report.used_fallback_mode);

    // lot_size must come from profile, not config file
    test("lot_size = 75 (from profile)",        resolved.config.lot_size == 75);

    // Verify V7 production values are present from MASTER.config
    test("starting_capital = 300000",
         std::abs(resolved.config.starting_capital - 300000.0) < 1.0);
    test("min_zone_width_atr = 0.705",
         std::abs(resolved.config.min_zone_width_atr - 0.705) < 0.001);
    test("min_zone_strength = 63.5",
         std::abs(resolved.config.min_zone_strength - 63.5) < 0.001);
    test("use_trailing_stop = true",            resolved.config.use_trailing_stop);
    test("trail_activation_rr ≈ 0.55",
         std::abs(resolved.config.trail_activation_rr - 0.55) < 0.001);
    test("max_loss_per_trade = 1500",
         std::abs(resolved.config.max_loss_per_trade - 1500.0) < 1.0);
    test("max_zone_age_days = 60",              resolved.config.max_zone_age_days == 60);
    test("max_touch_count = 200",               resolved.config.max_touch_count == 200);
    test("require_ema_longs = true",            resolved.config.require_ema_alignment_for_longs);
    test("close_eod = true",                    resolved.config.close_eod);
    test("enable_oi_entry_filters = true",      resolved.config.enable_oi_entry_filters);
    test("use_relaxed_live_detection = true",   resolved.config.use_relaxed_live_detection);
    test("skip_violated_zones = true",          resolved.config.skip_violated_zones);
    test("duplicate_prevention = true",         resolved.config.duplicate_prevention_enabled);
    test("consolidation_min_bars = 2",          resolved.config.consolidation_min_bars == 2);
    test("atr_period = 14",                     resolved.config.atr_period == 14);

    // Verify MASTER→CLASS→SYMBOL inheritance is visible in report
    // (class path may or may not exist; symbol path should exist for NIFTY)
    if (!resolved.report.symbol_path.empty()) {
        test("SYMBOL config path recorded",     true);
    }

    return resolved;
}

// ============================================================
// M1.1: Regression check (cascade == flat)
// ============================================================

static void test_regression(const ResolvedConfig& cascade_result,
                              const std::string& flat_config_path) {
    section("M1.1 — Regression: Cascade == Flat Config");

    if (!fs::exists(flat_config_path)) {
        std::cout << "  ⚠️  Flat config not found: " << flat_config_path << "\n";
        std::cout << "  ⚠️  Regression test skipped (run with correct path)\n";
        std::cout << "  ⚠️  Usage: ./verify_m0_m1 config conf/phase_6_config_v0_7.txt\n";
        return;
    }

    Config flat_config;
    try {
        flat_config = ConfigLoader::load_from_file(flat_config_path);
        test("Old flat config loads", true);
    } catch (const std::exception& e) {
        test("Old flat config loads", false, e.what());
        return;
    }

    auto diffs = ConfigCascadeResolver::compare_configs(
        flat_config, cascade_result.config);

    if (diffs.empty()) {
        test("CASCADE ≡ FLAT: zero field differences", true);
    } else {
        test("CASCADE ≡ FLAT: zero field differences", false,
             std::to_string(diffs.size()) + " difference(s)");
        std::cout << "\n  ⚠️  Fields that differ (investigate before going live):\n";
        for (const auto& d : diffs) {
            std::cout << "       → " << d << "\n";
        }
        std::cout << "\n  ⚠️  Fix: ensure MASTER.config matches phase_6_config_v0_7.txt\n";
        std::cout << "  ⚠️  for all fields listed above.\n";
    }
}

// ============================================================
// M1.1: Serialisation & hashing tests
// ============================================================

static void test_serialisation(const ResolvedConfig& resolved) {
    section("M1.1 — Config Serialisation & Hashing");

    std::string json1 = ConfigCascadeResolver::serialize_config_to_json(resolved.config);
    std::string json2 = ConfigCascadeResolver::serialize_config_to_json(resolved.config);

    test("Serialisation is deterministic",      json1 == json2);
    test("Serialised JSON not empty",           !json1.empty());
    test("JSON contains starting_capital",
         json1.find("starting_capital") != std::string::npos);
    test("JSON contains min_zone_width_atr",
         json1.find("min_zone_width_atr") != std::string::npos);

    std::string h1 = ConfigCascadeResolver::hash_config(resolved.config);
    std::string h2 = ConfigCascadeResolver::hash_config(resolved.config);
    test("Hash is deterministic",               h1 == h2);
    test("Hash is 16 hex characters",           h1.size() == 16);
    test("Hash matches report hash",            h1 == resolved.report.config_hash);

    // Mutating config must change hash
    Config mutated = resolved.config;
    mutated.starting_capital = 999999.0;
    std::string h_mutated = ConfigCascadeResolver::hash_config(mutated);
    test("Mutated config → different hash",     h1 != h_mutated);

    // Fallback resolve_flat must produce same hash when loading same values
    // (tested separately in regression section)
}

// ============================================================
// M1.1: Fallback mode (backward compatibility)
// ============================================================

static void test_fallback(const std::string& flat_config_path,
                            const SymbolProfile& profile) {
    section("M1.1 — Fallback Mode (resolve_flat)");

    if (!fs::exists(flat_config_path)) {
        std::cout << "  ⚠️  Skipped — flat config not found\n";
        return;
    }

    ResolvedConfig flat_resolved;
    try {
        flat_resolved = ConfigCascadeResolver::resolve_flat(flat_config_path, profile);
        test("resolve_flat() succeeds",         true);
    } catch (const std::exception& e) {
        test("resolve_flat() succeeds",         false, e.what());
        return;
    }

    test("fallback_mode flag = true",           flat_resolved.report.used_fallback_mode);
    test("fallback_path recorded",              !flat_resolved.report.fallback_path.empty());
    test("lot_size = 75 from profile",          flat_resolved.config.lot_size == 75);

    // Direct load vs resolve_flat must be identical
    Config direct = ConfigLoader::load_from_file(flat_config_path);
    auto diffs = ConfigCascadeResolver::compare_configs(direct, flat_resolved.config);
    test("resolve_flat() ≡ direct load",        diffs.empty(),
         diffs.empty() ? "" : std::to_string(diffs.size()) + " diff(s)");
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
    std::string config_root     = argc > 1 ? argv[1] : "config";
    std::string old_flat_config = argc > 2 ? argv[2] : "conf/phase_6_config_v0_7.txt";
    std::string db_path         = argc > 3 ? argv[3] : "data/sd_trading_v8_test.db";

    // Schema path: look in common locations
    std::string schema_path;
    for (const auto& p : {
            "src/persistence/db_schema.sql",
            "../src/persistence/db_schema.sql",
            "db_schema.sql"}) {
        if (fs::exists(p)) { schema_path = p; break; }
    }
    if (schema_path.empty()) schema_path = "src/persistence/db_schema.sql";

    std::cout << "============================================================\n";
    std::cout << "SD Trading V8 — M0.2 + M1.1 Verification\n";
    std::cout << "============================================================\n";
    std::cout << "config_root:     " << config_root     << "\n";
    std::cout << "flat_config:     " << old_flat_config << "\n";
    std::cout << "db_path:         " << db_path         << "\n";
    std::cout << "schema_path:     " << schema_path     << "\n";

    // ---- M0.2: SQLite ----
    test_schema(db_path, schema_path);

    // ---- M1.1: Registry ----
    SymbolRegistry registry = test_registry(config_root);

    const SymbolProfile* nifty = registry.find("NIFTY");
    if (!nifty) {
        std::cout << "\n⚠️  NIFTY not in registry — skipping cascade tests\n";
    } else {
        // ---- M1.1: Cascade ----
        ResolvedConfig resolved = test_cascade(config_root, *nifty);

        // ---- M1.1: Regression ----
        if (resolved.is_valid()) {
            test_regression(resolved, old_flat_config);
        }

        // ---- M1.1: Serialisation ----
        if (resolved.is_valid()) {
            test_serialisation(resolved);
        }

        // ---- M1.1: Fallback ----
        test_fallback(old_flat_config, *nifty);
    }

    // ---- Summary ----
    std::cout << "\n============================================================\n";
    std::cout << "RESULTS: " << g_run << " tests | "
              << g_pass << " passed | " << g_fail << " failed\n";
    std::cout << "============================================================\n";

    if (g_fail == 0) {
        std::cout << "✅ ALL TESTS PASSED\n";
        std::cout << "   M0.2 (SQLite schema) — complete\n";
        std::cout << "   M1.1 (Config cascade) — complete\n";
        std::cout << "   Safe to proceed to M2 (AmiBroker Data Pipeline)\n";
    } else {
        std::cout << "❌ " << g_fail << " test(s) FAILED — fix before proceeding\n";
    }

    // Clean up test DB if it was a test run
    if (fs::exists(db_path) &&
        db_path.find("test") != std::string::npos) {
        fs::remove(db_path);
    }

    return g_fail == 0 ? 0 : 1;
}
