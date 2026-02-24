#include "csv_reporter.h"
#include "../utils/logger.h"
#include <iostream>
#include <iomanip>

namespace SDTrading {
namespace Backtest {

CSVReporter::CSVReporter(const std::string& output_directory)
    : output_dir(output_directory) {
    LOG_INFO("CSVReporter initialized, output dir: " + output_dir);
}

std::string CSVReporter::sanitize_csv(const std::string& str) const {
    std::string result = str;
    
    // Escape quotes
    size_t pos = 0;
    while ((pos = result.find('"', pos)) != std::string::npos) {
        result.replace(pos, 1, "\"\"");
        pos += 2;
    }
    
    // Wrap in quotes if contains comma
    if (result.find(',') != std::string::npos) {
        result = "\"" + result + "\"";
    }
    
    return result;
}

void CSVReporter::write_trade_header(std::ofstream& file) const {
        file << "Trade#,Direction,Entry Date,Exit Date,Entry Price,Exit Price," 
            << "Stop Loss,Take Profit,Position Size,Risk Amount,Reward Amount," 
            << "P&L,Return %,Exit Reason,Zone ID,Zone Score,Base Strength,Elite Bonus," 
            << "Swing Score,Regime Score,State Freshness,Rejection Score," 
            << "Recommended RR,Score Rationale,Aggressiveness," 
            << "Regime,Zone Formation,Zone Distal,Zone Proximal," 
            << "Swing Percentile,At Swing Extreme," 
            << "Fast EMA,Slow EMA,RSI,BB Upper,BB Middle,BB Lower,BB Bandwidth," 
            << "ADX,+DI,-DI,MACD Line,MACD Signal,MACD Histogram," 
            << "Zone Departure Vol Ratio,Zone Initiative,Zone Vol Climax,Zone Vol Score,Zone Institutional Index," 
            << "Entry Pullback Vol Ratio,Entry Volume Score,Entry Volume Pattern,Entry Position Lots,Position Size Reason," 
            << "Exit Volume Ratio,Exit Was Vol Climax\n";
}

void CSVReporter::write_trade_row(std::ofstream& file, const Trade& trade) const {
        file << trade.trade_num << "," 
            << sanitize_csv(trade.direction) << "," 
            << sanitize_csv(trade.entry_date) << "," 
            << sanitize_csv(trade.exit_date) << "," 
            << std::fixed << std::setprecision(2) << trade.entry_price << "," 
            << trade.exit_price << "," 
            << trade.stop_loss << "," 
            << trade.take_profit << "," 
            << trade.position_size << "," 
            << trade.risk_amount << "," 
            << trade.reward_amount << "," 
            << trade.pnl << "," 
            << std::setprecision(2) << trade.return_pct << "," 
            << sanitize_csv(trade.exit_reason) << "," 
            << trade.zone_id << "," 
            << std::setprecision(2) << trade.zone_score << "," 
            << trade.score_base_strength << "," 
            << trade.score_elite_bonus << "," 
            << trade.score_swing_position << "," 
            << trade.score_regime_alignment << "," 
            << trade.score_state_freshness << "," 
            << trade.score_rejection_confirmation << "," 
            << trade.score_recommended_rr << "," 
            << sanitize_csv(trade.score_rationale) << "," 
            << trade.entry_aggressiveness << "," 
            << sanitize_csv(trade.regime) << "," 
            << sanitize_csv(trade.zone_formation_time) << "," 
            << std::setprecision(2) << trade.zone_distal << "," 
            << trade.zone_proximal << "," 
            << trade.swing_percentile << "," 
            << (trade.is_at_swing_extreme ? "YES" : "NO") << "," 
            << std::setprecision(2) << trade.fast_ema << "," 
            << trade.slow_ema << "," 
            << trade.rsi << "," 
            << trade.bb_upper << "," 
            << trade.bb_middle << "," 
            << trade.bb_lower << "," 
            << trade.bb_bandwidth << "," 
            << trade.adx << "," 
            << trade.plus_di << "," 
            << trade.minus_di << "," 
            << trade.macd_line << "," 
            << trade.macd_signal << "," 
            << trade.macd_histogram << "," 
            << trade.zone_departure_volume_ratio << "," 
            << (trade.zone_is_initiative ? "YES" : "NO") << "," 
            << (trade.zone_has_volume_climax ? "YES" : "NO") << "," 
            << trade.zone_volume_score << "," 
            << trade.zone_institutional_index << "," 
            << trade.entry_pullback_vol_ratio << "," 
            << trade.entry_volume_score << "," 
            << sanitize_csv(trade.entry_volume_pattern) << "," 
            << trade.entry_position_lots << "," 
            << sanitize_csv(trade.position_size_reason) << "," 
            << trade.exit_volume_ratio << "," 
            << (trade.exit_was_volume_climax ? "YES" : "NO") << "\n";
}

bool CSVReporter::export_trades(const std::vector<Trade>& trades,
                                const std::string& filename) {
    std::string filepath = output_dir + "/" + filename;
    std::ofstream file(filepath);
    
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: " + filepath);
        return false;
    }
    
    write_trade_header(file);
    
    for (const auto& trade : trades) {
        write_trade_row(file, trade);
    }
    
    file.close();
    
    LOG_INFO("Exported " + std::to_string(trades.size()) + " trades to " + filepath);
    return true;
}

bool CSVReporter::export_metrics(const PerformanceMetrics& metrics,
                                 const std::string& filename) {
    std::string filepath = output_dir + "/" + filename;
    std::ofstream file(filepath);
    
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: " + filepath);
        return false;
    }
    
    // Write as vertical key-value pairs
    file << "Metric,Value\n";
    file << std::fixed << std::setprecision(2);
    
    file << "Starting Capital," << metrics.starting_capital << "\n";
    file << "Ending Capital," << metrics.ending_capital << "\n";
    file << "Total P&L," << metrics.total_pnl << "\n";
    file << "Total Return %," << metrics.total_return_pct << "\n";
    file << "Total Trades," << metrics.total_trades << "\n";
    file << "Winning Trades," << metrics.winning_trades << "\n";
    file << "Losing Trades," << metrics.losing_trades << "\n";
    file << "Win Rate %," << metrics.win_rate_pct << "\n";
    file << "Average Win," << metrics.avg_win << "\n";
    file << "Average Loss," << metrics.avg_loss << "\n";
    file << "Largest Win," << metrics.largest_win << "\n";
    file << "Largest Loss," << metrics.largest_loss << "\n";
    file << "Profit Factor," << metrics.profit_factor << "\n";
    file << "Expectancy," << metrics.expectancy << "\n";
    file << "Max Drawdown," << metrics.max_drawdown << "\n";
    file << "Max Drawdown %," << metrics.max_drawdown_pct << "\n";
    file << "Max Consecutive Wins," << metrics.max_consecutive_wins << "\n";
    file << "Max Consecutive Losses," << metrics.max_consecutive_losses << "\n";
    file << "Avg RR Ratio," << std::setprecision(2) << metrics.avg_rr_ratio << "\n";
    file << "Sharpe Ratio," << std::setprecision(3) << metrics.sharpe_ratio << "\n";
    file << "Start Date," << sanitize_csv(metrics.start_date) << "\n";
    file << "End Date," << sanitize_csv(metrics.end_date) << "\n";
    file << "Total Bars," << metrics.total_bars << "\n";
    
    file.close();
    
    LOG_INFO("Exported metrics to " + filepath);
    return true;
}

bool CSVReporter::export_equity_curve(const std::vector<Trade>& trades,
                                     double starting_capital,
                                     const std::string& filename) {
    std::string filepath = output_dir + "/" + filename;
    std::ofstream file(filepath);
    
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: " + filepath);
        return false;
    }
    
    file << "Trade#,Date,Capital,Cumulative P&L,Cumulative Return %\n";
    file << std::fixed << std::setprecision(2);
    
    // Starting point
    file << "0,Start," << starting_capital << ",0.00,0.00\n";
    
    double current_capital = starting_capital;
    double cumulative_pnl = 0.0;
    
    for (size_t i = 0; i < trades.size(); i++) {
        const Trade& trade = trades[i];
        current_capital += trade.pnl;
        cumulative_pnl += trade.pnl;
        double cumulative_return = ((current_capital - starting_capital) / starting_capital) * 100.0;
        
        file << (i + 1) << ","
             << sanitize_csv(trade.exit_date) << ","
             << current_capital << ","
             << cumulative_pnl << ","
             << cumulative_return << "\n";
    }
    
    file.close();
    
    LOG_INFO("Exported equity curve to " + filepath);
    return true;
}

} // namespace Backtest
} // namespace SDTrading
