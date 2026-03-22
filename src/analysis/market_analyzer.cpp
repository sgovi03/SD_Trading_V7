#include "market_analyzer.h"
#include "../utils/logger.h"

namespace SDTrading {
namespace Core {

double MarketAnalyzer::calculate_true_range(const Bar& current, const Bar& previous) {
    double hl = current.high - current.low;
    double hc = std::abs(current.high - previous.close);
    double lc = std::abs(current.low - previous.close);
    
    return std::max({hl, hc, lc});
}

double MarketAnalyzer::calculate_atr(const std::vector<Bar>& bars, int period, int end_index) {
    if (bars.empty()) return 0.0;
    
    // Default to last bar
    if (end_index < 0 || end_index >= (int)bars.size()) {
        end_index = bars.size() - 1;
    }
    
    // Need at least period + 1 bars
    if (end_index < period) return 0.0;
    
    // Calculate initial ATR using SMA of TR
    double sum_tr = 0.0;
    for (int i = end_index - period + 1; i <= end_index; i++) {
        if (i > 0) {
            sum_tr += calculate_true_range(bars[i], bars[i-1]);
        }
    }
    
    double atr = sum_tr / period;
    return atr;
}

double MarketAnalyzer::calculate_ema(const std::vector<Bar>& bars, int period, int end_index) {
    if (bars.empty()) return 0.0;

    // Default to last bar
    if (end_index < 0 || end_index >= (int)bars.size()) {
        end_index = static_cast<int>(bars.size()) - 1;
    }

    // Need at least period bars
    if (end_index < period - 1) return 0.0;

    // Calculate multiplier
    double multiplier = 2.0 / (period + 1.0);

    // Seed with SMA for the first EMA value
    double sum = 0.0;
    int sma_start = end_index - period + 1;
    for (int i = sma_start; i <= sma_start + period - 1; ++i) {
        sum += bars[i].close;
    }
    double ema = sum / period;

    // Apply EMA recursively for remaining bars up to end_index
    for (int i = sma_start + period; i <= end_index; ++i) {
        ema = (bars[i].close - ema) * multiplier + ema;
    }

    return ema;
}

MarketRegime MarketAnalyzer::detect_regime(const std::vector<Bar>& bars,
                                           int lookback,
                                           double threshold,
                                           int end_index) {
    if (bars.empty()) return MarketRegime::RANGING;

    // ⭐ LOOKAHEAD FIX: Use caller-supplied end_index if valid; otherwise default
    // to bars.size()-1. LT passes -1 (default) because bar_history already ends at
    // the current bar. BT must pass bar_index explicitly — without this, end_index
    // would be bars.size()-1 (the last bar of the full Aug→Mar dataset), so regime
    // on a Feb-02 bar would be computed using Mar-2026 prices: pure lookahead bias.
    int eidx = (end_index >= 0 && end_index < static_cast<int>(bars.size()))
                   ? end_index
                   : static_cast<int>(bars.size()) - 1;

    int start_index = std::max(0, eidx - lookback + 1);

    if (start_index >= eidx) return MarketRegime::RANGING;

    double start_price   = bars[start_index].close;
    double current_price = bars[eidx].close;

    double pct_change = ((current_price - start_price) / start_price) * 100.0;

    if (pct_change >  threshold) return MarketRegime::BULL;
    if (pct_change < -threshold) return MarketRegime::BEAR;
    return MarketRegime::RANGING;
}

MarketRegime MarketAnalyzer::analyze_htf_trend(const std::vector<Bar>& bars,
                                               int htf_period,
                                               double threshold) {
    // HTF trend is just regime detection over longer period
    return detect_regime(bars, htf_period, threshold);
}

// ============================================================
// RSI CALCULATION
// ============================================================
double MarketAnalyzer::calculate_rsi(const std::vector<Bar>& bars, int period, int end_index) {
    if (bars.empty()) return 50.0;  // Neutral RSI
    
    // Default to last bar
    if (end_index < 0 || end_index >= (int)bars.size()) {
        end_index = bars.size() - 1;
    }
    
    // Need at least period + 1 bars for RSI
    if (end_index < period) return 50.0;
    
    // Calculate initial average gain and loss
    double avg_gain = 0.0;
    double avg_loss = 0.0;
    
    for (int i = end_index - period + 1; i <= end_index; i++) {
        double change = bars[i].close - bars[i-1].close;
        if (change > 0) {
            avg_gain += change;
        } else {
            avg_loss += std::abs(change);
        }
    }
    
    avg_gain /= period;
    avg_loss /= period;
    
    // Avoid division by zero
    if (avg_loss < 0.0001) {
        return 100.0;  // No losses, RSI = 100
    }
    
    double rs = avg_gain / avg_loss;
    double rsi = 100.0 - (100.0 / (1.0 + rs));
    
    return rsi;
}

// ============================================================
// BOLLINGER BANDS CALCULATION
// ============================================================
MarketAnalyzer::BollingerBands MarketAnalyzer::calculate_bollinger_bands(
    const std::vector<Bar>& bars, 
    int period, 
    double std_dev,
    int end_index) {
    
    BollingerBands bb;
    
    if (bars.empty()) return bb;
    
    // Default to last bar
    if (end_index < 0 || end_index >= (int)bars.size()) {
        end_index = bars.size() - 1;
    }
    
    // Need at least period bars
    if (end_index < period - 1) return bb;
    
    // Calculate SMA (middle band)
    double sum = 0.0;
    int start_index = end_index - period + 1;
    for (int i = start_index; i <= end_index; i++) {
        sum += bars[i].close;
    }
    bb.middle = sum / period;
    
    // Calculate standard deviation
    double variance_sum = 0.0;
    for (int i = start_index; i <= end_index; i++) {
        double diff = bars[i].close - bb.middle;
        variance_sum += diff * diff;
    }
    double std_deviation = std::sqrt(variance_sum / period);
    
    // Calculate upper and lower bands
    bb.upper = bb.middle + (std_dev * std_deviation);
    bb.lower = bb.middle - (std_dev * std_deviation);
    
    // Calculate bandwidth
    bb.bandwidth = ((bb.upper - bb.lower) / bb.middle) * 100.0;
    
    return bb;
}

// ============================================================
// ADX CALCULATION
// ============================================================
MarketAnalyzer::ADXValues MarketAnalyzer::calculate_adx(
    const std::vector<Bar>& bars, 
    int period, 
    int end_index) {
    
    ADXValues adx_vals;
    
    if (bars.empty()) return adx_vals;
    
    // Default to last bar
    if (end_index < 0 || end_index >= (int)bars.size()) {
        end_index = bars.size() - 1;
    }
    
    // Need at least period * 2 bars for ADX
    if (end_index < period * 2) return adx_vals;
    
    // Calculate +DM and -DM for the period
    std::vector<double> plus_dm(period, 0.0);
    std::vector<double> minus_dm(period, 0.0);
    std::vector<double> tr(period, 0.0);
    
    int start_index = end_index - period + 1;
    for (int i = 0; i < period; i++) {
        int bar_idx = start_index + i;
        if (bar_idx <= 0) continue;
        
        double high_diff = bars[bar_idx].high - bars[bar_idx-1].high;
        double low_diff = bars[bar_idx-1].low - bars[bar_idx].low;
        
        if (high_diff > low_diff && high_diff > 0) {
            plus_dm[i] = high_diff;
        }
        if (low_diff > high_diff && low_diff > 0) {
            minus_dm[i] = low_diff;
        }
        
        tr[i] = calculate_true_range(bars[bar_idx], bars[bar_idx-1]);
    }
    
    // Calculate smoothed +DI and -DI
    double sum_plus_dm = 0.0, sum_minus_dm = 0.0, sum_tr = 0.0;
    for (int i = 0; i < period; i++) {
        sum_plus_dm += plus_dm[i];
        sum_minus_dm += minus_dm[i];
        sum_tr += tr[i];
    }
    
    if (sum_tr < 0.0001) return adx_vals;
    
    adx_vals.plus_di = (sum_plus_dm / sum_tr) * 100.0;
    adx_vals.minus_di = (sum_minus_dm / sum_tr) * 100.0;
    
    // Calculate DX
    double di_diff = std::abs(adx_vals.plus_di - adx_vals.minus_di);
    double di_sum = adx_vals.plus_di + adx_vals.minus_di;
    
    if (di_sum < 0.0001) return adx_vals;
    
    double dx = (di_diff / di_sum) * 100.0;
    
    // ADX is smoothed DX (simplified - using current DX as ADX)
    adx_vals.adx = dx;
    
    return adx_vals;
}

// ============================================================
// MACD CALCULATION
// ============================================================
MarketAnalyzer::MACDValues MarketAnalyzer::calculate_macd(
    const std::vector<Bar>& bars,
    int fast_period,
    int slow_period,
    int signal_period,
    int end_index) {
    
    MACDValues macd_vals;
    
    if (bars.empty()) return macd_vals;
    
    // Default to last bar
    if (end_index < 0 || end_index >= (int)bars.size()) {
        end_index = bars.size() - 1;
    }
    
    // Need at least slow_period + signal_period bars
    if (end_index < slow_period + signal_period) return macd_vals;
    
    // Calculate fast and slow EMAs
    double fast_ema = 0.0, slow_ema = 0.0;
    
    // Fast EMA calculation
    double fast_multiplier = 2.0 / (fast_period + 1.0);
    double fast_sum = 0.0;
    int fast_start = end_index - fast_period + 1;
    for (int i = fast_start; i <= end_index; i++) {
        fast_sum += bars[i].close;
    }
    fast_ema = fast_sum / fast_period;
    
    // Slow EMA calculation
    double slow_multiplier = 2.0 / (slow_period + 1.0);
    double slow_sum = 0.0;
    int slow_start = end_index - slow_period + 1;
    for (int i = slow_start; i <= end_index; i++) {
        slow_sum += bars[i].close;
    }
    slow_ema = slow_sum / slow_period;
    
    // MACD Line = Fast EMA - Slow EMA
    macd_vals.macd_line = fast_ema - slow_ema;
    
    // Signal Line (EMA of MACD Line)
    std::vector<double> macd_line_series;
    int macd_start = end_index - signal_period + 1;
    if (macd_start < slow_start) macd_start = slow_start;
    for (int i = macd_start; i <= end_index; ++i) {
        double fast_ema = 0.0, slow_ema = 0.0;
        // Fast EMA
        double fast_sum = 0.0;
        int fast_start = i - fast_period + 1;
        if (fast_start < 0) fast_start = 0;
        for (int j = fast_start; j <= i; ++j) fast_sum += bars[j].close;
        fast_ema = fast_sum / fast_period;
        for (int j = fast_start + fast_period; j <= i; ++j)
            fast_ema = (bars[j].close - fast_ema) * fast_multiplier + fast_ema;
        // Slow EMA
        double slow_sum = 0.0;
        int slow_start_inner = i - slow_period + 1;
        if (slow_start_inner < 0) slow_start_inner = 0;
        for (int j = slow_start_inner; j <= i; ++j) slow_sum += bars[j].close;
        slow_ema = slow_sum / slow_period;
        for (int j = slow_start_inner + slow_period; j <= i; ++j)
            slow_ema = (bars[j].close - slow_ema) * slow_multiplier + slow_ema;
        macd_line_series.push_back(fast_ema - slow_ema);
    }
    // Calculate EMA of MACD line series for signal line
    double signal_multiplier = 2.0 / (signal_period + 1.0);
    double signal_ema = 0.0;
    if ((int)macd_line_series.size() >= signal_period) {
        double signal_sum = 0.0;
        for (int i = 0; i < signal_period; ++i) signal_sum += macd_line_series[i];
        signal_ema = signal_sum / signal_period;
        for (int i = signal_period; i < (int)macd_line_series.size(); ++i)
            signal_ema = (macd_line_series[i] - signal_ema) * signal_multiplier + signal_ema;
    } else if (!macd_line_series.empty()) {
        // Fallback: SMA if not enough values
        double signal_sum = 0.0;
        for (double v : macd_line_series) signal_sum += v;
        signal_ema = signal_sum / macd_line_series.size();
    }
    macd_vals.signal_line = signal_ema;

    // Histogram = MACD Line - Signal Line
    macd_vals.histogram = macd_vals.macd_line - macd_vals.signal_line;

    return macd_vals;
}

} // namespace Core
} // namespace SDTrading