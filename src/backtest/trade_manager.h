#ifndef TRADE_MANAGER_H
#define TRADE_MANAGER_H

#include <string>
#include "common_types.h"

// Forward declaration for live trading
namespace SDTrading {
namespace Live {
    class BrokerInterface;
    struct OrderResponse;
}

// Forward declarations for V6.0
namespace Utils {
    class VolumeBaseline;
}

namespace Core {
    class OIScorer;
}
}

namespace SDTrading {
namespace Backtest {

using Core::Trade;
using Core::Config;
using Core::Zone;
using Core::Bar;
using Core::EntryDecision;
using Core::MarketRegime;

/**
 * @enum ExecutionMode
 * @brief Defines whether TradeManager operates in backtest or live mode
 */
enum class ExecutionMode {
    BACKTEST,  // Simulated execution
    LIVE       // Real broker execution
};

/**
 * @class TradeManager
 * @brief UNIFIED trade management for both backtest and live trading
 * 
 * ⭐ CRITICAL SHARED MODULE ⭐
 * This module ensures IDENTICAL trade management logic in both:
 * - Backtest engine (historical simulation)
 * - Live engine (real-time trading)
 * 
 * SHARED LOGIC (Same for both modes):
 * - Stop loss monitoring
 * - Take profit monitoring
 * - P&L calculation
 * - Position state tracking
 * - Trade recording
 * 
 * DIFFERENT LOGIC (Mode-specific):
 * - Order execution (simulated vs real broker API)
 * - Fill confirmation (simulated vs real)
 * 
 * Usage:
 * - Backtest: TradeManager(config, capital)
 * - Live:     TradeManager(config, capital, broker)
 */
class TradeManager {
private:
    const Config& config;
    double starting_capital;  // ⭐ ADDED: Store starting capital for safe position sizing
    double current_capital;
    bool in_position;
    Trade current_trade;
    
    // Execution mode and broker (for live trading)
    ExecutionMode mode;
    Live::BrokerInterface* broker;  // nullptr for backtest
    
    /**
     * Calculate position size based on risk
     * @param entry_price Entry price
     * @param stop_loss Stop loss price
     * @return Position size (number of contracts)
     */
    int calculate_position_size(double entry_price, double stop_loss) const;
    
    /**
     * Calculate P&L for trade
     * @param trade Trade to calculate P&L for
     */
    void calculate_pnl(Trade& trade) const;
    
    /**
     * Execute entry order (simulated or real)
     * @param symbol Trading symbol
     * @param direction "BUY" or "SELL"
     * @param quantity Position size
     * @param price Limit price (for backtest)
     * @param bar  Current bar — used in backtest to clamp fill to bar's
     *             actual high/low range (prevents phantom fills above bar.high
     *             for SHORTs or below bar.low for LONGs). nullptr = no clamp.
     * @return Actual fill price
     */
    double execute_entry(const std::string& symbol,
                        const std::string& direction,
                        int quantity,
                        double price,
                        const Bar* bar = nullptr);
    
    /**
     * Execute exit order (simulated or real)
     * @param symbol Trading symbol
     * @param direction "BUY" or "SELL" (opposite of entry)
     * @param quantity Position size
     * @param price Limit price (for backtest)
     * @return Actual fill price
     */
    double execute_exit(const std::string& symbol,
                       const std::string& direction,
                       int quantity,
                       double price);
    
    // V6.0: Volume/OI integration
    const Utils::VolumeBaseline* volume_baseline_;
    const Core::OIScorer* oi_scorer_;
    
    // V6.0: Extract time slot helper
    std::string extract_time_slot(const std::string& datetime) const;

public:
    // V6.0: Volume exit signals
    enum class VolumeExitSignal {
        NONE,
        VOLUME_CLIMAX,
        VOLUME_DRYING_UP,
        VOLUME_DIVERGENCE
    };
    
    VolumeExitSignal check_volume_exit_signals(
        const Trade& trade,
        const Bar& current_bar
    ) const;
    
    // V6.0: OI exit signals
    enum class OIExitSignal {
        NONE,
        OI_UNWINDING,
        OI_REVERSAL,
        OI_STAGNATION
    };
    
    OIExitSignal check_oi_exit_signals(
        const Trade& trade,
        const Bar& current_bar,
        const std::vector<Bar>& bars,
        int current_index
    ) const;
    
    // Volume Exhaustion exit signals (for early loss exits)
    enum class VolumeExhaustionSignal {
        NONE,
        AGAINST_TREND_SPIKE,
        ABSORPTION,
        FLOW_REVERSAL,
        LOW_VOLUME_DRIFT
    };
    
    VolumeExhaustionSignal check_volume_exhaustion_signals(
        const Trade& trade,
        const Bar& current_bar,
        const std::vector<Bar>& bars,
        int current_index
    ) const;
    
    bool should_exit_on_exhaustion(
        VolumeExhaustionSignal signal,
        const Trade& trade,
        const Bar& current_bar
    ) const;
    
    /**
     * Constructor for BACKTEST mode
     * @param cfg Configuration
     * @param starting_capital Initial capital
     */
    TradeManager(const Config& cfg, double starting_capital);
    
    /**
     * Constructor for LIVE mode
     * @param cfg Configuration
     * @param starting_capital Initial capital
     * @param broker_interface Broker for real execution
     */
    TradeManager(const Config& cfg, 
                double starting_capital,
                Live::BrokerInterface* broker_interface);
    
    /**
     * Enter a new trade
     * @param decision Entry decision from scoring engine
     * @param zone Zone being traded
     * @param bar Current bar
     * @param bar_index Current bar index (for backtest)
     * @param regime Current market regime
     * @param symbol Trading symbol (for live mode)
     * @param bars Historical bars for indicator calculation
     * @return true if trade entered successfully
     */
    bool enter_trade(const EntryDecision& decision,
                    const Zone& zone,
                    const Bar& bar,
                    int bar_index,
                    MarketRegime regime,
                    const std::string& symbol = "",
                    const std::vector<Bar>* bars = nullptr);
    
    /**
     * Check if stop loss hit (SHARED LOGIC for both modes)
     * @param bar Current bar (for backtest)
     * @param current_price Current price (for live)
     * @return true if SL hit
     */
    bool check_stop_loss(const Bar& bar) const;
    bool check_stop_loss(double current_price) const;
    
    /**
     * Check if take profit hit (SHARED LOGIC for both modes)
     * @param bar Current bar (for backtest)
     * @param current_price Current price (for live)
     * @return true if TP hit
     */
    bool check_take_profit(const Bar& bar) const;
    bool check_take_profit(double current_price) const;
    
    /**
     * Close current trade
     * @param bar Exit bar (for backtest)
     * @param exit_reason Exit reason ("SL", "TP", "EOD", etc.)
     * @param exit_price Actual exit price
     * @return Closed trade record
     */
    Trade close_trade(const Bar& bar, 
                     const std::string& exit_reason,
                     double exit_price);
    
    /**
     * Close current trade (live mode)
     * @param exit_datetime Exit timestamp
     * @param exit_reason Exit reason
     * @param exit_price Actual exit price
     * @return Closed trade record
     */
    Trade close_trade(const std::string& exit_datetime,
                     const std::string& exit_reason,
                     double exit_price);
    
    /**
     * Check if currently in a position
     * @return true if position open
     */
    bool is_in_position() const { return in_position; }
    
    /**
     * Get current open trade
     * @return Reference to current trade
     */
    const Trade& get_current_trade() const { return current_trade; }
    
    /**
     * Update capital after trade close
     * @param pnl P&L from closed trade
     */
    void update_capital(double pnl);
    
    /**
     * Get current capital
     * @return Current capital amount
     */
    double get_capital() const { return current_capital; }
    
    /**
     * Get execution mode
     * @return Current mode (BACKTEST or LIVE)
     */
    ExecutionMode get_mode() const { return mode; }
    
    // V6.0: Set volume baseline
    void set_volume_baseline(const Utils::VolumeBaseline* baseline);
    
    // V6.0: Set OI scorer
    void set_oi_scorer(const Core::OIScorer* scorer);
};

} // namespace Backtest
} // namespace SDTrading

#endif // TRADE_MANAGER_H