#ifndef CSV_REPORTER_H
#define CSV_REPORTER_H

#include <string>
#include <vector>
#include <fstream>
#include "common_types.h"
#include "performance_tracker.h"

namespace SDTrading {
namespace Backtest {

// Import types from Core namespace
using Core::Trade;

/**
 * @class CSVReporter
 * @brief Exports backtest results to CSV files
 * 
 * Generates:
 * - Trade log CSV (all trades with details)
 * - Summary metrics CSV (performance stats)
 * - Equity curve CSV (capital over time)
 */
class CSVReporter {
private:
    std::string output_dir;
    
    /**
     * Write trade log header
     * @param file Output file stream
     */
    void write_trade_header(std::ofstream& file) const;
    
    /**
     * Write single trade row
     * @param file Output file stream
     * @param trade Trade to write
     */
    void write_trade_row(std::ofstream& file, const Trade& trade) const;
    
    /**
     * Sanitize string for CSV (escape commas, quotes)
     * @param str String to sanitize
     * @return Sanitized string
     */
    std::string sanitize_csv(const std::string& str) const;

public:
    /**
     * Constructor
     * @param output_directory Directory for output files
     */
    explicit CSVReporter(const std::string& output_directory = ".");
    
    /**
     * Export all trades to CSV
     * @param trades Vector of trades
     * @param filename Output filename
     * @return true if successful
     */
    bool export_trades(const std::vector<Trade>& trades, 
                      const std::string& filename = "trades.csv");
    
    /**
     * Export performance metrics to CSV
     * @param metrics Performance metrics
     * @param filename Output filename
     * @return true if successful
     */
    bool export_metrics(const PerformanceMetrics& metrics,
                       const std::string& filename = "metrics.csv");
    
    /**
     * Export equity curve to CSV
     * @param trades Vector of trades
     * @param starting_capital Initial capital
     * @param filename Output filename
     * @return true if successful
     */
    bool export_equity_curve(const std::vector<Trade>& trades,
                            double starting_capital,
                            const std::string& filename = "equity_curve.csv");
    
    /**
     * Set output directory
     * @param dir Directory path
     */
    void set_output_directory(const std::string& dir) { output_dir = dir; }
};

} // namespace Backtest
} // namespace SDTrading

#endif // CSV_REPORTER_H
