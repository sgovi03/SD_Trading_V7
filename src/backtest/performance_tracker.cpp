#include "performance_tracker.h"
#include "../utils/logger.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <sstream>

namespace SDTrading {
namespace Backtest {

PerformanceTracker::PerformanceTracker(double initial_capital)
    : starting_capital(initial_capital),
      peak_capital(initial_capital),
      current_capital(initial_capital) {
    LOG_INFO("PerformanceTracker initialized with capital: $" + std::to_string(initial_capital));
}

void PerformanceTracker::add_trade(const Trade& trade) {
    all_trades.push_back(trade);
    LOG_DEBUG("Trade #" + std::to_string(all_trades.size()) + " recorded");
}

void PerformanceTracker::update_capital(double capital) {
    current_capital = capital;
    if (capital > peak_capital) {
        peak_capital = capital;
    }
}

double PerformanceTracker::calculate_win_rate() const {
    if (all_trades.empty()) return 0.0;
    
    int wins = 0;
    for (const auto& trade : all_trades) {
        if (trade.pnl > 0) wins++;
    }
    
    return (static_cast<double>(wins) / all_trades.size()) * 100.0;
}

double PerformanceTracker::calculate_avg_win() const {
    double total_wins = 0.0;
    int win_count = 0;
    
    for (const auto& trade : all_trades) {
        if (trade.pnl > 0) {
            total_wins += trade.pnl;
            win_count++;
        }
    }
    
    return (win_count > 0) ? (total_wins / win_count) : 0.0;
}

double PerformanceTracker::calculate_avg_loss() const {
    double total_losses = 0.0;
    int loss_count = 0;
    
    for (const auto& trade : all_trades) {
        if (trade.pnl < 0) {
            total_losses += std::abs(trade.pnl);
            loss_count++;
        }
    }
    
    return (loss_count > 0) ? (total_losses / loss_count) : 0.0;
}

double PerformanceTracker::calculate_profit_factor() const {
    double gross_profit = 0.0;
    double gross_loss = 0.0;
    
    for (const auto& trade : all_trades) {
        if (trade.pnl > 0) {
            gross_profit += trade.pnl;
        } else {
            gross_loss += std::abs(trade.pnl);
        }
    }
    
    return (gross_loss > 0.0001) ? (gross_profit / gross_loss) : 0.0;
}

double PerformanceTracker::calculate_expectancy() const {
    if (all_trades.empty()) return 0.0;
    
    double total_pnl = 0.0;
    for (const auto& trade : all_trades) {
        total_pnl += trade.pnl;
    }
    
    return total_pnl / all_trades.size();
}

double PerformanceTracker::calculate_max_drawdown() const {
    double max_dd = 0.0;
    double peak = starting_capital;
    double current = starting_capital;
    
    for (const auto& trade : all_trades) {
        current += trade.pnl;
        
        if (current > peak) {
            peak = current;
        }
        
        double drawdown = peak - current;
        if (drawdown > max_dd) {
            max_dd = drawdown;
        }
    }
    
    return max_dd;
}

double PerformanceTracker::calculate_sharpe_ratio() const {
    if (all_trades.size() < 2) return 0.0;
    
    // Calculate returns
    std::vector<double> returns;
    for (const auto& trade : all_trades) {
        double return_pct = trade.return_pct / 100.0;  // Convert to decimal
        returns.push_back(return_pct);
    }
    
    // Calculate mean return
    double mean_return = 0.0;
    for (double r : returns) {
        mean_return += r;
    }
    mean_return /= returns.size();
    
    // Calculate standard deviation
    double variance = 0.0;
    for (double r : returns) {
        variance += (r - mean_return) * (r - mean_return);
    }
    variance /= (returns.size() - 1);
    double std_dev = std::sqrt(variance);
    
    // Per-trade Sharpe ratio (assuming 0 risk-free rate)
    double sharpe_per_trade = (std_dev > 0.00001) ? (mean_return / std_dev) : 0.0;

    // Annualize the Sharpe Ratio
    if (all_trades.empty()) return 0.0;

    std::string start_date = all_trades.front().entry_date;
    std::string end_date = all_trades.back().exit_date;

    std::tm tm_start = {};
    std::tm tm_end = {};
    std::istringstream ss_start(start_date);
    std::istringstream ss_end(end_date);
    
    // Format: "YYYY-MM-DD HH:MM:SS"
    ss_start >> std::get_time(&tm_start, "%Y-%m-%d %H:%M:%S");
    ss_end >> std::get_time(&tm_end, "%Y-%m-%d %H:%M:%S");
    
    if (ss_start.fail() || ss_end.fail()) {
        // Fallback if parsing fails: assume 252 trades per year (standard daily)
        return sharpe_per_trade * std::sqrt(252.0);
    }

    std::time_t time_start = std::mktime(&tm_start);
    std::time_t time_end = std::mktime(&tm_end);
    
    double seconds = std::difftime(time_end, time_start);
    double years = seconds / (365.25 * 24 * 3600);
    
    if (years < 0.01) years = 0.01; // Avoid division by zero or tiny duration

    double trades_per_year = all_trades.size() / years;
    
    return sharpe_per_trade * std::sqrt(trades_per_year);
}

PerformanceMetrics PerformanceTracker::calculate_metrics() const {
    PerformanceMetrics metrics;
    
    // Capital metrics
    metrics.starting_capital = starting_capital;
    metrics.ending_capital = current_capital;
    metrics.total_pnl = current_capital - starting_capital;
    metrics.total_return_pct = ((current_capital - starting_capital) / starting_capital) * 100.0;
    
    if (all_trades.empty()) {
        return metrics;
    }
    
    // Trade statistics
    metrics.total_trades = static_cast<int>(all_trades.size());
    metrics.winning_trades = 0;
    metrics.losing_trades = 0;
    
    for (const auto& trade : all_trades) {
        if (trade.pnl > 0) metrics.winning_trades++;
        else if (trade.pnl < 0) metrics.losing_trades++;
    }
    
    metrics.win_rate_pct = calculate_win_rate();
    
    // P&L metrics
    metrics.avg_win = calculate_avg_win();
    metrics.avg_loss = calculate_avg_loss();
    
    metrics.largest_win = 0.0;
    metrics.largest_loss = 0.0;
    
    for (const auto& trade : all_trades) {
        if (trade.pnl > metrics.largest_win) {
            metrics.largest_win = trade.pnl;
        }
        if (trade.pnl < metrics.largest_loss) {
            metrics.largest_loss = trade.pnl;
        }
    }
    
    metrics.profit_factor = calculate_profit_factor();
    metrics.expectancy = calculate_expectancy();
    metrics.avg_rr_ratio = (metrics.avg_loss > 0.0001) ? (metrics.avg_win / metrics.avg_loss) : 0.0;
    
    // Risk metrics - Recalculate drawdown with peak tracking for percentage
    double max_dd = 0.0;
    double peak = starting_capital;
    double current = starting_capital;
    double peak_at_max_dd = starting_capital;
    
    for (const auto& trade : all_trades) {
        current += trade.pnl;
        
        if (current > peak) {
            peak = current;
        }
        
        double drawdown = peak - current;
        if (drawdown > max_dd) {
            max_dd = drawdown;
            peak_at_max_dd = peak;
        }
    }
    
    metrics.max_drawdown = max_dd;
    // Correct: DD% should be based on peak capital, not starting capital
    metrics.max_drawdown_pct = (peak_at_max_dd > 0) ? 
        ((max_dd / peak_at_max_dd) * 100.0) : 0.0;
    
    // Calculate max consecutive wins/losses
    int current_wins = 0;
    int current_losses = 0;
    metrics.max_consecutive_wins = 0;
    metrics.max_consecutive_losses = 0;
    
    for (const auto& trade : all_trades) {
        if (trade.pnl > 0) {
            current_wins++;
            current_losses = 0;
            metrics.max_consecutive_wins = std::max(metrics.max_consecutive_wins, current_wins);
        } else if (trade.pnl < 0) {
            current_losses++;
            current_wins = 0;
            metrics.max_consecutive_losses = std::max(metrics.max_consecutive_losses, current_losses);
        }
    }
    
    // Quality metrics
    metrics.sharpe_ratio = calculate_sharpe_ratio();
    
    // Time metrics
    if (!all_trades.empty()) {
        metrics.start_date = all_trades.front().entry_date;
        metrics.end_date = all_trades.back().exit_date;
    }
    
    return metrics;
}

void PerformanceTracker::print_summary() const {
    PerformanceMetrics metrics = calculate_metrics();
    
    std::cout << "\n==================================================" << std::endl;
    std::cout << "  BACKTEST PERFORMANCE SUMMARY" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    std::cout << std::fixed << std::setprecision(2);
    
    std::cout << "\nCAPITAL:" << std::endl;
    std::cout << "  Starting:        $" << metrics.starting_capital << std::endl;
    std::cout << "  Ending:          $" << metrics.ending_capital << std::endl;
    std::cout << "  Total P&L:       $" << metrics.total_pnl << std::endl;
    std::cout << "  Return:          " << metrics.total_return_pct << "%" << std::endl;
    
    std::cout << "\nTRADES:" << std::endl;
    std::cout << "  Total:           " << metrics.total_trades << std::endl;
    std::cout << "  Winners:         " << metrics.winning_trades << std::endl;
    std::cout << "  Losers:          " << metrics.losing_trades << std::endl;
    std::cout << "  Win Rate:        " << metrics.win_rate_pct << "%" << std::endl;
    
    std::cout << "\nP&L METRICS:" << std::endl;
    std::cout << "  Avg Win:         $" << metrics.avg_win << std::endl;
    std::cout << "  Avg Loss:        $" << metrics.avg_loss << std::endl;
    std::cout << "  Largest Win:     $" << metrics.largest_win << std::endl;
    std::cout << "  Largest Loss:    $" << metrics.largest_loss << std::endl;
    std::cout << "  Profit Factor:   " << metrics.profit_factor << std::endl;
    std::cout << "  Expectancy:      $" << metrics.expectancy << std::endl;
    
    std::cout << "\nRISK METRICS:" << std::endl;
    std::cout << "  Max Drawdown:    $" << metrics.max_drawdown << std::endl;
    std::cout << "  Max DD %:        " << metrics.max_drawdown_pct << "%" << std::endl;
    std::cout << "  Max Cons Wins:   " << metrics.max_consecutive_wins << std::endl;
    std::cout << "  Max Cons Losses: " << metrics.max_consecutive_losses << std::endl;
    
    std::cout << "\nQUALITY:" << std::endl;
    std::cout << "  Avg RR Ratio:    " << std::setprecision(2) << metrics.avg_rr_ratio << ":1" << std::endl;
    std::cout << "  Sharpe Ratio:    " << std::setprecision(3) << metrics.sharpe_ratio << std::endl;
    
    std::cout << "\n==================================================" << std::endl;
}

} // namespace Backtest
} // namespace SDTrading
