// ============================================================
// CSV SIMULATOR BROKER - FOR DRY RUN ENGINE
// Simulates live broker by feeding CSV data sequentially
// ============================================================

#ifndef CSV_SIMULATOR_BROKER_H
#define CSV_SIMULATOR_BROKER_H

#include "live/broker_interface.h"
#include "common_types.h"
#include <vector>
#include <string>
#include <chrono>

namespace SDTrading {
namespace Live {

/**
 * @class CsvSimulatorBroker
 * @brief Simulates a live broker by reading CSV data sequentially
 * 
 * DESIGN PHILOSOPHY:
 * - Behaves EXACTLY like a real broker (from LiveEngine perspective)
 * - Feeds bars ONE AT A TIME (sequential, not batch)
 * - Supports "tick simulation" (can update same bar multiple times)
 * - Deterministic and repeatable
 * 
 * KEY FEATURES:
 * 1. Sequential bar delivery (bar-by-bar advancement)
 * 2. Configurable tick simulation (intra-bar price updates)
 * 3. Position tracking (for simulated order execution)
 * 4. Trade logging (for verification)
 * 
 * USAGE:
 * ```cpp
 * auto simulator = std::make_unique<CsvSimulatorBroker>(
 *     "data/nifty_jan2025.csv",
 *     "NIFTY25JAN",
 *     "15",  // 15-minute bars
 *     true   // Enable tick simulation
 * );
 * 
 * LiveEngine engine(config, std::move(simulator), ...);
 * engine.run();  // Processes CSV as if live data
 * ```
 */
class CsvSimulatorBroker : public BrokerInterface {
private:
    // CSV data management
    std::string csv_file_path_;
    std::vector<Core::Bar> all_bars_;
    size_t current_bar_index_;
    size_t bars_loaded_count_;  // Track how many bars were pre-loaded
    
    // Symbol configuration
    std::string trading_symbol_;
    std::string bar_interval_;
    
    // Tick simulation settings
    bool enable_tick_simulation_;
    int ticks_per_bar_;
    int current_tick_in_bar_;
    
    // Position tracking (for order simulation)
    struct SimulatedPosition {
        std::string symbol;
        std::string direction;  // "BUY" or "SELL"
        double entry_price;
        double stop_loss;
        double take_profit;
        int quantity;
        std::string entry_time;
        bool is_open;
    };
    SimulatedPosition current_position_;
    
    // Order execution log
    struct OrderLog {
        std::string timestamp;
        std::string symbol;
        std::string order_type;  // "ENTRY", "EXIT"
        std::string direction;
        double price;
        int quantity;
        std::string exit_reason;  // For exits: "SL", "TP", "MANUAL"
    };
    std::vector<OrderLog> order_history_;
    
    /**
     * Load all bars from CSV file
     * @return true if loaded successfully
     */
    bool load_csv_bars();
    
    /**
     * Generate intra-bar tick (simulate price movement within bar)
     * Uses linear interpolation between OHLC
     * @param bar Full bar data
     * @param tick_number Current tick number (0 to ticks_per_bar-1)
     * @return Simulated bar at this tick
     */
    Core::Bar generate_tick(const Core::Bar& bar, int tick_number) const;
    
    /**
     * Check if current position hit SL/TP
     * @param current_bar Latest bar data
     * @return true if position should be closed
     */
    bool check_position_exit(const Core::Bar& current_bar);

public:
    /**
     * Constructor
     * @param csv_path Path to CSV data file
     * @param symbol Trading symbol
     * @param interval Bar interval (e.g., "1", "5", "15")
     * @param enable_ticks Enable intra-bar tick simulation
     * @param ticks_per_bar Number of ticks per bar (default: 4 = OHLC)
     */
    CsvSimulatorBroker(
        const std::string& csv_path,
        const std::string& symbol,
        const std::string& interval,
        bool enable_ticks = false,
        int ticks_per_bar = 4
    );
    
    // ========================================
    // BrokerInterface Implementation
    // ========================================
    
    /**
     * Connect to broker (loads CSV data)
     * @return true if CSV loaded successfully
     */
    bool connect() override;
    
    /**
     * Disconnect (cleanup)
     */
    void disconnect() override;
    
    /**
     * Check if connected
     * @return true if connected
     */
    bool is_connected() const override;
    
    /**
     * Get latest bar (sequential delivery)
     * BEHAVIOR:
     * - First call: Returns bar at current_bar_index_
     * - If tick simulation enabled: Returns interpolated tick
     * - When all ticks exhausted: Advances to next bar
     * - At end of CSV: Returns last bar (repeats)
     * 
     * @param symbol Trading symbol
     * @param interval Bar interval
     * @return Current bar (or tick)
     */
    Core::Bar get_latest_bar(const std::string& symbol, 
                             const std::string& interval) override;
    
    /**
     * Get current LTP
     * @param symbol Trading symbol
     * @return Last traded price (close of current bar/tick)
     */
    double get_ltp(const std::string& symbol) override;
    
    /**
     * Place market order (simulated)
     * @param symbol Trading symbol
     * @param direction "BUY" or "SELL"
     * @param quantity Lot quantity
     * @return Order response
     */
    OrderResponse place_market_order(const std::string& symbol,
                                     const std::string& direction,
                                     int quantity) override;
    
    /**
     * Place limit order (simulated as market for simplicity)
     * @param symbol Trading symbol
     * @param direction "BUY" or "SELL"
     * @param quantity Lot quantity
     * @param price Limit price (ignored in simulation)
     * @return Order response
     */
    OrderResponse place_limit_order(const std::string& symbol,
                                    const std::string& direction,
                                    int quantity,
                                    double price) override;
    
    /**
     * Place stop order (simulated as market for simplicity)
     * @param symbol Trading symbol
     * @param direction "BUY" or "SELL"
     * @param quantity Order quantity
     * @param stop_price Stop trigger price
     * @return Order response
     */
    OrderResponse place_stop_order(const std::string& symbol,
                                   const std::string& direction,
                                   int quantity,
                                   double stop_price) override;
    
    /**
     * Cancel order (no-op for simulator)
     * @param order_id Order ID to cancel
     * @return true if cancelled successfully
     */
    bool cancel_order(const std::string& order_id) override;
    
    /**
     * Get current positions
     * @return Vector of open positions
     */
    std::vector<Position> get_positions() override;
    
    /**
     * Get account balance
     * @return Available balance
     */
    double get_account_balance() override;
    
    /**
     * Check if position exists for symbol
     * @param symbol Instrument symbol
     * @return true if position exists
     */
    bool has_position(const std::string& symbol) override;
    
    /**
     * Get historical bars (returns empty for simulator)
     * @param symbol Instrument symbol
     * @param interval Bar interval
     * @param count Number of bars
     * @return Vector of historical bars
     */
    std::vector<Core::Bar> get_historical_bars(
        const std::string& symbol,
        const std::string& interval,
        int count
    ) override;
    
    // ========================================
    // Simulator-Specific Methods
    // ========================================
    
    /**
     * Get total bars in CSV
     * @return Total bar count
     */
    size_t get_total_bars() const { return all_bars_.size(); }
    
    /**
     * Get current position in CSV
     * @return Current bar index
     */
    size_t get_current_position() const { return current_bar_index_; }
    
    /**
     * Check if more bars available
     * @return true if not at end of CSV
     */
    bool has_more_bars() const { 
        return current_bar_index_ < all_bars_.size(); 
    }
    
    /**
     * Get progress percentage
     * @return 0.0 to 100.0
     */
    double get_progress_pct() const {
        if (all_bars_.empty()) return 0.0;
        return (current_bar_index_ * 100.0) / all_bars_.size();
    }
    
    /**
     * Skip to specific bar index (for positioning)
     * Used during initialization to start after pre-loaded historical data
     * @param index Bar index to jump to
     */
    void skip_to_bar_index(size_t index);
    
    /**
     * Get bar at specific index (for bootstrap loading)
     * @param index Bar index to retrieve
     * @return Bar at the specified index
     */
    Core::Bar get_bar_at_index(size_t index) const;
    
    /**
     * Get order execution history
     * @return Vector of all executed orders
     */
    const std::vector<OrderLog>& get_order_history() const { 
        return order_history_; 
    }
    
    /**
     * Export order history to CSV
     * @param output_path Path to output file
     */
    void export_order_log(const std::string& output_path) const;
    
    /**
     * Reset simulator to beginning
     */
    void reset() {
        current_bar_index_ = 0;
        current_tick_in_bar_ = 0;
        current_position_ = SimulatedPosition{};
        order_history_.clear();
    }
    
    /**
     * Set how many bars were pre-loaded (for correct positioning)
     * @param count Number of bars already loaded by engine
     */
    void set_preloaded_bar_count(size_t count) {
        bars_loaded_count_ = count;
    }
};

} // namespace Live
} // namespace SDTrading

#endif // CSV_SIMULATOR_BROKER_H
