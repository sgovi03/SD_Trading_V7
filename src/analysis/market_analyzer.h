#ifndef MARKET_ANALYZER_H
#define MARKET_ANALYZER_H

#include <vector>
#include <algorithm>
#include <cmath>
#include "common_types.h"

namespace SDTrading {
namespace Core {

/**
 * @class MarketAnalyzer
 * @brief Technical analysis utilities for market data
 * 
 * Provides static methods for calculating technical indicators:
 * - ATR (Average True Range)
 * - EMA (Exponential Moving Average)
 * - HTF Trend Analysis
 * - Market Regime Detection
 */
class MarketAnalyzer {
public:
    /**
     * Calculate Average True Range
     * @param bars Bar data
     * @param period ATR period
     * @param end_index Index to calculate ATR at (default: last bar)
     * @return ATR value
     */
    static double calculate_atr(const std::vector<Bar>& bars, int period, int end_index = -1);

    /**
     * Calculate Exponential Moving Average
     * @param bars Bar data
     * @param period EMA period
     * @param end_index Index to calculate EMA at (default: last bar)
     * @return EMA value
     */
    static double calculate_ema(const std::vector<Bar>& bars, int period, int end_index = -1);

    /**
     * Detect market regime (BULL, BEAR, RANGING)
     * @param bars Bar data
     * @param lookback Number of bars to analyze
     * @param threshold Percentage threshold for regime detection
     * @return Market regime
     */
    // ⭐ LOOKAHEAD FIX: end_index pins the "current" bar so BT callers can pass
    // bar_index instead of bars.size()-1 (which is a future bar in BT's full dataset).
    // Default -1 preserves the old behaviour for LT callers that already use bar_history
    // up to the current bar, so bars.size()-1 IS the current bar there.
    static MarketRegime detect_regime(const std::vector<Bar>& bars,
                                      int lookback = 50,
                                      double threshold = 5.0,
                                      int end_index = -1);

    /**
     * Analyze Higher Time Frame trend
     * @param bars Bar data
     * @param htf_period HTF period in bars
     * @param threshold Trend threshold percentage
     * @return HTF trend direction (BULL, BEAR, RANGING)
     */
    static MarketRegime analyze_htf_trend(const std::vector<Bar>& bars,
                                         int htf_period = 100,
                                         double threshold = 3.0);

    /**
     * Calculate True Range for a single bar
     * @param current Current bar
     * @param previous Previous bar
     * @return True range value
     */
    static double calculate_true_range(const Bar& current, const Bar& previous);

    /**
     * Calculate RSI (Relative Strength Index)
     * @param bars Bar data
     * @param period RSI period (default 14)
     * @param end_index Index to calculate RSI at (default: last bar)
     * @return RSI value (0-100)
     */
    static double calculate_rsi(const std::vector<Bar>& bars, int period = 14, int end_index = -1);

    /**
     * Structure to hold Bollinger Bands values
     */
    struct BollingerBands {
        double upper;
        double middle;
        double lower;
        double bandwidth;
        
        BollingerBands() : upper(0), middle(0), lower(0), bandwidth(0) {}
    };

    /**
     * Calculate Bollinger Bands
     * @param bars Bar data
     * @param period BB period (default 20)
     * @param std_dev Standard deviations (default 2.0)
     * @param end_index Index to calculate BB at (default: last bar)
     * @return BollingerBands structure
     */
    static BollingerBands calculate_bollinger_bands(const std::vector<Bar>& bars, 
                                                     int period = 20, 
                                                     double std_dev = 2.0,
                                                     int end_index = -1);

    /**
     * Structure to hold ADX values
     */
    struct ADXValues {
        double adx;
        double plus_di;
        double minus_di;
        
        ADXValues() : adx(0), plus_di(0), minus_di(0) {}
    };

    /**
     * Calculate ADX (Average Directional Index) with +DI and -DI
     * @param bars Bar data
     * @param period ADX period (default 14)
     * @param end_index Index to calculate ADX at (default: last bar)
     * @return ADXValues structure
     */
    static ADXValues calculate_adx(const std::vector<Bar>& bars, 
                                   int period = 14, 
                                   int end_index = -1);

    /**
     * Structure to hold MACD values
     */
    struct MACDValues {
        double macd_line;
        double signal_line;
        double histogram;
        
        MACDValues() : macd_line(0), signal_line(0), histogram(0) {}
    };

    /**
     * Calculate MACD (Moving Average Convergence Divergence)
     * @param bars Bar data
     * @param fast_period Fast EMA period (default 12)
     * @param slow_period Slow EMA period (default 26)
     * @param signal_period Signal line period (default 9)
     * @param end_index Index to calculate MACD at (default: last bar)
     * @return MACDValues structure
     */
    static MACDValues calculate_macd(const std::vector<Bar>& bars,
                                     int fast_period = 12,
                                     int slow_period = 26,
                                     int signal_period = 9,
                                     int end_index = -1);

private:
    // No instantiation - all static methods
    MarketAnalyzer() = delete;
};

} // namespace Core
} // namespace SDTrading

#endif // MARKET_ANALYZER_H