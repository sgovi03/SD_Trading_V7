// ============================================================
// SD TRADING V8 - SYSTEMCONFIG ADDITIONS
// include/system_config_v8_additions.h
//
// Milestone M0.2 / M1.1
//
// These fields need to be added to the SystemConfig struct
// in include/common_types.h (after the existing last field).
//
// Instructions:
//   Open include/common_types.h
//   Find the SystemConfig struct constructor (line ~1470)
//   Before the closing brace of the constructor, add the
//   initializers shown below.
//   Before the get_full_path() method, add the new fields.
//
// We do NOT modify common_types.h directly here to avoid
// merge conflicts with the existing build. Apply this patch
// manually before building.
// ============================================================

/*
ADD TO struct SystemConfig (after existing field declarations,
before the constructor block):

    // ---- V8: Multi-symbol / Scanner additions ----

    // Symbol registry JSON path (replaces single trading.symbol)
    std::string symbol_registry_file;

    // Config root directory (for 3-tier cascade)
    // e.g. "config/"  containing MASTER.config, classes/, symbols/
    std::string config_root_dir;

    // SQLite database path
    std::string sqlite_db_path;

    // Schema SQL file path (applied on first run)
    std::string sqlite_schema_path;

    // Scanner mode settings
    bool        scanner_mode_enabled;
    double      scanner_signal_min_score;
    std::string scanner_rank_by;            // "SCORE" | "RR" | "ZONE_STRENGTH"

    // Compute backend
    std::string compute_backend;            // "AUTO" | "CPU" | "CUDA"
    int         cpu_thread_pool_size;
    int         cuda_device_id;
    int         cuda_min_symbols_threshold;

ADD TO SystemConfig constructor initializer list:

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
    cuda_min_symbols_threshold(20)

ADD TO system_config.json (new section alongside existing keys):

  "v8": {
    "symbol_registry_file": "config/symbol_registry.json",
    "config_root_dir": "config",
    "sqlite_db_path": "data/sd_trading_v8.db",
    "sqlite_schema_path": "src/persistence/db_schema.sql",
    "scanner_mode_enabled": false,
    "scanner_signal_min_score": 65.0,
    "scanner_rank_by": "SCORE",
    "compute_backend": "AUTO",
    "cpu_thread_pool_size": 8,
    "cuda_device_id": 0,
    "cuda_min_symbols_threshold": 20
  }

ADD TO SystemConfigLoader::load_from_json() in
src/core/system_config_loader.cpp:

    // V8 additions
    std::string v8_obj = extract_json_object(json_text, "v8");
    if (!v8_obj.empty()) {
        config.symbol_registry_file     = extract_json_string(v8_obj, "symbol_registry_file");
        config.config_root_dir          = extract_json_string(v8_obj, "config_root_dir");
        config.sqlite_db_path           = extract_json_string(v8_obj, "sqlite_db_path");
        config.sqlite_schema_path       = extract_json_string(v8_obj, "sqlite_schema_path");
        config.scanner_mode_enabled     = extract_json_bool(v8_obj, "scanner_mode_enabled");
        config.scanner_signal_min_score = extract_json_double(v8_obj, "scanner_signal_min_score");
        config.scanner_rank_by          = extract_json_string(v8_obj, "scanner_rank_by");
        config.compute_backend          = extract_json_string(v8_obj, "compute_backend");
        config.cpu_thread_pool_size     = extract_json_int(v8_obj, "cpu_thread_pool_size");
        config.cuda_device_id           = extract_json_int(v8_obj, "cuda_device_id");
        config.cuda_min_symbols_threshold = extract_json_int(v8_obj, "cuda_min_symbols_threshold");

        // Apply defaults for any missing keys
        if (config.symbol_registry_file.empty())
            config.symbol_registry_file = "config/symbol_registry.json";
        if (config.config_root_dir.empty())
            config.config_root_dir = "config";
        if (config.sqlite_db_path.empty())
            config.sqlite_db_path = "data/sd_trading_v8.db";
        if (config.sqlite_schema_path.empty())
            config.sqlite_schema_path = "src/persistence/db_schema.sql";
        if (config.scanner_rank_by.empty())
            config.scanner_rank_by = "SCORE";
        if (config.compute_backend.empty())
            config.compute_backend = "AUTO";
        if (config.cpu_thread_pool_size == 0)
            config.cpu_thread_pool_size = 8;
        if (config.cuda_min_symbols_threshold == 0)
            config.cuda_min_symbols_threshold = 20;
    }
*/

// This file is documentation only — no code to compile.
// Apply the patches above manually to common_types.h and
// system_config_loader.cpp before building.
