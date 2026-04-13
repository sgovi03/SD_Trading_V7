// ============================================================
// SD TRADING V8 - CONFIG CASCADE RESOLVER
// src/core/config_cascade.h
// Milestone M1.1
//
// Three-tier cascade:
//   MASTER.config          → all parameters + defaults
//   classes/{CLASS}.config → asset-class overrides (optional)
//   symbols/{SYMBOL}.config → symbol-specific overrides (optional)
//
// Resolution rules:
//   1. Start with Config default-constructed (C++ member defaults)
//   2. Apply MASTER.config  → sets full base
//   3. Apply CLASS config   → only keys present in file override
//   4. Apply SYMBOL config  → only keys present in file override
//   5. Apply SymbolProfile  → lot_size always wins over config file
//   6. Result: ResolvedConfig (immutable for session lifetime)
//
// Backward compatibility:
//   If MASTER.config not found, falls back to resolve_flat()
//   which loads the old single flat file (phase_6_config_v0_7.txt).
//   NIFTY continues to work with zero migration effort.
//
// Config resolved ONCE per symbol at startup.
// No file I/O during live trading session.
// ============================================================

#ifndef SDTRADING_CONFIG_CASCADE_H
#define SDTRADING_CONFIG_CASCADE_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <cstdint>
#include "common_types.h"
#include "config_loader.h"

namespace SDTrading {
namespace Core {

// ============================================================
// SYMBOL PROFILE
// Per-instrument metadata from symbol_registry.json.
// These fields are NOT in the Config struct — they are
// instrument-level facts (lot size, tick size, expiry) that
// override config values where relevant (e.g. lot_size).
// ============================================================

struct SymbolProfile {
    std::string symbol;             // "NIFTY"
    std::string display_name;       // "Nifty 50 Futures"
    std::string asset_class;        // "INDEX_FUTURES"
    std::string exchange;           // "NSE"
    int         lot_size   = 0;     // 75
    double      tick_size  = 0.0;   // 0.05
    double      margin_per_lot = 0.0;
    std::string expiry_day;         // "THURSDAY"
    std::string live_feed_ticker;   // "NSE:NIFTY26APRFUT"
    std::string config_file_path;   // "config/symbols/NIFTY.config"
    bool        active     = false;

    bool is_valid() const {
        return !symbol.empty() && lot_size > 0 && tick_size > 0.0;
    }
};

// ============================================================
// SYMBOL REGISTRY
// Loaded once at startup from symbol_registry.json.
// ============================================================

class SymbolRegistry {
public:
    std::vector<SymbolProfile> symbols;

    // Returns only profiles where active == true
    std::vector<SymbolProfile> get_active_symbols() const;

    // Find by symbol name. Returns nullptr if not found.
    const SymbolProfile* find(const std::string& symbol) const;

    // Load from symbol_registry.json (nlohmann/json)
    static SymbolRegistry load_from_json(const std::string& filepath);
};

// ============================================================
// CASCADE RESOLUTION REPORT
// Produced for each symbol. Stored in config_audit table.
// Allows exact reproduction of any historical trade decision.
// ============================================================

struct CascadeResolutionReport {
    std::string symbol;
    std::string asset_class;
    std::string master_path;
    std::string class_path;          // empty = not found / skipped
    std::string symbol_path;         // empty = not found / skipped
    std::set<std::string> class_overrides;   // keys applied from class config
    std::set<std::string> symbol_overrides;  // keys applied from symbol config
    std::vector<std::string> warnings;
    bool        used_fallback_mode = false;  // true = flat file fallback
    std::string fallback_path;
    std::string config_hash;         // FNV-1a of serialized resolved config
};

// ============================================================
// RESOLVED CONFIG
// Immutable result of cascade resolution.
// One per active symbol, created at startup and never changed.
// ============================================================

struct ResolvedConfig {
    Config                  config;   // Fully merged strategy config
    SymbolProfile           profile;  // Symbol metadata
    CascadeResolutionReport report;   // How it was resolved

    bool is_valid() const { return profile.is_valid(); }
};

// ============================================================
// CONFIG CASCADE RESOLVER
// Static utility class. Call resolve() once per symbol.
// ============================================================

class ConfigCascadeResolver {
public:

    // --------------------------------------------------------
    // PRIMARY: Three-tier cascade resolution.
    //
    // config_root expected layout:
    //   {config_root}/MASTER.config
    //   {config_root}/classes/INDEX_FUTURES.config  (optional)
    //   {config_root}/symbols/NIFTY.config          (optional)
    //
    // Falls back to resolve_flat() if MASTER.config absent.
    // Throws std::runtime_error if no config found at all.
    // --------------------------------------------------------
    static ResolvedConfig resolve(
        const std::string& config_root,
        const SymbolProfile& profile
    );

    // --------------------------------------------------------
    // FALLBACK: Single flat config file.
    // Preserves V7 behaviour exactly — used during migration.
    // --------------------------------------------------------
    static ResolvedConfig resolve_flat(
        const std::string& flat_config_path,
        const SymbolProfile& profile
    );

    // --------------------------------------------------------
    // REGRESSION CHECK
    // Compares two resolved configs field-by-field.
    // Returns list of field names that differ (empty = identical).
    //
    // Critical use: at startup verify that:
    //   resolve("config/", nifty_profile) ≡ resolve_flat("conf/phase_6.txt")
    // Any diff = regression to investigate before going live.
    // --------------------------------------------------------
    static std::vector<std::string> compare_configs(
        const Config& a,
        const Config& b
    );

    // --------------------------------------------------------
    // SERIALISATION
    // Stable JSON of key Config fields.
    // Stored in config_audit table for reproducibility.
    // --------------------------------------------------------
    static std::string serialize_config_to_json(const Config& config);

    // --------------------------------------------------------
    // HASHING
    // FNV-1a hash of serialized config.
    // Used to detect config changes between sessions.
    // Not cryptographic — just stable and fast.
    // --------------------------------------------------------
    static std::string hash_config(const Config& config);

    // --------------------------------------------------------
    // LOGGING
    // Prints resolution report to LOG_INFO.
    // --------------------------------------------------------
    static void log_resolution_report(const CascadeResolutionReport& report);

private:
    ConfigCascadeResolver() = delete;

    // --------------------------------------------------------
    // apply_file_overlay
    //
    // THE CORE OF THE CASCADE MECHANISM.
    //
    // Reads filepath line-by-line to collect key names present.
    // Loads the file into a temp Config via ConfigLoader.
    // Copies ONLY the keys found in this file from temp → target.
    // Returns the set of key names that were applied.
    //
    // This is what gives CLASS and SYMBOL configs "override only"
    // semantics — only what's written in the file takes effect.
    // Keys absent from the file leave target unchanged.
    // --------------------------------------------------------
    static std::set<std::string> apply_file_overlay(
        const std::string& filepath,
        Config& target
    );

    // Build class config path: {root}/classes/{ASSET_CLASS}.config
    static std::string class_config_path(
        const std::string& config_root,
        const std::string& asset_class
    );

    // Build symbol config path: {root}/symbols/{SYMBOL}.config
    static std::string symbol_config_path(
        const std::string& config_root,
        const std::string& symbol
    );

    static bool file_exists(const std::string& path);
    static uint64_t fnv1a_hash(const std::string& data);
    static std::string format_key_list(const std::set<std::string>& keys);
};

} // namespace Core
} // namespace SDTrading

#endif // SDTRADING_CONFIG_CASCADE_H
