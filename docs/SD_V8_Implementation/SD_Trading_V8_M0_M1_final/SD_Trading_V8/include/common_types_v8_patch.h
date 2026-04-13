// ============================================================
// SD TRADING V8 — common_types.h PATCH
// include/common_types_v8_patch.h
//
// Milestone M0.2 / M1.1
//
// HOW TO APPLY:
//   Open include/common_types.h in your editor.
//
//   Step 1: Find the END of the SystemConfig field declarations
//           (search for "zone_bootstrap_force_regenerate")
//           After that line's declaration, add BLOCK A below.
//
//   Step 2: Find the SystemConfig constructor initializer list
//           (search for "zone_bootstrap_force_regenerate(false)")
//           After that line (before the closing "}{}"), add BLOCK B.
//
//   Step 3: Save and rebuild. All existing tests must pass unchanged.
//
// ZERO RISK: These are additive fields only. No existing field
// is modified, removed, or renamed.
// ============================================================

// ============================================================
// BLOCK A — Add these field declarations to struct SystemConfig
// Place after: bool zone_bootstrap_force_regenerate;
// ============================================================

/*

    // ---- V8: Multi-symbol / Scanner additions ----

    // Symbol registry file — source of truth for all tradeable symbols.
    // Replaces the single trading.symbol for scanner mode.
    std::string symbol_registry_file;

    // Config root directory for 3-tier cascade:
    //   {config_root_dir}/MASTER.config
    //   {config_root_dir}/classes/{ASSET_CLASS}.config
    //   {config_root_dir}/symbols/{SYMBOL}.config
    std::string config_root_dir;

    // SQLite database file path (WAL mode, single file for all symbols)
    std::string sqlite_db_path;

    // Schema SQL file — applied once at first run
    std::string sqlite_schema_path;

    // Scanner mode settings
    bool        scanner_mode_enabled;
    double      scanner_signal_min_score;
    std::string scanner_rank_by;            // "SCORE" | "RR" | "ZONE_STRENGTH"

    // Compute backend selection
    std::string compute_backend;            // "AUTO" | "CPU" | "CUDA"
    int         cpu_thread_pool_size;       // Workers for CPU thread pool
    int         cuda_device_id;             // GPU device index (0 = RTX 4050)
    int         cuda_min_symbols_threshold; // Min symbols to justify GPU

    // Order submitter (extracted from system_config.json order_submitter section)
    bool        order_submitter_enabled;
    std::string order_submitter_base_url;
    int         order_submitter_long_strategy;
    int         order_submitter_short_strategy;
    int         order_submitter_timeout;

*/

// ============================================================
// BLOCK B — Add these initializers to SystemConfig constructor
// Place after: zone_bootstrap_force_regenerate(false)
// Add a comma after the previous line first if needed.
// ============================================================

/*

          symbol_registry_file("config/symbol_registry.json"),
          config_root_dir("config"),
          sqlite_db_path("data/sd_trading_v8.db"),
          sqlite_schema_path("src/persistence/db_schema.sql"),
          scanner_mode_enabled(false),
          scanner_signal_min_score(65.0),
          scanner_rank_by("SCORE"),
          compute_backend("AUTO"),
          cpu_thread_pool_size(8),
          cuda_device_id(0),
          cuda_min_symbols_threshold(20),
          order_submitter_enabled(true),
          order_submitter_base_url("http://localhost:8080/trade"),
          order_submitter_long_strategy(13),
          order_submitter_short_strategy(14),
          order_submitter_timeout(10)

*/

// ============================================================
// VERIFICATION
// After applying the patch, search common_types.h for:
//   symbol_registry_file
//   sqlite_db_path
//   scanner_mode_enabled
// All three should appear exactly twice: once in declarations,
// once in the constructor initializer list.
// ============================================================
