#ifndef PERFORMANCE_TRACKER_H
#define PERFORMANCE_TRACKER_H

#include <vector>
#include <string>
#include "common_types.h"

namespace SDTrading {
namespace Backtest {

// Import types from Core namespace
using Core::Trade;

/**
 * @struct PerformanceMetrics
 * @brief Comprehensive backtest performance metrics
 */
struct PerformanceMetrics {
    // Capital metrics
    double starting_capital;
    double ending_capital;
    double total_pnl;
    double total_return_pct;
    
    // Trade statistics
    int total_trades;
    int winning_trades;
    int losing_trades;
    double win_rate_pct;
    
    // P&L metrics
    double avg_win;
    double avg_loss;
    double largest_win;
    double largest_loss;
    double profit_factor;
    double expectancy;
    
    // Risk metrics
    double max_drawdown;
    double max_drawdown_pct;
    int max_consecutive_wins;
    int max_consecutive_losses;
    
    // Quality metrics
    double avg_rr_ratio;
    double sharpe_ratio;
    
    // Time metrics
    std::string start_date;
    std::string end_date;
    int total_bars;
    
    PerformanceMetrics()
        : starting_capital(0), ending_capital(0), total_pnl(0), total_return_pct(0),
          total_trades(0), winning_trades(0), losing_trades(0), win_rate_pct(0),
          avg_win(0), avg_loss(0), largest_win(0), largest_loss(0),
          profit_factor(0), expectancy(0),
          max_drawdown(0), max_drawdown_pct(0),
          max_consecutive_wins(0), max_consecutive_losses(0),
          avg_rr_ratio(0), sharpe_ratio(0), total_bars(0) {}
};

/**
 * @class PerformanceTracker
 * @brief Tracks and calculates backtest performance metrics
 */
class PerformanceTracker {
private:
    std::vector<Trade> all_trades;
    double starting_capital;
    double peak_capital;
    double current_capital;
    
    /**
     * Calculate win rate
     * @return Win rate percentage
     */
    double calculate_win_rate() const;
    
    /**
     * Calculate average win
     * @return Average winning trade P&L
     */
    double calculate_avg_win() const;
    
    /**
     * Calculate average loss
     * @return Average losing trade P&L
     */
    double calculate_avg_loss() const;
    
    /**
     * Calculate profit factor
     * @return Profit factor (gross profit / gross loss)
     */
    double calculate_profit_factor() const;
    
    /**
     * Calculate expectancy
     * @return Expected value per trade
     */
    double calculate_expectancy() const;
    
    /**
     * Calculate maximum drawdown
     * @return Max drawdown in currency
     */
    double calculate_max_drawdown() const;
    
    /**
     * Calculate Sharpe ratio
     * @return Sharpe ratio
     */
    double calculate_sharpe_ratio() const;

public:
    /**
     * Constructor
     * @param initial_capital Starting capital
     */
    explicit PerformanceTracker(double initial_capital);
    
    /**
     * Add completed trade to tracker
     * @param trade Closed trade
     */
    void add_trade(const Trade& trade);
    
    /**
     * Update current capital
     * @param capital New capital value
     */
    void update_capital(double capital);
    
    /**
     * Calculate all performance metrics
     * @return Complete metrics structure
     */
    PerformanceMetrics calculate_metrics() const;
    
    /**
     * Get all trades
     * @return Vector of all trades
     */
    const std::vector<Trade>& get_trades() const { return all_trades; }
    
    /**
     * Get trade count
     * @return Number of trades
     */
    size_t get_trade_count() const { return all_trades.size(); }
    
    /**
     * Print summary to console
     */
    void print_summary() const;
};

} // namespace Backtest
} // namespace SDTrading

#endif // PERFORMANCE_TRACKER_H
