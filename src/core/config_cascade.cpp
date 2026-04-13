// ============================================================
// SD TRADING V8 - CONFIG CASCADE RESOLVER IMPLEMENTATION
// src/core/config_cascade.cpp
// Milestone M1.1
// ============================================================

#include "config_cascade.h"
#include "../utils/logger.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <cassert>

// nlohmann/json for symbol_registry.json parsing
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace fs = std::filesystem;

namespace SDTrading {
namespace Core {

// ============================================================
// INTERNAL HELPERS — file scope only
// ============================================================

namespace {

// Trim whitespace from both ends of a string
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Extract key from a config line "key = value  # optional comment"
// Returns empty string if line is empty or comment.
std::string extract_key(const std::string& raw_line) {
    std::string line = trim(raw_line);
    if (line.empty() || line[0] == '#') return "";

    // Strip inline comment
    size_t comment = line.find('#');
    if (comment != std::string::npos) line = line.substr(0, comment);

    size_t eq = line.find('=');
    if (eq == std::string::npos) return "";

    return trim(line.substr(0, eq));
}

// Collect all key names that appear in a config file.
// Preserves declaration order (use vector, deduplicate with set).
std::vector<std::string> collect_keys_from_file(const std::string& filepath) {
    std::vector<std::string> keys;
    std::set<std::string> seen;

    std::ifstream f(filepath);
    if (!f.is_open()) return keys;

    std::string line;
    while (std::getline(f, line)) {
        std::string key = extract_key(line);
        if (!key.empty() && seen.find(key) == seen.end()) {
            keys.push_back(key);
            seen.insert(key);
        }
    }
    return keys;
}

} // anonymous namespace

// ============================================================
// SymbolRegistry
// ============================================================

std::vector<SymbolProfile> SymbolRegistry::get_active_symbols() const {
    std::vector<SymbolProfile> active;
    for (const auto& p : symbols) {
        if (p.active) active.push_back(p);
    }
    return active;
}

const SymbolProfile* SymbolRegistry::find(const std::string& symbol) const {
    for (const auto& p : symbols) {
        if (p.symbol == symbol) return &p;
    }
    return nullptr;
}

SymbolRegistry SymbolRegistry::load_from_json(const std::string& filepath) {
    SymbolRegistry registry;

    std::ifstream f(filepath);
    if (!f.is_open()) {
        throw std::runtime_error("[SymbolRegistry] Cannot open: " + filepath);
    }

    json j;
    try {
        f >> j;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("[SymbolRegistry] JSON parse error in "
                                 + filepath + ": " + e.what());
    }

    if (!j.contains("symbols") || !j["symbols"].is_array()) {
        throw std::runtime_error(
            "[SymbolRegistry] Missing 'symbols' array in " + filepath);
    }

    for (const auto& entry : j["symbols"]) {
        SymbolProfile p;
        p.symbol           = entry.value("symbol",           "");
        p.display_name     = entry.value("display_name",     "");
        p.asset_class      = entry.value("asset_class",      "INDEX_FUTURES");
        p.exchange         = entry.value("exchange",         "NSE");
        p.lot_size         = entry.value("lot_size",         0);
        p.tick_size        = entry.value("tick_size",        0.05);
        p.margin_per_lot   = entry.value("margin_per_lot",   0.0);
        p.expiry_day       = entry.value("expiry_day",       "THURSDAY");
        p.live_feed_ticker = entry.value("live_feed_ticker", "");
        p.config_file_path = entry.value("config_file_path", "");
        p.active           = entry.value("active",           false);

        if (p.symbol.empty()) {
            LOG_WARN("[SymbolRegistry] Skipping entry with empty symbol");
            continue;
        }
        if (!p.is_valid()) {
            LOG_WARN("[SymbolRegistry] '" << p.symbol
                     << "' invalid profile (lot_size=" << p.lot_size
                     << " tick_size=" << p.tick_size << ") — skipping");
            continue;
        }

        registry.symbols.push_back(p);
        LOG_INFO("[SymbolRegistry] Loaded: " << p.symbol
                 << " (" << p.asset_class << ", lot=" << p.lot_size << ")"
                 << (p.active ? " [ACTIVE]" : " [INACTIVE]"));
    }

    auto active = registry.get_active_symbols();
    LOG_INFO("[SymbolRegistry] Total: " << registry.symbols.size()
             << " | Active: " << active.size());
    return registry;
}

// ============================================================
// ConfigCascadeResolver — private helpers
// ============================================================

bool ConfigCascadeResolver::file_exists(const std::string& path) {
    return fs::exists(path) && fs::is_regular_file(path);
}

std::string ConfigCascadeResolver::class_config_path(
    const std::string& config_root, const std::string& asset_class) {
    return config_root + "/classes/" + asset_class + ".config";
}

std::string ConfigCascadeResolver::symbol_config_path(
    const std::string& config_root, const std::string& symbol) {
    return config_root + "/symbols/" + symbol + ".config";
}

std::string ConfigCascadeResolver::format_key_list(
    const std::set<std::string>& keys) {
    if (keys.empty()) return "(none)";
    std::string out;
    for (const auto& k : keys) {
        if (!out.empty()) out += ", ";
        out += k;
    }
    return out;
}

// ============================================================
// apply_file_overlay — THE CORE CASCADE MECHANISM
//
// This function implements "overlay" semantics:
//   Only keys explicitly written in the file override target.
//   Keys absent from the file leave target unchanged.
//
// Algorithm:
//   1. Collect key names present in filepath
//   2. Load filepath into a temp Config (using full ConfigLoader)
//   3. For each key in the file, copy temp→target for that field
//   4. Return the set of keys applied
//
// The field transfer is driven by a static lookup table mapping
// key names to copy lambdas. Adding a new Config field = one
// new entry in the table below.
// ============================================================

std::set<std::string> ConfigCascadeResolver::apply_file_overlay(
    const std::string& filepath,
    Config& target
) {
    std::set<std::string> applied;

    if (!file_exists(filepath)) return applied;

    // Step 1: Which keys are present in this file?
    std::vector<std::string> keys_in_file = collect_keys_from_file(filepath);
    if (keys_in_file.empty()) return applied;

    // Step 2: Load file into temp config (ConfigLoader handles all parsing)
    Config temp = ConfigLoader::load_from_file(filepath);

    // Step 3: Transfer only keys found in this file to target.
    //
    // Field transfer table: key_name → lambda(src, dst)
    // Each lambda copies one field from src (temp) to dst (target).
    //
    // NOTE: When a new field is added to Config, add one line here.
    // The LOG_DEBUG below will flag any key not yet in the table.
    //
    using CopyFn = std::function<void(const Config&, Config&)>;
    static const std::map<std::string, CopyFn> TRANSFER = {

        // ---- Capital & Risk ----
        {"starting_capital",             [](const Config& s, Config& d){ d.starting_capital = s.starting_capital; }},
        {"risk_per_trade_pct",           [](const Config& s, Config& d){ d.risk_per_trade_pct = s.risk_per_trade_pct; }},
        {"lot_size",                     [](const Config& s, Config& d){ d.lot_size = s.lot_size; }},
        {"commission_per_trade",         [](const Config& s, Config& d){ d.commission_per_trade = s.commission_per_trade; }},
        {"max_loss_per_trade",           [](const Config& s, Config& d){ d.max_loss_per_trade = s.max_loss_per_trade; }},
        {"max_fill_slippage_pts",        [](const Config& s, Config& d){ d.max_fill_slippage_pts = s.max_fill_slippage_pts; }},

        // ---- Zone Detection ----
        {"base_height_max_atr",          [](const Config& s, Config& d){ d.base_height_max_atr = s.base_height_max_atr; }},
        {"consolidation_min_bars",       [](const Config& s, Config& d){ d.consolidation_min_bars = s.consolidation_min_bars; }},
        {"consolidation_max_bars",       [](const Config& s, Config& d){ d.consolidation_max_bars = s.consolidation_max_bars; }},
        {"max_consolidation_range",      [](const Config& s, Config& d){ d.max_consolidation_range = s.max_consolidation_range; }},
        {"min_zone_width_atr",           [](const Config& s, Config& d){ d.min_zone_width_atr = s.min_zone_width_atr; }},
        {"min_impulse_atr",              [](const Config& s, Config& d){ d.min_impulse_atr = s.min_impulse_atr; }},
        {"atr_period",                   [](const Config& s, Config& d){ d.atr_period = s.atr_period; }},
        {"min_zone_strength",            [](const Config& s, Config& d){ d.min_zone_strength = s.min_zone_strength; }},
        {"only_fresh_zones",             [](const Config& s, Config& d){ d.only_fresh_zones = s.only_fresh_zones; }},
        {"invalidate_on_body_close",     [](const Config& s, Config& d){ d.invalidate_on_body_close = s.invalidate_on_body_close; }},
        {"gap_over_invalidation",        [](const Config& s, Config& d){ d.gap_over_invalidation = s.gap_over_invalidation; }},
        {"skip_retest_after_gap_over",   [](const Config& s, Config& d){ d.skip_retest_after_gap_over = s.skip_retest_after_gap_over; }},
        {"zone_detection_interval_bars", [](const Config& s, Config& d){ d.zone_detection_interval_bars = s.zone_detection_interval_bars; }},
        {"min_zone_age_days",            [](const Config& s, Config& d){ d.min_zone_age_days = s.min_zone_age_days; }},
        {"max_zone_age_days",            [](const Config& s, Config& d){ d.max_zone_age_days = s.max_zone_age_days; }},
        {"max_touch_count",              [](const Config& s, Config& d){ d.max_touch_count = s.max_touch_count; }},

        // ---- Duplicate Prevention ----
        {"duplicate_prevention_enabled", [](const Config& s, Config& d){ d.duplicate_prevention_enabled = s.duplicate_prevention_enabled; }},
        {"zone_existence_tolerance_atr", [](const Config& s, Config& d){ d.zone_existence_tolerance_atr = s.zone_existence_tolerance_atr; }},
        {"atr_buffer_multiplier",        [](const Config& s, Config& d){ d.atr_buffer_multiplier = s.atr_buffer_multiplier; }},
        {"check_existing_before_create", [](const Config& s, Config& d){ d.check_existing_before_create = s.check_existing_before_create; }},
        {"merge_strategy",               [](const Config& s, Config& d){ d.merge_strategy = s.merge_strategy; }},
        {"zone_merge_strategy",          [](const Config& s, Config& d){ d.merge_strategy = s.merge_strategy; }},
        {"strength_improvement_threshold",[](const Config& s, Config& d){ d.strength_improvement_threshold = s.strength_improvement_threshold; }},
        {"freshness_bars_threshold",     [](const Config& s, Config& d){ d.freshness_bars_threshold = s.freshness_bars_threshold; }},

        // ---- Zone State Filters ----
        {"skip_violated_zones",          [](const Config& s, Config& d){ d.skip_violated_zones = s.skip_violated_zones; }},
        {"skip_tested_zones",            [](const Config& s, Config& d){ d.skip_tested_zones = s.skip_tested_zones; }},
        {"prefer_fresh_zones",           [](const Config& s, Config& d){ d.prefer_fresh_zones = s.prefer_fresh_zones; }},

        // ---- Sweep & Reclaim ----
        {"enable_sweep_reclaim",         [](const Config& s, Config& d){ d.enable_sweep_reclaim = s.enable_sweep_reclaim; }},
        {"reclaim_acceptance_bars",      [](const Config& s, Config& d){ d.reclaim_acceptance_bars = s.reclaim_acceptance_bars; }},
        {"reclaim_acceptance_pct",       [](const Config& s, Config& d){ d.reclaim_acceptance_pct = s.reclaim_acceptance_pct; }},

        // ---- Zone Flip ----
        {"enable_zone_flip",             [](const Config& s, Config& d){ d.enable_zone_flip = s.enable_zone_flip; }},
        {"flip_min_impulse_atr",         [](const Config& s, Config& d){ d.flip_min_impulse_atr = s.flip_min_impulse_atr; }},
        {"flip_lookback_bars",           [](const Config& s, Config& d){ d.flip_lookback_bars = s.flip_lookback_bars; }},

        // ---- Scoring ----
        {"use_adaptive_entry",           [](const Config& s, Config& d){ d.use_adaptive_entry = s.use_adaptive_entry; }},
        {"high_score_position_mult",     [](const Config& s, Config& d){ d.high_score_position_mult = s.high_score_position_mult; }},
        {"high_score_threshold",         [](const Config& s, Config& d){ d.high_score_threshold = s.high_score_threshold; }},
        {"enable_two_stage_scoring",     [](const Config& s, Config& d){ d.enable_two_stage_scoring = s.enable_two_stage_scoring; }},
        {"enable_revamped_scoring",      [](const Config& s, Config& d){ d.enable_revamped_scoring = s.enable_revamped_scoring; }},

        // ---- EMA Trend Filter ----
        {"use_ema_trend_filter",         [](const Config& s, Config& d){ d.use_ema_trend_filter = s.use_ema_trend_filter; }},
        {"ema_fast_period",              [](const Config& s, Config& d){ d.ema_fast_period = s.ema_fast_period; }},
        {"ema_slow_period",              [](const Config& s, Config& d){ d.ema_slow_period = s.ema_slow_period; }},
        {"ema_ranging_threshold_pct",    [](const Config& s, Config& d){ d.ema_ranging_threshold_pct = s.ema_ranging_threshold_pct; }},
        {"require_ema_alignment_for_longs", [](const Config& s, Config& d){ d.require_ema_alignment_for_longs = s.require_ema_alignment_for_longs; }},
        {"require_ema_alignment_for_shorts",[](const Config& s, Config& d){ d.require_ema_alignment_for_shorts = s.require_ema_alignment_for_shorts; }},
        {"ema_min_separation_pct_long",  [](const Config& s, Config& d){ d.ema_min_separation_pct_long = s.ema_min_separation_pct_long; }},
        {"ema_min_separation_pct_short", [](const Config& s, Config& d){ d.ema_min_separation_pct_short = s.ema_min_separation_pct_short; }},

        // ---- EMA Exit (LIVE) ----
        {"enable_ema_exit",              [](const Config& s, Config& d){ d.enable_ema_exit = s.enable_ema_exit; }},
        {"exit_ema_fast_period",         [](const Config& s, Config& d){ d.exit_ema_fast_period = s.exit_ema_fast_period; }},
        {"exit_ema_slow_period",         [](const Config& s, Config& d){ d.exit_ema_slow_period = s.exit_ema_slow_period; }},

        // ---- Trailing Stop ----
        {"use_trailing_stop",            [](const Config& s, Config& d){ d.use_trailing_stop = s.use_trailing_stop; }},
        {"trail_activation_rr",          [](const Config& s, Config& d){ d.trail_activation_rr = s.trail_activation_rr; }},
        {"trailing_stop_activation_r",   [](const Config& s, Config& d){ d.trailing_stop_activation_r = s.trailing_stop_activation_r; }},
        {"trail_atr_multiplier",         [](const Config& s, Config& d){ d.trail_atr_multiplier = s.trail_atr_multiplier; }},
        {"trail_ema_period",             [](const Config& s, Config& d){ d.trail_ema_period = s.trail_ema_period; }},
        {"trail_ema_buffer_pct",         [](const Config& s, Config& d){ d.trail_ema_buffer_pct = s.trail_ema_buffer_pct; }},
        {"trail_use_hybrid",             [](const Config& s, Config& d){ d.trail_use_hybrid = s.trail_use_hybrid; }},
        {"trail_fallback_tp_rr",         [](const Config& s, Config& d){ d.trail_fallback_tp_rr = s.trail_fallback_tp_rr; }},

        // ---- Stop Loss & Take Profit ----
        {"stop_loss_atr_multiplier",     [](const Config& s, Config& d){ d.sl_buffer_atr = s.sl_buffer_atr; }},
        {"target_rr_ratio",              [](const Config& s, Config& d){ d.risk_reward_ratio = s.risk_reward_ratio; }},
        {"sl_buffer_zone_pct",           [](const Config& s, Config& d){ d.sl_buffer_zone_pct = s.sl_buffer_zone_pct; }},
        {"sl_buffer_atr",                [](const Config& s, Config& d){ d.sl_buffer_atr = s.sl_buffer_atr; }},
        {"rejection_wick_ratio",         [](const Config& s, Config& d){ d.rejection_wick_ratio = s.rejection_wick_ratio; }},
        {"entry_buffer_pct",             [](const Config& s, Config& d){ d.entry_buffer_pct = s.entry_buffer_pct; }},
        {"min_stop_distance_points",     [](const Config& s, Config& d){ d.min_stop_distance_points = s.min_stop_distance_points; }},
        {"use_breakeven_stop_loss",      [](const Config& s, Config& d){ d.use_breakeven_stop_loss = s.use_breakeven_stop_loss; }},

        // ---- Live Fill Guards ----
        {"min_zone_penetration_pct",     [](const Config& s, Config& d){ d.min_zone_penetration_pct = s.min_zone_penetration_pct; }},
        {"require_price_at_entry_level", [](const Config& s, Config& d){ d.require_price_at_entry_level = s.require_price_at_entry_level; }},

        // ---- Trade Management ----
        {"max_concurrent_trades",        [](const Config& s, Config& d){ d.max_concurrent_trades = s.max_concurrent_trades; }},
        {"max_consecutive_losses",       [](const Config& s, Config& d){ d.max_consecutive_losses = s.max_consecutive_losses; }},
        {"pause_bars_after_losses",      [](const Config& s, Config& d){ d.pause_bars_after_losses = s.pause_bars_after_losses; }},
        {"lookback_for_zones",           [](const Config& s, Config& d){ d.lookback_for_zones = s.lookback_for_zones; }},
        {"max_retries_per_zone",         [](const Config& s, Config& d){ d.max_retries_per_zone = s.max_retries_per_zone; }},
        {"enable_per_zone_retry_limit",  [](const Config& s, Config& d){ d.enable_per_zone_retry_limit = s.enable_per_zone_retry_limit; }},

        // ---- Session ----
        {"close_eod",                    [](const Config& s, Config& d){ d.close_eod = s.close_eod; }},
        {"session_end_time",             [](const Config& s, Config& d){ d.session_end_time = s.session_end_time; }},
        {"close_before_session_end_minutes",[](const Config& s, Config& d){ d.close_before_session_end_minutes = s.close_before_session_end_minutes; }},

        // ---- Entry Time Block ----
        {"enable_entry_time_block",      [](const Config& s, Config& d){ d.enable_entry_time_block = s.enable_entry_time_block; }},
        {"entry_block_start_time",       [](const Config& s, Config& d){ d.entry_block_start_time = s.entry_block_start_time; }},
        {"entry_block_end_time",         [](const Config& s, Config& d){ d.entry_block_end_time = s.entry_block_end_time; }},

        // ---- HTF Regime ----
        {"use_htf_regime_filter",        [](const Config& s, Config& d){ d.use_htf_regime_filter = s.use_htf_regime_filter; }},
        {"htf_lookback_bars",            [](const Config& s, Config& d){ d.htf_lookback_bars = s.htf_lookback_bars; }},
        {"htf_trending_threshold",       [](const Config& s, Config& d){ d.htf_trending_threshold = s.htf_trending_threshold; }},

        // ---- Elite Zone ----
        {"min_departure_imbalance",      [](const Config& s, Config& d){ d.min_departure_imbalance = s.min_departure_imbalance; }},
        {"min_departure_distance_atr",   [](const Config& s, Config& d){ d.min_departure_distance_atr = s.min_departure_distance_atr; }},
        {"max_retest_speed_atr",         [](const Config& s, Config& d){ d.max_retest_speed_atr = s.max_retest_speed_atr; }},
        {"min_bars_to_retest",           [](const Config& s, Config& d){ d.min_bars_to_retest = s.min_bars_to_retest; }},

        // ---- Swing / Trend ----
        {"swing_detection_bars",         [](const Config& s, Config& d){ d.swing_detection_bars = s.swing_detection_bars; }},
        {"trade_with_trend_only",        [](const Config& s, Config& d){ d.trade_with_trend_only = s.trade_with_trend_only; }},
        {"allow_ranging_trades",         [](const Config& s, Config& d){ d.allow_ranging_trades = s.allow_ranging_trades; }},
        {"use_bos_trend_filter",         [](const Config& s, Config& d){ d.use_bos_trend_filter = s.use_bos_trend_filter; }},

        // ---- Candle/Structure Confirmation ----
        {"enable_candle_confirmation",   [](const Config& s, Config& d){ d.enable_candle_confirmation = s.enable_candle_confirmation; }},
        {"candle_confirmation_body_pct", [](const Config& s, Config& d){ d.candle_confirmation_body_pct = s.candle_confirmation_body_pct; }},
        {"enable_structure_confirmation",[](const Config& s, Config& d){ d.enable_structure_confirmation = s.enable_structure_confirmation; }},

        // ---- Live Engine ----
        {"use_relaxed_live_detection",   [](const Config& s, Config& d){ d.use_relaxed_live_detection = s.use_relaxed_live_detection; }},
        {"live_zone_detection_interval_bars",[](const Config& s, Config& d){ d.live_zone_detection_interval_bars = s.live_zone_detection_interval_bars; }},
        {"live_zone_detection_lookback_bars",[](const Config& s, Config& d){ d.live_zone_detection_lookback_bars = s.live_zone_detection_lookback_bars; }},
        {"live_min_zone_width_atr",      [](const Config& s, Config& d){ d.live_min_zone_width_atr = s.live_min_zone_width_atr; }},
        {"live_min_zone_strength",       [](const Config& s, Config& d){ d.live_min_zone_strength = s.live_min_zone_strength; }},
        {"live_entry_require_new_bar",   [](const Config& s, Config& d){ d.live_entry_require_new_bar = s.live_entry_require_new_bar; }},
        {"live_skip_when_in_position",   [](const Config& s, Config& d){ d.live_skip_when_in_position = s.live_skip_when_in_position; }},
        {"live_zone_entry_cooldown_seconds",[](const Config& s, Config& d){ d.live_zone_entry_cooldown_seconds = s.live_zone_entry_cooldown_seconds; }},

        // ---- Live Zone Filtering ----
        {"enable_live_zone_filtering",   [](const Config& s, Config& d){ d.enable_live_zone_filtering = s.enable_live_zone_filtering; }},
        {"live_zone_range_pct",          [](const Config& s, Config& d){ d.live_zone_range_pct = s.live_zone_range_pct; }},
        {"live_zone_refresh_interval_minutes",[](const Config& s, Config& d){ d.live_zone_refresh_interval_minutes = s.live_zone_refresh_interval_minutes; }},
        {"max_zone_distance_atr",        [](const Config& s, Config& d){ d.max_zone_distance_atr = s.max_zone_distance_atr; }},

        // ---- Zone Bootstrap ----
        {"zone_bootstrap_enabled",       [](const Config& s, Config& d){ d.zone_bootstrap_enabled = s.zone_bootstrap_enabled; }},
        {"zone_bootstrap_ttl_hours",     [](const Config& s, Config& d){ d.zone_bootstrap_ttl_hours = s.zone_bootstrap_ttl_hours; }},
        {"zone_bootstrap_refresh_time",  [](const Config& s, Config& d){ d.zone_bootstrap_refresh_time = s.zone_bootstrap_refresh_time; }},
        {"zone_bootstrap_force_regenerate",[](const Config& s, Config& d){ d.zone_bootstrap_force_regenerate = s.zone_bootstrap_force_regenerate; }},

        // ---- V6.0 Volume ----
        {"enable_volume_entry_filters",  [](const Config& s, Config& d){ d.enable_volume_entry_filters = s.enable_volume_entry_filters; }},
        {"enable_volume_exit_signals",   [](const Config& s, Config& d){ d.enable_volume_exit_signals = s.enable_volume_exit_signals; }},
        {"min_entry_volume_ratio",       [](const Config& s, Config& d){ d.min_entry_volume_ratio = s.min_entry_volume_ratio; }},
        {"institutional_volume_threshold",[](const Config& s, Config& d){ d.institutional_volume_threshold = s.institutional_volume_threshold; }},
        {"extreme_volume_threshold",     [](const Config& s, Config& d){ d.extreme_volume_threshold = s.extreme_volume_threshold; }},
        {"volume_lookback_period",       [](const Config& s, Config& d){ d.volume_lookback_period = s.volume_lookback_period; }},
        {"volume_normalization_method",  [](const Config& s, Config& d){ d.volume_normalization_method = s.volume_normalization_method; }},
        {"volume_baseline_file",         [](const Config& s, Config& d){ d.volume_baseline_file = s.volume_baseline_file; }},
        {"volume_climax_exit_threshold", [](const Config& s, Config& d){ d.volume_climax_exit_threshold = s.volume_climax_exit_threshold; }},
        {"volume_drying_up_threshold",   [](const Config& s, Config& d){ d.volume_drying_up_threshold = s.volume_drying_up_threshold; }},
        {"volume_drying_up_bar_count",   [](const Config& s, Config& d){ d.volume_drying_up_bar_count = s.volume_drying_up_bar_count; }},
        {"enable_volume_divergence_exit",[](const Config& s, Config& d){ d.enable_volume_divergence_exit = s.enable_volume_divergence_exit; }},
        {"max_entry_volume_ratio",       [](const Config& s, Config& d){ d.max_entry_volume_ratio = s.max_entry_volume_ratio; }},
        {"max_volume_without_elite",     [](const Config& s, Config& d){ d.max_volume_without_elite = s.max_volume_without_elite; }},
        {"min_inst_for_high_volume",     [](const Config& s, Config& d){ d.min_inst_for_high_volume = s.min_inst_for_high_volume; }},
        {"optimal_volume_min",           [](const Config& s, Config& d){ d.optimal_volume_min = s.optimal_volume_min; }},
        {"optimal_volume_max",           [](const Config& s, Config& d){ d.optimal_volume_max = s.optimal_volume_max; }},
        {"optimal_institutional_min",    [](const Config& s, Config& d){ d.optimal_institutional_min = s.optimal_institutional_min; }},
        {"allow_low_volume_if_score_above",[](const Config& s, Config& d){ d.allow_low_volume_if_score_above = s.allow_low_volume_if_score_above; }},
        {"elite_institutional_threshold",[](const Config& s, Config& d){ d.elite_institutional_threshold = s.elite_institutional_threshold; }},
        {"min_volume_entry_score",       [](const Config& s, Config& d){ d.min_volume_entry_score = s.min_volume_entry_score; }},

        // ---- V6.0 OI ----
        {"enable_oi_entry_filters",      [](const Config& s, Config& d){ d.enable_oi_entry_filters = s.enable_oi_entry_filters; }},
        {"enable_oi_exit_signals",       [](const Config& s, Config& d){ d.enable_oi_exit_signals = s.enable_oi_exit_signals; }},
        {"enable_market_phase_detection",[](const Config& s, Config& d){ d.enable_market_phase_detection = s.enable_market_phase_detection; }},
        {"min_oi_change_threshold",      [](const Config& s, Config& d){ d.min_oi_change_threshold = s.min_oi_change_threshold; }},
        {"high_oi_buildup_threshold",    [](const Config& s, Config& d){ d.high_oi_buildup_threshold = s.high_oi_buildup_threshold; }},
        {"oi_lookback_period",           [](const Config& s, Config& d){ d.oi_lookback_period = s.oi_lookback_period; }},
        {"price_oi_correlation_window",  [](const Config& s, Config& d){ d.price_oi_correlation_window = s.price_oi_correlation_window; }},
        {"oi_unwinding_threshold",       [](const Config& s, Config& d){ d.oi_unwinding_threshold = s.oi_unwinding_threshold; }},
        {"oi_reversal_threshold",        [](const Config& s, Config& d){ d.oi_reversal_threshold = s.oi_reversal_threshold; }},
        {"oi_stagnation_threshold",      [](const Config& s, Config& d){ d.oi_stagnation_threshold = s.oi_stagnation_threshold; }},
        {"oi_stagnation_bar_count",      [](const Config& s, Config& d){ d.oi_stagnation_bar_count = s.oi_stagnation_bar_count; }},

        // ---- V6.0 Scoring Weights ----
        {"base_score_weight",            [](const Config& s, Config& d){ d.base_score_weight = s.base_score_weight; }},
        {"volume_score_weight",          [](const Config& s, Config& d){ d.volume_score_weight = s.volume_score_weight; }},
        {"oi_score_weight",              [](const Config& s, Config& d){ d.oi_score_weight = s.oi_score_weight; }},
        {"institutional_volume_bonus",   [](const Config& s, Config& d){ d.institutional_volume_bonus = s.institutional_volume_bonus; }},
        {"oi_alignment_bonus",           [](const Config& s, Config& d){ d.oi_alignment_bonus = s.oi_alignment_bonus; }},
        {"low_volume_retest_bonus",      [](const Config& s, Config& d){ d.low_volume_retest_bonus = s.low_volume_retest_bonus; }},

        // ---- V6.0 Position Sizing ----
        {"low_volume_size_multiplier",   [](const Config& s, Config& d){ d.low_volume_size_multiplier = s.low_volume_size_multiplier; }},
        {"high_institutional_size_mult", [](const Config& s, Config& d){ d.high_institutional_size_mult = s.high_institutional_size_mult; }},
        {"max_lot_size",                 [](const Config& s, Config& d){ d.max_lot_size = s.max_lot_size; }},

        // ---- V6.0 Expiry Day ----
        {"trade_on_expiry_day",          [](const Config& s, Config& d){ d.trade_on_expiry_day = s.trade_on_expiry_day; }},
        {"expiry_day_disable_oi_filters",[](const Config& s, Config& d){ d.expiry_day_disable_oi_filters = s.expiry_day_disable_oi_filters; }},
        {"expiry_day_position_multiplier",[](const Config& s, Config& d){ d.expiry_day_position_multiplier = s.expiry_day_position_multiplier; }},
        {"expiry_day_min_zone_score",    [](const Config& s, Config& d){ d.expiry_day_min_zone_score = s.expiry_day_min_zone_score; }},

        // ---- V6.0 Feature Flags ----
        {"v6_fully_enabled",             [](const Config& s, Config& d){ d.v6_fully_enabled = s.v6_fully_enabled; }},
        {"v6_log_volume_filters",        [](const Config& s, Config& d){ d.v6_log_volume_filters = s.v6_log_volume_filters; }},
        {"v6_log_oi_filters",            [](const Config& s, Config& d){ d.v6_log_oi_filters = s.v6_log_oi_filters; }},
        {"v6_log_institutional_index",   [](const Config& s, Config& d){ d.v6_log_institutional_index = s.v6_log_institutional_index; }},
        {"v6_validate_baseline_on_startup",[](const Config& s, Config& d){ d.v6_validate_baseline_on_startup = s.v6_validate_baseline_on_startup; }},

        // ---- Indicator Periods ----
        {"rsi_period",                   [](const Config& s, Config& d){ d.rsi_period = s.rsi_period; }},
        {"bb_period",                    [](const Config& s, Config& d){ d.bb_period = s.bb_period; }},
        {"bb_stddev",                    [](const Config& s, Config& d){ d.bb_stddev = s.bb_stddev; }},
        {"adx_period",                   [](const Config& s, Config& d){ d.adx_period = s.adx_period; }},
        {"macd_fast_period",             [](const Config& s, Config& d){ d.macd_fast_period = s.macd_fast_period; }},
        {"macd_slow_period",             [](const Config& s, Config& d){ d.macd_slow_period = s.macd_slow_period; }},
        {"macd_signal_period",           [](const Config& s, Config& d){ d.macd_signal_period = s.macd_signal_period; }},

        // ---- Logging & Debug ----
        {"enable_debug_logging",         [](const Config& s, Config& d){ d.enable_debug_logging = s.enable_debug_logging; }},
        {"enable_score_logging",         [](const Config& s, Config& d){ d.enable_score_logging = s.enable_score_logging; }},
        {"live_export_every_n_bars",     [](const Config& s, Config& d){ d.live_export_every_n_bars = s.live_export_every_n_bars; }},

        // ---- State History ----
        {"enable_state_history",         [](const Config& s, Config& d){ d.enable_state_history = s.enable_state_history; }},
        {"max_state_history_events",     [](const Config& s, Config& d){ d.max_state_history_events = s.max_state_history_events; }},
        {"record_zone_creation",         [](const Config& s, Config& d){ d.record_zone_creation = s.record_zone_creation; }},
        {"record_first_touch",           [](const Config& s, Config& d){ d.record_first_touch = s.record_first_touch; }},
        {"record_retests",               [](const Config& s, Config& d){ d.record_retests = s.record_retests; }},
        {"record_violations",            [](const Config& s, Config& d){ d.record_violations = s.record_violations; }},
    };

    // Apply only keys that appear in this file
    for (const auto& key : keys_in_file) {
        auto it = TRANSFER.find(key);
        if (it != TRANSFER.end()) {
            it->second(temp, target);
            applied.insert(key);
        } else {
            // Key in file but not in transfer table.
            // MASTER load via ConfigLoader already captured it correctly
            // (ConfigLoader handles all keys). The limitation here is only
            // for CLASS/SYMBOL overlay — those files cannot selectively
            // override this key until it's added to the table above.
            LOG_DEBUG("[Cascade] Key '" << key << "' in " << filepath
                      << " not in transfer table — add to apply_file_overlay()");
        }
    }

    return applied;
}

// ============================================================
// resolve — PRIMARY ENTRY POINT
// ============================================================

ResolvedConfig ConfigCascadeResolver::resolve(
    const std::string& config_root,
    const SymbolProfile& profile
) {
    ResolvedConfig result;
    result.profile          = profile;
    result.report.symbol    = profile.symbol;
    result.report.asset_class = profile.asset_class;

    // --- Tier 1: MASTER.config (mandatory) ---
    std::string master_path = config_root + "/MASTER.config";

    if (!file_exists(master_path)) {
        // Backward compatibility: fall back to old flat config behaviour.
        // Try the symbol-specific config file path first,
        // then common locations, before failing.
        LOG_WARN("[Cascade] MASTER.config not found at '" << master_path
                 << "' — falling back to flat config mode");
        result.report.warnings.push_back(
            "MASTER.config not found — falling back to flat file");
        result.report.used_fallback_mode = true;

        // Priority order for fallback flat config:
        //   1. profile.config_file_path (symbol-specific, e.g. config/symbols/NIFTY.config)
        //   2. config_root/phase_6_config_v0_7_NEW_v0.0.txt (known V7 filename)
        //   3. config_root/strategy.config
        std::vector<std::string> candidates = {
            profile.config_file_path,
            config_root + "/phase_6_config_v0_7_NEW_v0.2_TUNED_79pct_PROFIT.txt",
            config_root + "/phase_6_config_v0_7_NEW_v0.0.txt",
            config_root + "/strategy.config",
            config_root + "/nifty.config",
        };

        std::string flat_path;
        for (const auto& c : candidates) {
            if (!c.empty() && file_exists(c)) {
                flat_path = c;
                break;
            }
        }

        if (flat_path.empty()) {
            throw std::runtime_error(
                "No config file found. Tried MASTER.config at '" + master_path + "' "
                "and symbol config '" + profile.config_file_path + "'. "
                "Please create config/MASTER.config — copy your existing "
                "phase_6_config file and rename it.");
        }

        LOG_INFO("[Cascade] Using flat config: " << flat_path);
        result.report.fallback_path = flat_path;
        result.config = ConfigLoader::load_from_file(flat_path);
        result.config.lot_size = profile.lot_size;
        result.report.config_hash = hash_config(result.config);
        return result;
    }

    result.report.master_path = master_path;

    // Load MASTER via the existing ConfigLoader — it knows ALL keys.
    // This sets the full base with every parameter at its MASTER value.
    result.config = ConfigLoader::load_from_file(master_path);
    LOG_INFO("[Cascade] [" << profile.symbol << "] Tier1 MASTER: " << master_path);

    // --- Tier 2: CLASS config (optional) ---
    std::string cls_path = class_config_path(config_root, profile.asset_class);
    if (file_exists(cls_path)) {
        result.report.class_path     = cls_path;
        result.report.class_overrides = apply_file_overlay(cls_path, result.config);
        LOG_INFO("[Cascade] [" << profile.symbol << "] Tier2 CLASS ("
                 << profile.asset_class << "): " << cls_path
                 << " [" << result.report.class_overrides.size() << " overrides]");
    } else {
        LOG_INFO("[Cascade] [" << profile.symbol << "] Tier2 CLASS: not found, skipped");
    }

    // --- Tier 3: SYMBOL config (optional) ---
    std::string sym_path = symbol_config_path(config_root, profile.symbol);
    if (file_exists(sym_path)) {
        result.report.symbol_path     = sym_path;
        result.report.symbol_overrides = apply_file_overlay(sym_path, result.config);
        LOG_INFO("[Cascade] [" << profile.symbol << "] Tier3 SYMBOL: " << sym_path
                 << " [" << result.report.symbol_overrides.size() << " overrides]");
    } else {
        LOG_INFO("[Cascade] [" << profile.symbol << "] Tier3 SYMBOL: not found, skipped");
    }

    // --- Profile values always win ---
    // lot_size in the registry is authoritative — NSE changes it periodically.
    // We overwrite whatever the config files may have set.
    result.config.lot_size = profile.lot_size;

    // --- Hash & report ---
    result.report.config_hash = hash_config(result.config);
    log_resolution_report(result.report);

    return result;
}

// ============================================================
// resolve_flat — FALLBACK (backward compatibility)
// ============================================================

ResolvedConfig ConfigCascadeResolver::resolve_flat(
    const std::string& flat_config_path,
    const SymbolProfile& profile
) {
    ResolvedConfig result;
    result.profile = profile;
    result.report.symbol           = profile.symbol;
    result.report.used_fallback_mode = true;
    result.report.fallback_path    = flat_config_path;

    if (!file_exists(flat_config_path)) {
        throw std::runtime_error(
            "[Cascade] Config file not found: " + flat_config_path);
    }

    result.config = ConfigLoader::load_from_file(flat_config_path);
    result.config.lot_size = profile.lot_size;
    result.report.config_hash = hash_config(result.config);

    LOG_WARN("[Cascade] [" << profile.symbol << "] Flat fallback: "
             << flat_config_path);
    return result;
}

// ============================================================
// compare_configs
// ============================================================

std::vector<std::string> ConfigCascadeResolver::compare_configs(
    const Config& a, const Config& b
) {
    std::vector<std::string> diffs;

    #define CMP_FIELD(field) \
        if (a.field != b.field) diffs.push_back(#field)

    #define CMP_DOUBLE(field) \
        if (std::abs(a.field - b.field) > 1e-9) diffs.push_back(#field)

    // Capital & Risk
    CMP_DOUBLE(starting_capital);
    CMP_DOUBLE(risk_per_trade_pct);
    CMP_FIELD(lot_size);
    CMP_DOUBLE(commission_per_trade);
    CMP_DOUBLE(max_loss_per_trade);
    CMP_DOUBLE(max_fill_slippage_pts);

    // Zone Detection
    CMP_DOUBLE(base_height_max_atr);
    CMP_FIELD(consolidation_min_bars);
    CMP_FIELD(consolidation_max_bars);
    CMP_DOUBLE(min_zone_width_atr);
    CMP_DOUBLE(min_impulse_atr);
    CMP_FIELD(atr_period);
    CMP_DOUBLE(min_zone_strength);
    CMP_FIELD(only_fresh_zones);
    CMP_FIELD(invalidate_on_body_close);
    CMP_FIELD(gap_over_invalidation);
    CMP_FIELD(zone_detection_interval_bars);
    CMP_FIELD(min_zone_age_days);
    CMP_FIELD(max_zone_age_days);
    CMP_FIELD(max_touch_count);

    // Duplicate Prevention
    CMP_FIELD(duplicate_prevention_enabled);
    CMP_DOUBLE(zone_existence_tolerance_atr);
    CMP_DOUBLE(atr_buffer_multiplier);
    CMP_DOUBLE(strength_improvement_threshold);
    CMP_FIELD(freshness_bars_threshold);

    // Zone State
    CMP_FIELD(skip_violated_zones);
    CMP_FIELD(skip_tested_zones);
    CMP_FIELD(prefer_fresh_zones);

    // EMA
    CMP_FIELD(use_ema_trend_filter);
    CMP_FIELD(ema_fast_period);
    CMP_FIELD(ema_slow_period);
    CMP_FIELD(require_ema_alignment_for_longs);
    CMP_FIELD(require_ema_alignment_for_shorts);

    // Trailing Stop
    CMP_FIELD(use_trailing_stop);
    CMP_DOUBLE(trail_activation_rr);
    CMP_DOUBLE(trail_atr_multiplier);
    CMP_DOUBLE(trail_fallback_tp_rr);
    CMP_DOUBLE(trailing_stop_activation_r);

    // SL / TP
    CMP_DOUBLE(sl_buffer_atr);
    CMP_DOUBLE(sl_buffer_zone_pct);
    CMP_DOUBLE(risk_reward_ratio);

    // Trade Management
    CMP_FIELD(max_concurrent_trades);
    CMP_FIELD(max_consecutive_losses);
    CMP_FIELD(pause_bars_after_losses);
    CMP_FIELD(lookback_for_zones);

    // Session
    CMP_FIELD(close_eod);
    CMP_FIELD(session_end_time);

    // V6.0
    CMP_FIELD(enable_volume_entry_filters);
    CMP_FIELD(enable_volume_exit_signals);
    CMP_FIELD(enable_oi_entry_filters);
    CMP_FIELD(enable_oi_exit_signals);
    CMP_FIELD(enable_market_phase_detection);
    CMP_FIELD(volume_baseline_file);

    // Live
    CMP_FIELD(use_relaxed_live_detection);
    CMP_FIELD(live_zone_detection_interval_bars);
    CMP_FIELD(live_zone_detection_lookback_bars);
    CMP_DOUBLE(live_min_zone_width_atr);
    CMP_DOUBLE(live_min_zone_strength);

    // Scoring
    CMP_DOUBLE(base_score_weight);
    CMP_DOUBLE(volume_score_weight);
    CMP_DOUBLE(oi_score_weight);

    #undef CMP_FIELD
    #undef CMP_DOUBLE

    return diffs;
}

// ============================================================
// serialize_config_to_json
// ============================================================

std::string ConfigCascadeResolver::serialize_config_to_json(const Config& c) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{\n";
    ss << "  \"starting_capital\": "              << c.starting_capital              << ",\n";
    ss << "  \"risk_per_trade_pct\": "            << c.risk_per_trade_pct            << ",\n";
    ss << "  \"lot_size\": "                      << c.lot_size                      << ",\n";
    ss << "  \"commission_per_trade\": "          << c.commission_per_trade          << ",\n";
    ss << "  \"max_loss_per_trade\": "            << c.max_loss_per_trade            << ",\n";
    ss << "  \"max_fill_slippage_pts\": "         << c.max_fill_slippage_pts         << ",\n";
    ss << "  \"base_height_max_atr\": "           << c.base_height_max_atr           << ",\n";
    ss << "  \"consolidation_min_bars\": "        << c.consolidation_min_bars        << ",\n";
    ss << "  \"consolidation_max_bars\": "        << c.consolidation_max_bars        << ",\n";
    ss << "  \"min_zone_width_atr\": "            << c.min_zone_width_atr            << ",\n";
    ss << "  \"min_impulse_atr\": "               << c.min_impulse_atr               << ",\n";
    ss << "  \"atr_period\": "                    << c.atr_period                    << ",\n";
    ss << "  \"min_zone_strength\": "             << c.min_zone_strength             << ",\n";
    ss << "  \"only_fresh_zones\": "              << (c.only_fresh_zones ? 1 : 0)    << ",\n";
    ss << "  \"zone_detection_interval_bars\": "  << c.zone_detection_interval_bars  << ",\n";
    ss << "  \"min_zone_age_days\": "             << c.min_zone_age_days             << ",\n";
    ss << "  \"max_zone_age_days\": "             << c.max_zone_age_days             << ",\n";
    ss << "  \"max_touch_count\": "               << c.max_touch_count               << ",\n";
    ss << "  \"duplicate_prevention_enabled\": "  << (c.duplicate_prevention_enabled ? 1 : 0) << ",\n";
    ss << "  \"zone_existence_tolerance_atr\": "  << c.zone_existence_tolerance_atr  << ",\n";
    ss << "  \"skip_violated_zones\": "           << (c.skip_violated_zones ? 1 : 0) << ",\n";
    ss << "  \"use_ema_trend_filter\": "          << (c.use_ema_trend_filter ? 1 : 0)<< ",\n";
    ss << "  \"ema_fast_period\": "               << c.ema_fast_period               << ",\n";
    ss << "  \"ema_slow_period\": "               << c.ema_slow_period               << ",\n";
    ss << "  \"require_ema_longs\": "             << (c.require_ema_alignment_for_longs ? 1 : 0)  << ",\n";
    ss << "  \"require_ema_shorts\": "            << (c.require_ema_alignment_for_shorts ? 1 : 0) << ",\n";
    ss << "  \"use_trailing_stop\": "             << (c.use_trailing_stop ? 1 : 0)   << ",\n";
    ss << "  \"trail_activation_rr\": "           << c.trail_activation_rr           << ",\n";
    ss << "  \"trail_atr_multiplier\": "          << c.trail_atr_multiplier          << ",\n";
    ss << "  \"sl_buffer_atr\": "                 << c.sl_buffer_atr                 << ",\n";
    ss << "  \"risk_reward_ratio\": "             << c.risk_reward_ratio             << ",\n";
    ss << "  \"max_concurrent_trades\": "         << c.max_concurrent_trades         << ",\n";
    ss << "  \"max_loss_per_trade\": "            << c.max_loss_per_trade            << ",\n";
    ss << "  \"close_eod\": "                     << (c.close_eod ? 1 : 0)           << ",\n";
    ss << "  \"session_end_time\": \""            << c.session_end_time              << "\",\n";
    ss << "  \"enable_volume_entry_filters\": "   << (c.enable_volume_entry_filters ? 1 : 0) << ",\n";
    ss << "  \"enable_oi_entry_filters\": "       << (c.enable_oi_entry_filters ? 1 : 0)     << ",\n";
    ss << "  \"enable_volume_exit_signals\": "    << (c.enable_volume_exit_signals ? 1 : 0)  << ",\n";
    ss << "  \"enable_oi_exit_signals\": "        << (c.enable_oi_exit_signals ? 1 : 0)      << ",\n";
    ss << "  \"live_zone_detect_interval\": "     << c.live_zone_detection_interval_bars      << ",\n";
    ss << "  \"use_relaxed_live_detection\": "    << (c.use_relaxed_live_detection ? 1 : 0)  << ",\n";
    ss << "  \"live_min_zone_width_atr\": "       << c.live_min_zone_width_atr       << ",\n";
    ss << "  \"live_min_zone_strength\": "        << c.live_min_zone_strength        << ",\n";
    ss << "  \"base_score_weight\": "             << c.base_score_weight             << ",\n";
    ss << "  \"volume_score_weight\": "           << c.volume_score_weight           << ",\n";
    ss << "  \"oi_score_weight\": "               << c.oi_score_weight               << "\n";
    ss << "}";
    return ss.str();
}

// ============================================================
// hash_config
// ============================================================

uint64_t ConfigCascadeResolver::fnv1a_hash(const std::string& data) {
    constexpr uint64_t PRIME  = 0x100000001B3ULL;
    constexpr uint64_t OFFSET = 0xCBF29CE484222325ULL;
    uint64_t h = OFFSET;
    for (unsigned char c : data) {
        h ^= static_cast<uint64_t>(c);
        h *= PRIME;
    }
    return h;
}

std::string ConfigCascadeResolver::hash_config(const Config& config) {
    const std::string json = serialize_config_to_json(config);
    const uint64_t h = fnv1a_hash(json);
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << h;
    return ss.str();
}

// ============================================================
// log_resolution_report
// ============================================================

void ConfigCascadeResolver::log_resolution_report(
    const CascadeResolutionReport& r)
{
    LOG_INFO("=================================================");
    LOG_INFO("[Cascade] Resolution: " << r.symbol);
    if (r.used_fallback_mode) {
        LOG_WARN("[Cascade]   Mode:   FLAT FALLBACK");
        LOG_WARN("[Cascade]   File:   " << r.fallback_path);
    } else {
        LOG_INFO("[Cascade]   Mode:   3-TIER CASCADE");
        LOG_INFO("[Cascade]   Tier1:  " << r.master_path);
        LOG_INFO("[Cascade]   Tier2:  "
                 << (r.class_path.empty() ? "(not found)" : r.class_path)
                 << (r.class_overrides.empty() ? "" :
                     " [" + std::to_string(r.class_overrides.size()) + " overrides]"));
        LOG_INFO("[Cascade]   Tier3:  "
                 << (r.symbol_path.empty() ? "(not found)" : r.symbol_path)
                 << (r.symbol_overrides.empty() ? "" :
                     " [" + std::to_string(r.symbol_overrides.size()) + " overrides]"));
        if (!r.class_overrides.empty()) {
            LOG_INFO("[Cascade]   Class overrides:  " << format_key_list(r.class_overrides));
        }
        if (!r.symbol_overrides.empty()) {
            LOG_INFO("[Cascade]   Symbol overrides: " << format_key_list(r.symbol_overrides));
        }
    }
    LOG_INFO("[Cascade]   Hash:   " << r.config_hash);
    for (const auto& w : r.warnings) {
        LOG_WARN("[Cascade]   WARNING: " << w);
    }
    LOG_INFO("=================================================");
}

} // namespace Core
} // namespace SDTrading