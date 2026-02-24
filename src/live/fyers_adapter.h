// ============================================================
// FYERS BROKER ADAPTER - CORRECTED VERSION
// CSV-Based Simulation Mode
// ============================================================

#ifndef FYERS_ADAPTER_H
#define FYERS_ADAPTER_H

#include "broker_interface.h"
#include <string>
#include <vector>
#include <map>

namespace SDTrading {
namespace Live {

class FyersAdapter : public BrokerInterface {
private:
    std::string api_key;
    std::string access_token;
    std::string client_id;
    std::string data_file_path;
    bool connected;
    
    // Position tracking
    std::map<std::string, Position> positions_cache;
    double account_balance;
    
    // ⭐ NEW: CSV Bar Management
    std::vector<Core::Bar> csv_bars_;       // All bars loaded from CSV
    size_t current_bar_index_;              // Current position in CSV
    std::string last_returned_datetime_;    // Track last bar returned
    bool csv_loaded_;                       // Whether CSV is loaded
    
    // ⭐ NEW: Helper method to load CSV
    bool load_csv_bars();

public:
    FyersAdapter(const std::string& api_key,
                const std::string& access_token,
                const std::string& client_id,
                const std::string& data_file_path);
    
    virtual ~FyersAdapter() = default;
    
    // BrokerInterface implementation
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    
    // Market data
    double get_ltp(const std::string& symbol) override;
    Core::Bar get_latest_bar(const std::string& symbol, const std::string& interval) override;
    std::vector<Core::Bar> get_historical_bars(const std::string& symbol,
                                               const std::string& interval,
                                               int count) override;
    
    // Order management - CORRECTED SIGNATURES
    OrderResponse place_market_order(
        const std::string& symbol,
        const std::string& direction,
        int quantity
    ) override;
    
    OrderResponse place_limit_order(
        const std::string& symbol,
        const std::string& direction,
        int quantity,
        double price
    ) override;
    
    OrderResponse place_stop_order(
        const std::string& symbol,
        const std::string& direction,
        int quantity,
        double stop_price
    ) override;
    
    bool cancel_order(const std::string& order_id) override;
    std::vector<Position> get_positions() override;
    double get_account_balance() override;
    bool has_position(const std::string& symbol) override;
    
    // ⭐ NEW: CSV Progress Monitoring
    size_t get_csv_bar_count() const { return csv_bars_.size(); }
    size_t get_current_bar_index() const { return current_bar_index_; }
    bool has_more_bars() const { return current_bar_index_ < csv_bars_.size(); }
    
    // ⭐ NEW: Skip already-loaded bars (for sync with engine)
    void skip_to_bar_index(size_t index) {
        if (index <= csv_bars_.size()) {  // Allow positioning at or past end
            current_bar_index_ = index;
            last_returned_datetime_.clear();  // Reset to allow new bars
        }
    }
};

}  // namespace Live
}  // namespace SDTrading

#endif // FYERS_ADAPTER_H