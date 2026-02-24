#include "entry_validation_scorer.h"
#include "../analysis/market_analyzer.h"

namespace SDTrading {
namespace Core {

EntryValidationScorer::EntryValidationScorer(const Config& cfg) : config_(cfg) {}

EntryValidationScore EntryValidationScorer::calculate(const Zone& zone,
                                                      const std::vector<Bar>& bars,
                                                      int current_index,
                                                      double entry_price,
                                                      double stop_loss) const {
    EntryValidationScore score;

    if (current_index < 0 || current_index >= static_cast<int>(bars.size())) {
        return score;
    }

    score.trend_alignment = calculate_trend_alignment_score(zone, bars, current_index);
    score.momentum_state = calculate_momentum_score(zone, bars, current_index);
    score.trend_strength = calculate_trend_strength_score(bars, current_index);
    score.volatility_context = calculate_volatility_score(entry_price, stop_loss, bars, current_index);

    score.total = score.trend_alignment + score.momentum_state + score.trend_strength + score.volatility_context;
    return score;
}

bool EntryValidationScorer::meets_threshold(double score) const {
    return score >= config_.entry_validation_minimum_score;
}

double EntryValidationScorer::calculate_trend_alignment_score(const Zone& zone,
                                                              const std::vector<Bar>& bars,
                                                              int current_index) const {
    double ema_fast = MarketAnalyzer::calculate_ema(
        bars,
        config_.entry_validation_ema_fast_period,
        current_index
    );
    double ema_slow = MarketAnalyzer::calculate_ema(
        bars,
        config_.entry_validation_ema_slow_period,
        current_index
    );

    bool uptrend = (ema_fast > ema_slow);
    double ema_separation_pct = (ema_slow != 0.0)
        ? (std::abs(ema_fast - ema_slow) / std::abs(ema_slow)) * 100.0
        : 0.0;

    bool aligned = (zone.type == ZoneType::DEMAND && uptrend) ||
                   (zone.type == ZoneType::SUPPLY && !uptrend);

    if (aligned) {
        if (ema_separation_pct > config_.entry_validation_strong_trend_sep) {
            return 35.0;
        } else if (ema_separation_pct > config_.entry_validation_moderate_trend_sep) {
            return 30.0;
        } else if (ema_separation_pct > config_.entry_validation_weak_trend_sep) {
            return 25.0;
        }
        return 18.0;
    }

    if (ema_separation_pct < config_.entry_validation_ranging_threshold) {
        return 18.0;
    }

    return 5.0;
}

double EntryValidationScorer::calculate_momentum_score(const Zone& zone,
                                                       const std::vector<Bar>& bars,
                                                       int current_index) const {
    double rsi = MarketAnalyzer::calculate_rsi(bars, config_.rsi_period, current_index);
    auto macd = MarketAnalyzer::calculate_macd(
        bars,
        config_.macd_fast_period,
        config_.macd_slow_period,
        config_.macd_signal_period,
        current_index
    );

    double score = 0.0;

    if (zone.type == ZoneType::DEMAND) {
        if (rsi < config_.entry_validation_rsi_deeply_oversold) {
            score += 20.0;
        } else if (rsi < config_.entry_validation_rsi_oversold) {
            score += 17.0;
        } else if (rsi < config_.entry_validation_rsi_slightly_oversold) {
            score += 14.0;
        } else if (rsi < config_.entry_validation_rsi_pullback) {
            score += 10.0;
        } else if (rsi < config_.entry_validation_rsi_neutral) {
            score += 5.0;
        }

        if (macd.histogram < -config_.entry_validation_macd_strong_threshold) {
            score += 10.0;
        } else if (macd.histogram < -config_.entry_validation_macd_moderate_threshold) {
            score += 7.0;
        } else if (macd.histogram < 0.0) {
            score += 5.0;
        }
    } else {
        double rsi_inverse = 100.0 - rsi;
        if (rsi_inverse < config_.entry_validation_rsi_deeply_oversold) {
            score += 20.0;
        } else if (rsi_inverse < config_.entry_validation_rsi_oversold) {
            score += 17.0;
        } else if (rsi_inverse < config_.entry_validation_rsi_slightly_oversold) {
            score += 14.0;
        } else if (rsi_inverse < config_.entry_validation_rsi_pullback) {
            score += 10.0;
        } else if (rsi_inverse < config_.entry_validation_rsi_neutral) {
            score += 5.0;
        }

        if (macd.histogram > config_.entry_validation_macd_strong_threshold) {
            score += 10.0;
        } else if (macd.histogram > config_.entry_validation_macd_moderate_threshold) {
            score += 7.0;
        } else if (macd.histogram > 0.0) {
            score += 5.0;
        }
    }

    return score;
}

double EntryValidationScorer::calculate_trend_strength_score(const std::vector<Bar>& bars,
                                                             int current_index) const {
    auto adx_vals = MarketAnalyzer::calculate_adx(bars, config_.adx_period, current_index);
    double adx = adx_vals.adx;

    if (adx >= config_.entry_validation_adx_very_strong) {
        return 25.0;
    } else if (adx >= config_.entry_validation_adx_strong) {
        return 21.0;
    } else if (adx >= config_.entry_validation_adx_moderate) {
        return 17.0;
    } else if (adx >= config_.entry_validation_adx_weak) {
        return 12.0;
    } else if (adx >= config_.entry_validation_adx_minimum) {
        return 6.0;
    }

    return 0.0;
}

double EntryValidationScorer::calculate_volatility_score(double entry_price,
                                                         double stop_loss,
                                                         const std::vector<Bar>& bars,
                                                         int current_index) const {
    double atr = MarketAnalyzer::calculate_atr(bars, config_.atr_period, current_index);
    if (atr <= 0.0) {
        return 0.0;
    }

    double stop_distance = std::abs(entry_price - stop_loss);
    double stop_to_atr = stop_distance / atr;

    if (stop_to_atr >= config_.entry_validation_optimal_stop_atr_min &&
        stop_to_atr <= config_.entry_validation_optimal_stop_atr_max) {
        return 10.0;
    } else if (stop_to_atr >= config_.entry_validation_acceptable_stop_atr_min &&
               stop_to_atr <= config_.entry_validation_acceptable_stop_atr_max) {
        return 7.0;
    } else if (stop_to_atr < config_.entry_validation_acceptable_stop_atr_min) {
        return 2.0;
    }

    return 0.0;
}

} // namespace Core
} // namespace SDTrading
