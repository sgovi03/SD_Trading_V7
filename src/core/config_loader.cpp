#include "config_loader.h"
#include "../utils/logger.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <unordered_map>

namespace {
bool normalize_hhmm(const std::string& raw, std::string& normalized) {
    std::string value = raw;
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), value.end());

    size_t colon = value.find(':');
    if (colon == std::string::npos) {
        return false;
    }

    std::string hour_str = value.substr(0, colon);
    std::string minute_str = value.substr(colon + 1);

    if (hour_str.empty() || minute_str.empty() || hour_str.size() > 2 || minute_str.size() > 2) {
        return false;
    }

    if (!std::all_of(hour_str.begin(), hour_str.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }) ||
        !std::all_of(minute_str.begin(), minute_str.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return false;
    }

    int hour = std::stoi(hour_str);
    int minute = std::stoi(minute_str);
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }

    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << hour
        << ":"
        << std::setw(2) << std::setfill('0') << minute;
    normalized = oss.str();
    return true;
}
}

namespace SDTrading {
namespace Core {

Config ConfigLoader::load_from_file(const std::string& filepath) {
    std::string error_message;
    return load_from_file(filepath, error_message);
}

Config ConfigLoader::load_from_file(const std::string& filepath, std::string& error_message) {
    Config config;  // Start with defaults
    std::unordered_map<std::string, int> seen_keys;
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        error_message = "Failed to open config file: " + filepath;
        throw std::runtime_error(error_message);
    }
    
    LOG_INFO("Loading configuration from: " << filepath);
    
    std::string line;
    int line_number = 0;
    
    while (std::getline(file, line)) {
        line_number++;
        
        // Trim whitespace
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = trim(line.substr(0, eq_pos));
            if (!key.empty()) {
                auto it = seen_keys.find(key);
                if (it != seen_keys.end()) {
                    LOG_WARN("Duplicate configuration key '" << key
                             << "' at line " << line_number
                             << " overrides previous value from line " << it->second);
                }
                seen_keys[key] = line_number;
            }
        }
        
        try {
            parse_line(line, config);
        } catch (const std::exception& e) {
            LOG_WARN("Line " << line_number << ": " << e.what());
        }
    }
    
    file.close();

    if (config.enable_entry_time_block) {
        if (config.entry_block_start_time.empty() || config.entry_block_end_time.empty()) {
            error_message = "Entry time block enabled, but start/end time is missing";
            throw std::runtime_error(error_message);
        }
        if (config.entry_block_start_time >= config.entry_block_end_time) {
            error_message = "Invalid entry block window: start time must be earlier than end time (" +
                           config.entry_block_start_time + " >= " + config.entry_block_end_time + ")";
            throw std::runtime_error(error_message);
        }
    }
    
    // Validate configuration
    if (!config.validate()) {
        error_message = "Configuration validation failed";
        throw std::runtime_error(error_message);
    }
    
    LOG_INFO("Configuration loaded successfully");
    return config;
}

bool ConfigLoader::parse_section_capital_and_zones(const std::string& key, const std::string& value, Config& config) {
    // Capital & Risk
    if (key == "starting_capital") {
        config.starting_capital = std::stod(value);
    } else if (key == "risk_per_trade_pct") {
        config.risk_per_trade_pct = std::stod(value);
    } else if (key == "high_score_position_mult") {
        config.high_score_position_mult = std::stod(value);
    } else if (key == "high_score_threshold") {
        config.high_score_threshold = std::stod(value);
    }
    // Zone Detection
    else if (key == "base_height_max_atr") {
        config.base_height_max_atr = std::stod(value);
    } else if (key == "consolidation_min_bars") {
        config.consolidation_min_bars = std::stoi(value);
    } else if (key == "consolidation_max_bars") {
        config.consolidation_max_bars = std::stoi(value);
    } else if (key == "max_consolidation_range") {
        config.max_consolidation_range = std::stod(value);
    } else if (key == "min_zone_width_atr") {
        config.min_zone_width_atr = std::stod(value);
    } else if (key == "min_impulse_atr") {
        config.min_impulse_atr = std::stod(value);
    } else if (key == "atr_period") {
        config.atr_period = std::stoi(value);
    } else if (key == "min_zone_strength") {
        config.min_zone_strength = std::stod(value);
    } else if (key == "only_fresh_zones") {
        config.only_fresh_zones = parse_bool(value);
    } else if (key == "invalidate_on_body_close") {
        config.invalidate_on_body_close = parse_bool(value);
    } else if (key == "gap_over_invalidation") {
        config.gap_over_invalidation = parse_bool(value);
    } else if (key == "skip_retest_after_gap_over") {
        config.skip_retest_after_gap_over = parse_bool(value);
    }
    // Duplicate prevention
    else if (key == "duplicate_prevention_enabled") {
        config.duplicate_prevention_enabled = parse_bool(value);
    } else if (key == "zone_existence_tolerance_atr") {
        config.zone_existence_tolerance_atr = std::stod(value);
    } else if (key == "atr_buffer_multiplier") {
        config.atr_buffer_multiplier = std::stod(value);
    } else if (key == "check_existing_before_create") {
        config.check_existing_before_create = parse_bool(value);
    } else if (key == "merge_strategy" || key == "zone_merge_strategy") {
        std::string strategy = value;
        std::transform(strategy.begin(), strategy.end(), strategy.begin(), ::tolower);
        if (strategy == "update_if_stronger") {
            config.merge_strategy = ZoneMergeStrategy::UPDATE_IF_STRONGER;
        } else if (strategy == "update_if_fresher") {
            config.merge_strategy = ZoneMergeStrategy::UPDATE_IF_FRESHER;
        } else if (strategy == "expand_boundaries") {
            config.merge_strategy = ZoneMergeStrategy::EXPAND_BOUNDARIES;
        } else if (strategy == "keep_original") {
            config.merge_strategy = ZoneMergeStrategy::KEEP_ORIGINAL;
        } else {
            LOG_WARN("Unknown merge_strategy: " << value << ", using UPDATE_IF_STRONGER");
            config.merge_strategy = ZoneMergeStrategy::UPDATE_IF_STRONGER;
        }
    } else if (key == "strength_improvement_threshold") {
        config.strength_improvement_threshold = std::stod(value);
    } else if (key == "freshness_bars_threshold") {
        config.freshness_bars_threshold = std::stoi(value);
    } else if (key == "zone_detection_interval_bars") {
        config.zone_detection_interval_bars = std::stoi(value);
    }
    // Sweep and reclaim
    else if (key == "enable_sweep_reclaim") {
        config.enable_sweep_reclaim = parse_bool(value);
    } else if (key == "reclaim_acceptance_bars") {
        config.reclaim_acceptance_bars = std::stoi(value);
    } else if (key == "reclaim_acceptance_pct") {
        config.reclaim_acceptance_pct = std::stod(value);
    }
    // Zone flip
    else if (key == "enable_zone_flip") {
        config.enable_zone_flip = parse_bool(value);
    } else if (key == "flip_min_impulse_atr") {
        config.flip_min_impulse_atr = std::stod(value);
    } else if (key == "flip_lookback_bars") {
        config.flip_lookback_bars = std::stoi(value);
    } else {
        return false;
    }

    return true;
}

bool ConfigLoader::parse_section_scoring(const std::string& key, const std::string& value, Config& config) {
    // Scoring
    if (key == "use_adaptive_entry") {
        config.use_adaptive_entry = parse_bool(value);
    }
    // V6.0 Volume Pattern Filters (Feb 2026)
    else if (key == "max_entry_volume_ratio") {
        config.max_entry_volume_ratio = std::stod(value);
    } else if (key == "max_volume_without_elite") {
        config.max_volume_without_elite = std::stod(value);
    } else if (key == "min_inst_for_high_volume") {
        config.min_inst_for_high_volume = std::stod(value);
    } else if (key == "optimal_volume_min") {
        config.optimal_volume_min = std::stod(value);
    } else if (key == "optimal_volume_max") {
        config.optimal_volume_max = std::stod(value);
    } else if (key == "optimal_institutional_min") {
        config.optimal_institutional_min = std::stod(value);
    } else if (key == "allow_low_volume_if_score_above") {
        config.allow_low_volume_if_score_above = std::stod(value);
    } else if (key == "elite_institutional_threshold") {
        config.elite_institutional_threshold = std::stod(value);
    }
    // Two-stage scoring
    else if (key == "enable_two_stage_scoring") {
        config.enable_two_stage_scoring = parse_bool(value);
    } else if (key == "zone_quality_minimum_score") {
        config.zone_quality_minimum_score = std::stod(value);
    } else if (key == "zone_quality_age_very_fresh") {
        config.zone_quality_age_very_fresh = std::stoi(value);
    } else if (key == "zone_quality_age_fresh") {
        config.zone_quality_age_fresh = std::stoi(value);
    } else if (key == "zone_quality_age_recent") {
        config.zone_quality_age_recent = std::stoi(value);
    } else if (key == "zone_quality_age_aging") {
        config.zone_quality_age_aging = std::stoi(value);
    } else if (key == "zone_quality_age_old") {
        config.zone_quality_age_old = std::stoi(value);
    } else if (key == "zone_quality_height_optimal_min") {
        config.zone_quality_height_optimal_min = std::stod(value);
    } else if (key == "zone_quality_height_optimal_max") {
        config.zone_quality_height_optimal_max = std::stod(value);
    } else if (key == "zone_quality_height_acceptable_min") {
        config.zone_quality_height_acceptable_min = std::stod(value);
    } else if (key == "zone_quality_height_acceptable_max") {
        config.zone_quality_height_acceptable_max = std::stod(value);
    }
    // NEW: Revamped scoring parameters
    else if (key == "enable_revamped_scoring") {
        config.enable_revamped_scoring = parse_bool(value);
    } else if (key == "rejection_analysis_lookback_days") {
        config.rejection_analysis_lookback_days = std::stoi(value);
    } else if (key == "breakthrough_analysis_lookback_days") {
        config.breakthrough_analysis_lookback_days = std::stoi(value);
    } else if (key == "rejection_excellent_threshold") {
        config.rejection_excellent_threshold = std::stod(value);
    } else if (key == "rejection_good_threshold") {
        config.rejection_good_threshold = std::stod(value);
    } else if (key == "rejection_acceptable_threshold") {
        config.rejection_acceptable_threshold = std::stod(value);
    } else if (key == "breakthrough_disqualify_threshold") {
        config.breakthrough_disqualify_threshold = std::stod(value);
    } else if (key == "breakthrough_high_penalty_threshold") {
        config.breakthrough_high_penalty_threshold = std::stod(value);
    } else if (key == "breakthrough_low_penalty_threshold") {
        config.breakthrough_low_penalty_threshold = std::stod(value);
    } else if (key == "touch_count_exhausted_threshold") {
        config.touch_count_exhausted_threshold = std::stoi(value);
    } else if (key == "touch_count_high_threshold") {
        config.touch_count_high_threshold = std::stoi(value);
    } else if (key == "touch_count_moderate_threshold") {
        config.touch_count_moderate_threshold = std::stoi(value);
    } else if (key == "touch_count_untested_threshold") {
        config.touch_count_untested_threshold = std::stoi(value);
    } else if (key == "elite_full_bonus_age_days") {
        config.elite_full_bonus_age_days = std::stoi(value);
    } else if (key == "elite_half_bonus_age_days") {
        config.elite_half_bonus_age_days = std::stoi(value);
    } else if (key == "age_fresh_threshold_days") {
        config.age_fresh_threshold_days = std::stoi(value);
    } else if (key == "age_moderate_threshold_days") {
        config.age_moderate_threshold_days = std::stoi(value);
    } else if (key == "age_old_threshold_days") {
        config.age_old_threshold_days = std::stoi(value);
    } else if (key == "entry_validation_minimum_score") {
        config.entry_validation_minimum_score = std::stod(value);
    } else if (key == "entry_validation_ema_fast_period") {
        config.entry_validation_ema_fast_period = std::stoi(value);
    } else if (key == "entry_validation_ema_slow_period") {
        config.entry_validation_ema_slow_period = std::stoi(value);
    } else if (key == "entry_validation_strong_trend_sep") {
        config.entry_validation_strong_trend_sep = std::stod(value);
    } else if (key == "entry_validation_moderate_trend_sep") {
        config.entry_validation_moderate_trend_sep = std::stod(value);
    } else if (key == "entry_validation_weak_trend_sep") {
        config.entry_validation_weak_trend_sep = std::stod(value);
    } else if (key == "entry_validation_ranging_threshold") {
        config.entry_validation_ranging_threshold = std::stod(value);
    } else if (key == "entry_validation_rsi_deeply_oversold") {
        config.entry_validation_rsi_deeply_oversold = std::stod(value);
    } else if (key == "entry_validation_rsi_oversold") {
        config.entry_validation_rsi_oversold = std::stod(value);
    } else if (key == "entry_validation_rsi_slightly_oversold") {
        config.entry_validation_rsi_slightly_oversold = std::stod(value);
    } else if (key == "entry_validation_rsi_pullback") {
        config.entry_validation_rsi_pullback = std::stod(value);
    } else if (key == "entry_validation_rsi_neutral") {
        config.entry_validation_rsi_neutral = std::stod(value);
    } else if (key == "entry_validation_macd_strong_threshold") {
        config.entry_validation_macd_strong_threshold = std::stod(value);
    } else if (key == "entry_validation_macd_moderate_threshold") {
        config.entry_validation_macd_moderate_threshold = std::stod(value);
    } else if (key == "entry_validation_adx_very_strong") {
        config.entry_validation_adx_very_strong = std::stod(value);
    } else if (key == "entry_validation_adx_strong") {
        config.entry_validation_adx_strong = std::stod(value);
    } else if (key == "entry_validation_adx_moderate") {
        config.entry_validation_adx_moderate = std::stod(value);
    } else if (key == "entry_validation_adx_weak") {
        config.entry_validation_adx_weak = std::stod(value);
    } else if (key == "entry_validation_adx_minimum") {
        config.entry_validation_adx_minimum = std::stod(value);
    } else if (key == "entry_validation_optimal_stop_atr_min") {
        config.entry_validation_optimal_stop_atr_min = std::stod(value);
    } else if (key == "entry_validation_optimal_stop_atr_max") {
        config.entry_validation_optimal_stop_atr_max = std::stod(value);
    } else if (key == "entry_validation_acceptable_stop_atr_min") {
        config.entry_validation_acceptable_stop_atr_min = std::stod(value);
    } else if (key == "entry_validation_acceptable_stop_atr_max") {
        config.entry_validation_acceptable_stop_atr_max = std::stod(value);
    } else if (key == "zone_max_age_days") {
      config.zone_max_age_days = std::stoi(value);
	} else if (key == "zone_max_touch_count") {
      config.zone_max_touch_count = std::stoi(value);
	}
    // Scoring Weights
    else if (key == "scoring_weight_base_strength") {
        config.scoring.weight_base_strength = std::stod(value);
    } else if (key == "scoring_weight_elite_bonus") {
        config.scoring.weight_elite_bonus = std::stod(value);
    } else if (key == "scoring_weight_regime_alignment") {
        config.scoring.weight_regime_alignment = std::stod(value);
    } else if (key == "scoring_weight_state_freshness") {
        config.scoring.weight_state_freshness = std::stod(value);
    } else if (key == "scoring_weight_rejection_confirmation") {
        config.scoring.weight_rejection_confirmation = std::stod(value);
    }
    // Entry Thresholds
    else if (key == "entry_minimum_score") {
        config.scoring.entry_minimum_score = std::stod(value);
    } else if (key == "entry_optimal_score") {
        config.scoring.entry_optimal_score = std::stod(value);
    }
    // Risk/Reward Scaling
    else if (key == "rr_base_ratio") {
        config.scoring.rr_base_ratio = std::stod(value);
    } else if (key == "rr_scale_with_score") {
        config.scoring.rr_scale_with_score = parse_bool(value);
    } else if (key == "rr_max_ratio") {
        config.scoring.rr_max_ratio = std::stod(value);
    }
    // Zone exhaustion filter
    else if (key == "max_zone_touch_count") {
        config.scoring.max_zone_touch_count = std::stoi(value);
    }
    // EMA Exit
    else if (key == "enable_ema_exit") {
        config.enable_ema_exit = parse_bool(value);
    }
    // Rejection Wick Thresholds
    else if (key == "rejection_strong_threshold") {
        config.scoring.rejection_strong_threshold = std::stod(value);
    } else if (key == "rejection_moderate_threshold") {
        config.scoring.rejection_moderate_threshold = std::stod(value);
    } else if (key == "rejection_weak_threshold") {
        config.scoring.rejection_weak_threshold = std::stod(value);
    }
    // HTF Analysis
    else if (key == "use_htf_regime_filter") {
        config.use_htf_regime_filter = parse_bool(value);
    } else if (key == "htf_lookback_bars") {
        config.htf_lookback_bars = std::stoi(value);
    } else if (key == "htf_trending_threshold") {
        config.htf_trending_threshold = std::stod(value);
    }
    // Elite Detection
    else if (key == "min_departure_imbalance") {
        config.min_departure_imbalance = std::stod(value);
    } else if (key == "max_retest_speed_atr") {
        config.max_retest_speed_atr = std::stod(value);
    } else if (key == "min_bars_to_retest") {
        config.min_bars_to_retest = std::stoi(value);
    }
    // Market Structure
    else if (key == "use_bos_trend_filter") {
        config.use_bos_trend_filter = parse_bool(value);
    } else if (key == "swing_detection_bars") {
        config.swing_detection_bars = std::stoi(value);
    } else if (key == "trade_with_trend_only") {
        config.trade_with_trend_only = parse_bool(value);
    } else if (key == "allow_ranging_trades") {
        config.allow_ranging_trades = parse_bool(value);
    } else if (key == "enable_volume_exhaustion_exit") {
        config.enable_volume_exhaustion_exit = (value == "YES" || value == "yes");
    }
    else if (key == "vol_exhaustion_spike_min_ratio") {
        config.vol_exhaustion_spike_min_ratio = std::stod(value);
    }
    else if (key == "vol_exhaustion_spike_min_body_atr") {
        config.vol_exhaustion_spike_min_body_atr = std::stod(value);
    }
    else if (key == "vol_exhaustion_absorption_min_ratio") {
        config.vol_exhaustion_absorption_min_ratio = std::stod(value);
    }
    else if (key == "vol_exhaustion_absorption_max_body_atr") {
        config.vol_exhaustion_absorption_max_body_atr = std::stod(value);
    }
    else if (key == "vol_exhaustion_flow_min_ratio") {
        config.vol_exhaustion_flow_min_ratio = std::stod(value);
    }
    else if (key == "vol_exhaustion_flow_min_bars") {
        config.vol_exhaustion_flow_min_bars = std::stoi(value);
    }
    else if (key == "vol_exhaustion_drift_max_ratio") {
        config.vol_exhaustion_drift_max_ratio = std::stod(value);
    }
    else if (key == "vol_exhaustion_drift_min_loss_atr") {
        config.vol_exhaustion_drift_min_loss_atr = std::stod(value);
    }
    else if (key == "vol_exhaustion_max_loss_pct") {
        config.vol_exhaustion_max_loss_pct = std::stod(value);
    }
	
	
    // EMA Filter
    else if (key == "use_ema_trend_filter") {
        config.use_ema_trend_filter = parse_bool(value);
    } else if (key == "ema_fast_period") {
        config.ema_fast_period = std::stoi(value);
    } else if (key == "ema_slow_period") {
        config.ema_slow_period = std::stoi(value);
    } else if (key == "ema_ranging_threshold_pct") {
        config.ema_ranging_threshold_pct = std::stod(value);
    } else {
        return false;
    }

    return true;
}

bool ConfigLoader::parse_section_indicators_and_entry(const std::string& key, const std::string& value, Config& config) {
    // Indicator periods (trade logging)
    if (key == "rsi_period") {
        config.rsi_period = std::stoi(value);
    } else if (key == "bb_period") {
        config.bb_period = std::stoi(value);
    } else if (key == "bb_stddev") {
        config.bb_stddev = std::stod(value);
    } else if (key == "adx_period") {
        config.adx_period = std::stoi(value);
    } else if (key == "macd_fast_period") {
        config.macd_fast_period = std::stoi(value);
    } else if (key == "macd_slow_period") {
        config.macd_slow_period = std::stoi(value);
    } else if (key == "macd_signal_period") {
        config.macd_signal_period = std::stoi(value);
    }
    // Entry Logic
    else if (key == "entry_at_proximal") {
        config.entry_at_proximal = parse_bool(value);
    } else if (key == "rejection_wick_ratio") {
        config.rejection_wick_ratio = std::stod(value);
    } else if (key == "entry_buffer_pct") {
        config.entry_buffer_pct = std::stod(value);
    } else if (key == "min_zone_penetration_pct") {
        config.min_zone_penetration_pct = std::stod(value);
    } else if (key == "require_price_at_entry_level") {
        config.require_price_at_entry_level = parse_bool(value);
    }
    // Risk Management
    else if (key == "sl_buffer_zone_pct") {
        config.sl_buffer_zone_pct = std::stod(value);
    } else if (key == "sl_buffer_atr") {
        config.sl_buffer_atr = std::stod(value);
    } else if (key == "min_stop_distance_points") {
    config.min_stop_distance_points = std::stod(value);
	} else if (key == "max_fill_slippage_pts") {
        config.max_fill_slippage_pts = std::stod(value);
    } else if (key == "risk_reward_ratio") {
        config.risk_reward_ratio = std::stod(value);
    } else if (key == "use_breakeven_stop_loss") {
        config.use_breakeven_stop_loss = parse_bool(value);
    }
    // Trailing Stop
    else if (key == "use_trailing_stop") {
        config.use_trailing_stop = parse_bool(value);
    } else if (key == "trail_activation_rr") {
        config.trail_activation_rr = std::stod(value);
    } else if (key == "trail_ema_period") {
        config.trail_ema_period = std::stoi(value);
    } else if (key == "trail_ema_buffer_pct") {
        config.trail_ema_buffer_pct = std::stod(value);
    } else if (key == "trail_atr_multiplier") {
        config.trail_atr_multiplier = std::stod(value);
    } else if (key == "trail_use_hybrid") {
        config.trail_use_hybrid = parse_bool(value);
    } else if (key == "trail_fallback_tp_rr") {
        config.trail_fallback_tp_rr = std::stod(value);
    } else {
        return false;
    }

    return true;
}

bool ConfigLoader::parse_section_trade_and_runtime(const std::string& key, const std::string& value, Config& config) {
    // Trade Management
    if (key == "max_consecutive_losses") {
        config.max_consecutive_losses = std::stoi(value);
    } else if (key == "pause_bars_after_losses") {
        config.pause_bars_after_losses = std::stoi(value);
    } else if (key == "max_concurrent_trades") {
        config.max_concurrent_trades = std::stoi(value);
    } else if (key == "max_retries_per_zone") {
        config.max_retries_per_zone = std::stoi(value);
    } else if (key == "enable_per_zone_retry_limit") {
        config.enable_per_zone_retry_limit = parse_bool(value);
    } else if (key == "lookback_for_zones") {
        config.lookback_for_zones = std::stoi(value);
    }
    // Session Management
    else if (key == "close_eod") {
        config.close_eod = parse_bool(value);
    } else if (key == "session_end_time") {
        config.session_end_time = value;
    } else if (key == "close_before_session_end_minutes") {
        config.close_before_session_end_minutes = std::stoi(value);
    } else if (key == "enable_entry_time_block") {
        config.enable_entry_time_block = parse_bool(value);
    } else if (key == "entry_block_start_time") {
        std::string normalized;
        if (!normalize_hhmm(value, normalized)) {
            throw std::runtime_error("Invalid entry_block_start_time '" + value + "' (expected HH:MM, 24-hour format)");
        }
        if (normalized != value) {
            LOG_WARN("Normalized entry_block_start_time from '" << value << "' to '" << normalized << "'");
        }
        config.entry_block_start_time = normalized;
    } else if (key == "entry_block_end_time") {
        std::string normalized;
        if (!normalize_hhmm(value, normalized)) {
            throw std::runtime_error("Invalid entry_block_end_time '" + value + "' (expected HH:MM, 24-hour format)");
        }
        if (normalized != value) {
            LOG_WARN("Normalized entry_block_end_time from '" << value << "' to '" << normalized << "'");
        }
        config.entry_block_end_time = normalized;
    }
    // Overnight Continuation Trade (Issue 3)
    else if (key == "enable_overnight_continuation") {
        config.enable_overnight_continuation = parse_bool(value);
    } else if (key == "continuation_min_profit_pts") {
        config.continuation_min_profit_pts = std::stod(value);
    } else if (key == "continuation_min_score_pct") {
        config.continuation_min_score_pct = std::stod(value);
    } else if (key == "continuation_max_gap_pct") {
        config.continuation_max_gap_pct = std::stod(value);
    }
    // Position Sizing
    else if (key == "lot_size") {
        config.lot_size = std::stoi(value);
    } else if (key == "commission_per_trade") {
        config.commission_per_trade = std::stod(value);
    }
    // Logging
    else if (key == "enable_debug_logging") {
        config.enable_debug_logging = parse_bool(value);
    } else if (key == "enable_score_logging") {
        config.enable_score_logging = parse_bool(value);
    } else if (key == "live_export_every_n_bars") {
        config.live_export_every_n_bars = std::stoi(value);
    } else if (key == "exit_ema_fast_period") {
        config.exit_ema_fast_period = std::stoi(value);
    } else if (key == "exit_ema_slow_period") {
        config.exit_ema_slow_period = std::stoi(value);
    } else if (key == "enable_candle_confirmation") {
        config.enable_candle_confirmation = parse_bool(value);
    } else if (key == "candle_confirmation_body_pct") {
        config.candle_confirmation_body_pct = std::stod(value);
    } else if (key == "enable_structure_confirmation") {
        config.enable_structure_confirmation = parse_bool(value);
    }
    // Live-specific filters and gating
    else if (key == "enable_live_zone_filtering") {
        config.enable_live_zone_filtering = parse_bool(value);
    } else if (key == "live_zone_range_pct") {
        config.live_zone_range_pct = std::stod(value);
    } else if (key == "live_zone_refresh_interval_minutes") {
        config.live_zone_refresh_interval_minutes = std::stoi(value);
    } else if (key == "max_zone_distance_atr") {
        config.max_zone_distance_atr = std::stod(value);
    } else if (key == "live_zone_detection_interval_bars") {
        config.live_zone_detection_interval_bars = std::stoi(value);
    } else if (key == "use_relaxed_live_detection") {
        config.use_relaxed_live_detection = parse_bool(value);
    } else if (key == "live_min_zone_width_atr") {
        config.live_min_zone_width_atr = std::stod(value);
    } else if (key == "live_min_zone_strength") {
        config.live_min_zone_strength = std::stod(value);
    } else if (key == "live_zone_detection_lookback_bars") {
        config.live_zone_detection_lookback_bars = std::stoi(value);
    } else if (key == "live_entry_require_new_bar") {
        config.live_entry_require_new_bar = parse_bool(value);
    } else if (key == "live_skip_when_in_position") {
        config.live_skip_when_in_position = parse_bool(value);
    } else if (key == "live_zone_entry_cooldown_seconds") {
        config.live_zone_entry_cooldown_seconds = std::stoi(value);
    }
    // DryRun parameters
    else if (key == "dryrun_bootstrap_bars") {
        config.dryrun_bootstrap_bars = std::stoi(value);
    } else if (key == "dryrun_test_start_bar") {
        config.dryrun_test_start_bar = std::stoi(value);
    } else if (key == "dryrun_disable_trading_in_bootstrap") {
        config.dryrun_disable_trading_in_bootstrap = parse_bool(value);
    } else if (key == "allow_large_bootstrap" || key == "SD_ALLOW_LARGE_BOOTSTRAP") {
        config.allow_large_bootstrap = parse_bool(value);
    }
    // State History
    else if (key == "enable_state_history") {
        config.enable_state_history = parse_bool(value);
    } else if (key == "max_state_history_events") {
        config.max_state_history_events = std::stoi(value);
    } else if (key == "record_zone_creation") {
        config.record_zone_creation = parse_bool(value);
    } else if (key == "record_first_touch") {
        config.record_first_touch = parse_bool(value);
    } else if (key == "record_retests") {
        config.record_retests = parse_bool(value);
    } else if (key == "record_violations") {
        config.record_violations = parse_bool(value);
    }
    // Fixes and additional filters
    else if (key == "max_loss_per_trade") {
        config.max_loss_per_trade = std::stod(value);
    } else if (key == "require_ema_alignment_for_longs") {
        config.require_ema_alignment_for_longs = parse_bool(value);
    } else if (key == "require_ema_alignment_for_shorts") {
        config.require_ema_alignment_for_shorts = parse_bool(value);
    } else if (key == "ema_min_separation_pct_long") {
        config.ema_min_separation_pct_long = std::stod(value);
    } else if (key == "ema_min_separation_pct_short") {
        config.ema_min_separation_pct_short = std::stod(value);
    } else if (key == "min_zone_age_days") {
        config.min_zone_age_days = std::stoi(value);
    } else if (key == "max_zone_age_days") {
        config.max_zone_age_days = std::stoi(value);
    } else if (key == "max_touch_count") {
        config.max_touch_count = std::stoi(value);
    } else if (key == "skip_violated_zones") {
        config.skip_violated_zones = parse_bool(value);
    } else if (key == "skip_tested_zones") {
        config.skip_tested_zones = parse_bool(value);
    } else if (key == "prefer_fresh_zones") {
        config.prefer_fresh_zones = parse_bool(value);
    } else if (key == "exclude_zone_age_range") {
        config.exclude_zone_age_range = parse_bool(value);
    } else if (key == "exclude_zone_age_start") {
        config.exclude_zone_age_start = std::stoi(value);
    } else if (key == "exclude_zone_age_end") {
        config.exclude_zone_age_end = std::stoi(value);
    } else if (key == "trailing_stop_activation_r") {
        config.trailing_stop_activation_r = std::stod(value);
    } else {
        return parse_section_trade_and_runtime_extended(key, value, config);
    }

    return true;
}

bool ConfigLoader::parse_section_trade_and_runtime_extended(const std::string& key, const std::string& value, Config& config) {
    // ============================================================
    // New filters — Priority 1-4 (March 2026 live trade analysis)
    // ============================================================
    // Priority 1: Zone consecutive-loss exhaustion
    if (key == "enable_zone_exhaustion") {
        config.enable_zone_exhaustion = parse_bool(value);
    } else if (key == "zone_consecutive_loss_max") {
        config.zone_consecutive_loss_max = std::stoi(value);
    } else if (key == "zone_sl_suspend_threshold") {
        config.zone_sl_suspend_threshold = std::stoi(value);
    }
    // Priority 2a: RSI extreme hard block
    else if (key == "enable_rsi_hard_block") {
        config.enable_rsi_hard_block = parse_bool(value);
    } else if (key == "rsi_hard_block_high") {
        config.rsi_hard_block_high = std::stod(value);
    } else if (key == "rsi_hard_block_low") {
        config.rsi_hard_block_low = std::stod(value);
    }
    // Priority 2c: Freshness + BB Bandwidth compound block
    else if (key == "enable_freshness_bb_block") {
        config.enable_freshness_bb_block = parse_bool(value);
    } else if (key == "freshness_bb_min_freshness") {
        config.freshness_bb_min_freshness = std::stod(value);
    } else if (key == "freshness_bb_max_bandwidth") {
        config.freshness_bb_max_bandwidth = std::stod(value);
    }
    // Priority 2b: ADX transition hard block
    else if (key == "enable_adx_transition_filter") {
        config.enable_adx_transition_filter = parse_bool(value);
    } else if (key == "adx_transition_min") {
        config.adx_transition_min = std::stod(value);
    } else if (key == "adx_transition_max") {
        config.adx_transition_max = std::stod(value);
    } else if (key == "adx_transition_size_factor") {
        config.adx_transition_size_factor = std::stod(value);
    } else if (key == "adx_transition_skip_entry") {
        config.adx_transition_skip_entry = parse_bool(value);
    }
    // Priority 3: Late-session entry cutoff
    else if (key == "enable_late_entry_cutoff") {
        config.enable_late_entry_cutoff = parse_bool(value);
    } else if (key == "late_entry_cutoff_time") {
        std::string normalized;
        if (normalize_hhmm(value, normalized)) {
            config.late_entry_cutoff_time = normalized;
        } else {
            LOG_WARN("Invalid late_entry_cutoff_time '" << value << "' - expected HH:MM");
        }
    }
    // Priority 4: Hard lot cap
    else if (key == "max_position_lots") {
        config.max_position_lots = std::stoi(value);
    }
    // ============================================================
    // V6.0: Volume & OI integration
    // ============================================================
    // Volume Entry Filters
    else if (key == "enable_volume_entry_filters") {
        config.enable_volume_entry_filters = parse_bool(value);
    } else if (key == "min_entry_volume_ratio") {
        config.min_entry_volume_ratio = std::stod(value);
    } else if (key == "institutional_volume_threshold") {
        config.institutional_volume_threshold = std::stod(value);
    }
    // OI Entry Filters
    else if (key == "enable_oi_entry_filters") {
        config.enable_oi_entry_filters = parse_bool(value);
    } else if (key == "enable_market_phase_detection") {
        config.enable_market_phase_detection = parse_bool(value);
    }
    // Scoring Weights
    else if (key == "base_score_weight") {
        config.base_score_weight = std::stod(value);
    } else if (key == "volume_score_weight") {
        config.volume_score_weight = std::stod(value);
    } else if (key == "oi_score_weight") {
        config.oi_score_weight = std::stod(value);
    }
    // Dynamic Position Sizing
    else if (key == "low_volume_size_multiplier") {
        config.low_volume_size_multiplier = std::stod(value);
    } else if (key == "high_institutional_size_mult") {
        config.high_institutional_size_mult = std::stod(value);
    } else if (key == "max_lot_size") {
        config.max_lot_size = std::stoi(value);
    }
    // Volume Exit Signals
    else if (key == "enable_volume_exit_signals") {
        config.enable_volume_exit_signals = parse_bool(value);
    } else if (key == "volume_climax_exit_threshold") {
        config.volume_climax_exit_threshold = std::stod(value);
    }
    // OI Exit Signals
    else if (key == "enable_oi_exit_signals") {
        config.enable_oi_exit_signals = parse_bool(value);
    } else if (key == "oi_unwinding_threshold") {
        config.oi_unwinding_threshold = std::stod(value);
    } else if (key == "oi_reversal_threshold") {
        config.oi_reversal_threshold = std::stod(value);
    }
    // OI Exit Signals (extended)
    else if (key == "oi_stagnation_threshold") {
        config.oi_stagnation_threshold = std::stod(value);
    } else if (key == "oi_stagnation_bar_count") {
        config.oi_stagnation_bar_count = std::stoi(value);
    }
    // Volume Analysis Parameters
    else if (key == "volume_lookback_period") {
        config.volume_lookback_period = std::stoi(value);
    } else if (key == "extreme_volume_threshold") {
        config.extreme_volume_threshold = std::stod(value);
    } else if (key == "volume_normalization_method") {
        config.volume_normalization_method = value;
    } else if (key == "volume_baseline_file") {
        config.volume_baseline_file = value;
    }
    // OI Analysis Parameters
    else if (key == "min_oi_change_threshold") {
        config.min_oi_change_threshold = std::stod(value);
    } else if (key == "high_oi_buildup_threshold") {
        config.high_oi_buildup_threshold = std::stod(value);
    } else if (key == "oi_lookback_period") {
        config.oi_lookback_period = std::stoi(value);
    } else if (key == "price_oi_correlation_window") {
        config.price_oi_correlation_window = std::stod(value);
    }
    // Scoring Bonuses
    else if (key == "institutional_volume_bonus") {
        config.institutional_volume_bonus = std::stod(value);
    } else if (key == "oi_alignment_bonus") {
        config.oi_alignment_bonus = std::stod(value);
    } else if (key == "low_volume_retest_bonus") {
        config.low_volume_retest_bonus = std::stod(value);
    }
    // Expiry Day Handling
    else if (key == "trade_on_expiry_day") {
        config.trade_on_expiry_day = parse_bool(value);
    } else if (key == "expiry_day_disable_oi_filters") {
        config.expiry_day_disable_oi_filters = parse_bool(value);
    } else if (key == "expiry_day_position_multiplier") {
        config.expiry_day_position_multiplier = std::stod(value);
    } else if (key == "expiry_day_min_zone_score") {
        config.expiry_day_min_zone_score = std::stod(value);
    }
    // Entry Volume Score Filter (V6.0)
    else if (key == "min_volume_entry_score") {
        config.min_volume_entry_score = std::stoi(value);
    }
    // Volume Exit Signals (extended)
    else if (key == "volume_drying_up_threshold") {
        config.volume_drying_up_threshold = std::stod(value);
    } else if (key == "volume_drying_up_bar_count") {
        config.volume_drying_up_bar_count = std::stoi(value);
    } else if (key == "enable_volume_divergence_exit") {
        config.enable_volume_divergence_exit = parse_bool(value);
    }
    // Feature Flags
    else if (key == "v6_fully_enabled") {
        config.v6_fully_enabled = parse_bool(value);
    } else if (key == "v6_log_volume_filters") {
        config.v6_log_volume_filters = parse_bool(value);
    } else if (key == "v6_log_oi_filters") {
        config.v6_log_oi_filters = parse_bool(value);
    } else if (key == "v6_log_institutional_index") {
        config.v6_log_institutional_index = parse_bool(value);
    } else if (key == "v6_validate_baseline_on_startup") {
        config.v6_validate_baseline_on_startup = parse_bool(value);
    }
    // ============================================================
    else if (key == "min_entry_sl_points") {
        config.min_entry_sl_points = std::stod(value);
    } else {
        // ── Extended keys (inside else to avoid MSVC C1061 nesting limit) ──
        if (key == "zone_bootstrap_force_regenerate") {
            config.zone_bootstrap_force_regenerate = parse_bool(value);
        } else if (key == "zone_bootstrap_enabled") {
            config.zone_bootstrap_enabled = parse_bool(value);
        } else if (key == "zone_bootstrap_ttl_hours") {
            config.zone_bootstrap_ttl_hours = std::stoi(value);
        } else if (key == "zone_bootstrap_refresh_time") {
            config.zone_bootstrap_refresh_time = value;
        } else if (key == "enable_macd_histogram_filter") {
            config.enable_macd_histogram_filter = parse_bool(value);
        } else if (key == "macd_histogram_threshold") {
            config.macd_histogram_threshold = std::stod(value);
        } else if (key == "enable_entry_block2") {
            config.enable_entry_block2 = parse_bool(value);
        } else if (key == "entry_block2_start_time") {
            config.entry_block2_start_time = value;
        } else if (key == "entry_block2_end_time") {
            config.entry_block2_end_time = value;
        } else if (key == "zone_quality_maximum_score") {
            config.zone_quality_maximum_score = std::stod(value);
        } else if (key == "enable_bb_bandwidth_filter") {
            config.enable_bb_bandwidth_filter = parse_bool(value);
        } else if (key == "bb_bandwidth_min") {
            config.bb_bandwidth_min = std::stod(value);
        } else if (key == "bb_bandwidth_max") {
            config.bb_bandwidth_max = std::stod(value);
        } else {
            return false;
        }
    }

    return true;
}

void ConfigLoader::parse_line(const std::string& line, Config& config) {
    // Find the '=' separator
    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
        throw std::runtime_error("Invalid line format (no '=' found): " + line);
    }
    
    std::string key = trim(line.substr(0, eq_pos));
    std::string value_with_comment = line.substr(eq_pos + 1);
    
    // Strip inline comments (anything after '#')
    size_t comment_pos = value_with_comment.find('#');
    std::string value;
    if (comment_pos != std::string::npos) {
        value = trim(value_with_comment.substr(0, comment_pos));
    } else {
        value = trim(value_with_comment);
    }
    
    if (key.empty() || value.empty()) {
        throw std::runtime_error("Empty key or value");
    }

    // Check for legacy/unknown keys first - silently ignore them
    if (key == "stop_loss_atr_multiplier" || 
        key == "target_rr_ratio" ||
        key == "entry_score_zone_quality_max" ||
        key == "entry_score_volume_entry_max" ||
        key == "entry_score_market_context_max" ||
        key == "entry_score_total_max" ||
        key == "entry_score_zone_quality_min" ||
        key == "entry_score_volume_entry_min" ||
        key == "entry_score_market_context_min" ||
        key == "entry_score_total_min" ||
        key == "departure_volume_extreme_ratio" ||
        key == "departure_volume_strong_ratio" ||
        key == "departure_volume_moderate_ratio" ||
        key == "departure_volume_normal_ratio" ||
        key == "departure_volume_weak_penalty" ||
        key == "initiative_bonus" ||
        key == "absorption_penalty" ||
        key == "multi_touch_rising_penalty" ||
        key == "pullback_volume_very_low" ||
        key == "pullback_volume_low" ||
        key == "pullback_volume_moderate" ||
        key == "pullback_volume_elevated" ||
        key == "pullback_volume_high" ||
        key == "pullback_volume_very_high_penalty" ||
        key == "entry_bar_strong_body_pct" ||
        key == "entry_bar_moderate_body_pct" ||
        key == "entry_bar_weak_penalty" ||
        key == "recent_volume_declining_bonus" ||
        key == "recent_volume_stable_bonus" ||
        key == "recent_volume_rising_penalty" ||
        key == "volume_pattern_sweet_spot_min" ||
        key == "volume_pattern_sweet_spot_max" ||
        key == "volume_pattern_inst_min" ||
        key == "volume_pattern_high_trap_penalty") {
        // Legacy parameter - silently ignored
        return;
    }

    if (parse_section_capital_and_zones(key, value, config)) {
        return;
    }
    if (parse_section_scoring(key, value, config)) {
        return;
    }
    if (parse_section_indicators_and_entry(key, value, config)) {
        return;
    }
    if (parse_section_trade_and_runtime(key, value, config)) {
        return;
    }

    // Unknown key - just log warning, don't fail
    LOG_WARN("Unknown configuration key: " << key);
}

std::string ConfigLoader::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool ConfigLoader::parse_bool(const std::string& value) {
    std::string lower_value = value;
    std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
    
    if (lower_value == "true" || lower_value == "1" || lower_value == "yes") {
        return true;
    } else if (lower_value == "false" || lower_value == "0" || lower_value == "no") {
        return false;
    } else {
        throw std::runtime_error("Invalid boolean value: " + value);
    }
}

bool ConfigLoader::save_to_file(const Config& config, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: " << filepath);
        return false;
    }
    
    file << "# SD Trading System Configuration\n";
    file << "# Generated configuration file\n\n";
    
    file << "# Capital & Risk\n";
    file << "starting_capital=" << config.starting_capital << "\n";
    file << "risk_per_trade_pct=" << config.risk_per_trade_pct << "\n\n";
    
    file << "# Zone Detection\n";
    file << "base_height_max_atr=" << config.base_height_max_atr << "\n";
    file << "consolidation_min_bars=" << config.consolidation_min_bars << "\n";
    file << "consolidation_max_bars=" << config.consolidation_max_bars << "\n";
    file << "max_consolidation_range=" << config.max_consolidation_range << "\n";
    file << "min_impulse_atr=" << config.min_impulse_atr << "\n";
    file << "atr_period=" << config.atr_period << "\n";
    file << "min_zone_strength=" << config.min_zone_strength << "\n";
    file << "only_fresh_zones=" << (config.only_fresh_zones ? "true" : "false") << "\n";
    file << "invalidate_on_body_close=" << (config.invalidate_on_body_close ? "true" : "false") << "\n";
    file << "gap_over_invalidation=" << (config.gap_over_invalidation ? "true" : "false") << "\n\n";
    
    // ... add rest of parameters similarly ...
    
    file.close();
    LOG_INFO("Configuration saved to: " << filepath);
    return true;
}

} // namespace Core
} // namespace SDTrading