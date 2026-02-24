// ============================================================
// ITRADING ENGINE - ABSTRACT INTERFACE
// Unified interface for Backtest and Live trading engines
// ============================================================

#ifndef ITRADING_ENGINE_H
#define ITRADING_ENGINE_H

#include <string>

namespace SDTrading {
namespace Core {

/**
 * @interface ITradingEngine
 * @brief Abstract interface for trading engines (backtest/live)
 * 
 * Defines common contract for all trading engine implementations.
 * Enables factory pattern and polymorphic engine usage.
 */
class ITradingEngine {
public:
    virtual ~ITradingEngine() = default;
    
    /**
     * Initialize engine and load any required state
     * - Backtest: Load CSV data, start fresh zones
     * - Live: Connect to broker, load persisted zones
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;
    
    /**
     * Run the trading engine
     * - Backtest: Process all historical data
     * - Live: Run until stopped or duration expires
     * @param duration_minutes Run duration (0 = until stopped/complete)
     */
    virtual void run(int duration_minutes = 0) = 0;
    
    /**
     * Stop engine and cleanup
     * - Backtest: Export results
     * - Live: Close connections, save zones
     */
    virtual void stop() = 0;
    
    /**
     * Get engine type identifier
     * @return "backtest" or "live"
     */
    virtual std::string get_engine_type() const = 0;
};

} // namespace Core
} // namespace SDTrading

#endif // ITRADING_ENGINE_H
