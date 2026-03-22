// ============================================================
// CSV SIMULATOR BROKER - IMPLEMENTATION
// ============================================================

#include "csv_simulator_broker.h"
#include "utils/logger.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace SDTrading {
namespace Live {

using namespace Utils;
using namespace Core;

CsvSimulatorBroker::CsvSimulatorBroker(
    const std::string& csv_path,
    const std::string& symbol,
    const std::string& interval,
    bool enable_ticks,
    int ticks_per_bar
)
    : csv_file_path_(csv_path),
      trading_symbol_(symbol),
      bar_interval_(interval),
      enable_tick_simulation_(enable_ticks),
      ticks_per_bar_(ticks_per_bar),
      current_bar_index_(0),
      bars_loaded_count_(0),
      current_tick_in_bar_(0) {
    
    // Initialize empty position
    current_position_.is_open = false;
    
    LOG_INFO("CsvSimulatorBroker created");
    LOG_INFO("  CSV file:        " << csv_file_path_);
    LOG_INFO("  Symbol:          " << symbol);
    LOG_INFO("  Interval:        " << interval << " min");
    LOG_INFO("  Tick simulation: " << (enable_ticks ? "ENABLED" : "DISABLED"));
    if (enable_ticks) {
        LOG_INFO("  Ticks per bar:   " << ticks_per_bar);
    }
}

bool CsvSimulatorBroker::load_csv_bars() {
    LOG_INFO("Loading CSV data: " << csv_file_path_);
    
    std::ifstream file(csv_file_path_);
    if (!file.is_open()) {
        LOG_ERROR("Cannot open CSV file: " << csv_file_path_);
        return false;
    }
    
    all_bars_.clear();
    std::string line;
    std::getline(file, line);  // Skip header
    
    int line_num = 1;
    while (std::getline(file, line)) {
        line_num++;
        if (line.empty()) continue;
        
        try {
            std::stringstream ss(line);
            std::string timestamp, datetime, symbol;
            std::string open_str, high_str, low_str, close_str, volume_str, oi_str;
            
            // Parse Fyers format: Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OI
            std::getline(ss, timestamp, ',');
            std::getline(ss, datetime, ',');
            std::getline(ss, symbol, ',');
            std::getline(ss, open_str, ',');
            std::getline(ss, high_str, ',');
            std::getline(ss, low_str, ',');
            std::getline(ss, close_str, ',');
            std::getline(ss, volume_str, ',');
            std::getline(ss, oi_str, ',');
            
            Bar bar;
            bar.datetime = datetime;
            bar.open = std::stod(open_str);
            bar.high = std::stod(high_str);
            bar.low = std::stod(low_str);
            bar.close = std::stod(close_str);
            bar.volume = volume_str.empty() ? 0.0 : std::stod(volume_str);
            bar.oi = oi_str.empty() ? 0.0 : std::stod(oi_str);
            
            all_bars_.push_back(bar);
            
        } catch (const std::exception& e) {
            LOG_WARN("Skipping line " << line_num << ": " << e.what());
            continue;
        }
    }
    
    file.close();
    
    if (all_bars_.empty()) {
        LOG_ERROR("No bars loaded from CSV");
        return false;
    }
    
    LOG_INFO("✅ Loaded " << all_bars_.size() << " bars");
    LOG_INFO("   First: " << all_bars_.front().datetime);
    LOG_INFO("   Last:  " << all_bars_.back().datetime);
    
    return true;
}

bool CsvSimulatorBroker::connect() {
    LOG_INFO("Connecting CsvSimulatorBroker...");
    
    if (!load_csv_bars()) {
        LOG_ERROR("Failed to load CSV data");
        return false;
    }
    
    LOG_INFO("✅ CsvSimulatorBroker connected");
    LOG_INFO("   Ready to provide " << all_bars_.size() << " bars sequentially");
    
    return true;
}

void CsvSimulatorBroker::disconnect() {
    LOG_INFO("Disconnecting CsvSimulatorBroker...");
    
    // Export final order log
    if (!order_history_.empty()) {
        std::string log_path = "simulator_orders.csv";
        export_order_log(log_path);
        LOG_INFO("Order log exported: " << log_path);
    }
    
    LOG_INFO("CsvSimulatorBroker disconnected");
}

Bar CsvSimulatorBroker::generate_tick(const Bar& bar, int tick_number) const {
    // Generate intra-bar tick using OHLC sequence
    // Tick 0: Open
    // Tick 1: High (or Low if bearish)
    // Tick 2: Low (or High if bearish)
    // Tick 3: Close
    
    Bar tick = bar;  // Copy full bar
    
    bool is_bullish = (bar.close >= bar.open);
    
    if (tick_number == 0) {
        // Open tick
        tick.close = bar.open;
        tick.high = bar.open;
        tick.low = bar.open;
    } else if (tick_number == 1) {
        // First move (to high or low)
        if (is_bullish) {
            tick.close = bar.high;
            tick.low = bar.open;
        } else {
            tick.close = bar.low;
            tick.high = bar.open;
        }
    } else if (tick_number == 2) {
        // Counter move (to low or high)
        if (is_bullish) {
            tick.close = bar.low;
        } else {
            tick.close = bar.high;
        }
    } else {
        // Final tick = full bar
        tick.close = bar.close;
    }
    
    return tick;
}


Bar CsvSimulatorBroker::get_latest_bar(const std::string& symbol, const std::string& interval) {
    // Check for new bars in CSV file before returning next bar
    size_t previous_size = all_bars_.size();
    std::ifstream file(csv_file_path_);
    if (file.is_open()) {
        std::string line;
        std::getline(file, line); // Skip header
        std::vector<Bar> new_bars;
        int line_num = 1;
        while (std::getline(file, line)) {
            line_num++;
            if (line.empty()) continue;
            try {
                std::stringstream ss(line);
                std::string timestamp, datetime, symbol;
                std::string open_str, high_str, low_str, close_str, volume_str, oi_str;
                std::getline(ss, timestamp, ',');
                std::getline(ss, datetime, ',');
                std::getline(ss, symbol, ',');
                std::getline(ss, open_str, ',');
                std::getline(ss, high_str, ',');
                std::getline(ss, low_str, ',');
                std::getline(ss, close_str, ',');
                std::getline(ss, volume_str, ',');
                std::getline(ss, oi_str, ',');
                Bar bar;
                bar.datetime = datetime;
                bar.open = std::stod(open_str);
                bar.high = std::stod(high_str);
                bar.low = std::stod(low_str);
                bar.close = std::stod(close_str);
                bar.volume = volume_str.empty() ? 0.0 : std::stod(volume_str);
                bar.oi = oi_str.empty() ? 0.0 : std::stod(oi_str);
                new_bars.push_back(bar);
            } catch (const std::exception& e) {
                continue;
            }
        }
        file.close();
        // Only append new bars
        if (new_bars.size() > previous_size) {
            for (size_t i = previous_size; i < new_bars.size(); ++i) {
                all_bars_.push_back(new_bars[i]);
                LOG_INFO("[CSV] New bar appended: " << new_bars[i].datetime);
            }
        }
    }

    // Check if we're at end of data
    if (current_bar_index_ >= all_bars_.size()) {
        // At end - return last bar (repeat)
        LOG_DEBUG("At end of CSV data - repeating last bar");
        return all_bars_.back();
    }

    const Bar& current_bar = all_bars_[current_bar_index_];

    // ⭐ DISABLED: Let LiveEngine handle position exits for deterministic behavior
    // The CSV Broker's auto-exit was causing non-deterministic exit prices because
    // it competed with LiveEngine's manage_position() function.
    // if (current_position_.is_open) {
    //     check_position_exit(current_bar);
    // }

    if (enable_tick_simulation_) {
        // Generate tick within current bar
        Bar tick = generate_tick(current_bar, current_tick_in_bar_);

        // Advance tick counter
        current_tick_in_bar_++;

        // If all ticks exhausted, advance to next bar
        if (current_tick_in_bar_ >= ticks_per_bar_) {
            current_tick_in_bar_ = 0;
            current_bar_index_++;
            LOG_DEBUG("📊 Bar " << current_bar_index_ << " complete: " << current_bar.datetime);
        }
        return tick;
    } else {
        // No tick simulation - return full bar
        Bar result = current_bar;
        current_bar_index_++;
        LOG_DEBUG("📊 Delivered bar " << current_bar_index_ << ": " << result.datetime);
        return result;
    }
}

double CsvSimulatorBroker::get_ltp(const std::string& symbol) {
    if (current_bar_index_ >= all_bars_.size()) {
        return all_bars_.back().close;
    }
    
    if (enable_tick_simulation_) {
        // Return close of current tick
        const Bar& current_bar = all_bars_[current_bar_index_];
        Bar tick = generate_tick(current_bar, current_tick_in_bar_);
        return tick.close;
    } else {
        // Return close of current bar
        return all_bars_[current_bar_index_].close;
    }
}

OrderResponse CsvSimulatorBroker::place_market_order(const std::string& symbol,
                                                     const std::string& direction,
                                                     int quantity) {
    double entry_price = get_ltp(symbol);
    
    // Simulate order execution
    current_position_.symbol = symbol;
    current_position_.direction = direction;
    current_position_.entry_price = entry_price;
    current_position_.quantity = quantity;
    current_position_.is_open = true;
    
    // Get timestamp
    if (current_bar_index_ < all_bars_.size()) {
        current_position_.entry_time = all_bars_[current_bar_index_].datetime;
    }
    
    // Generate order ID
    OrderResponse response;
    response.order_id = "SIM_" + std::to_string(order_history_.size() + 1);
    response.status = OrderStatus::FILLED;
    response.filled_price = entry_price;
    response.filled_quantity = quantity;
    response.message = "Order filled at market";
    
    // Log order
    OrderLog log;
    log.timestamp = current_position_.entry_time;
    log.symbol = symbol;
    log.order_type = "ENTRY";
    log.direction = direction;
    log.price = entry_price;
    log.quantity = quantity;
    log.exit_reason = "";
    order_history_.push_back(log);
    
    LOG_INFO("📝 SIMULATED ORDER PLACED:");
    LOG_INFO("   Order ID:  " << response.order_id);
    LOG_INFO("   Symbol:    " << symbol);
    LOG_INFO("   Direction: " << direction);
    LOG_INFO("   Price:     " << entry_price);
    LOG_INFO("   Quantity:  " << quantity);
    
    return response;
}

OrderResponse CsvSimulatorBroker::place_limit_order(const std::string& symbol,
                                                    const std::string& direction,
                                                    int quantity,
                                                    double price) {
    // ⭐ GAP-1 FIX: Simulate a proper limit order fill, matching backtest behaviour.
    //
    // BACKTEST execute_entry clamps fills to the bar's actual range:
    //   SELL (SHORT): fill = price IF bar.high >= price, else fill = bar.high
    //   BUY  (LONG):  fill = price IF bar.low  <= price, else fill = bar.low
    // If the bar never reached the limit price on the protective side:
    //   SELL: bar.high < price  → reject (bar never reached our sell level)
    //   BUY:  bar.low  > price  → reject (bar never fell to our buy level)
    //
    // Previously CsvSimulatorBroker ignored the limit price entirely and filled
    // at get_ltp() = bar.close, which could be 5–70 pts past the intended entry.
    // This caused SL rescue paths to fire, degrading RR on virtually every trade.
    //
    // With this fix, both engines fill using the same limit-order semantics:
    //   - Entry at or better than the intended proximal-line price
    //   - Never filled deep inside the zone at bar.close
    
    const Bar* current_bar = nullptr;
    Bar bar_snapshot;
    if (current_bar_index_ < all_bars_.size()) {
        bar_snapshot = all_bars_[current_bar_index_];
        current_bar = &bar_snapshot;
    }

    double fill_price = price;  // start with the intended limit price

    if (current_bar != nullptr) {
        if (direction == "SELL") {
            // SHORT: limit sell at 'price'. Bar must reach that level.
            if (current_bar->high < price) {
                // Bar never reached our sell price — reject the order.
                OrderResponse rejected;
                rejected.status = OrderStatus::REJECTED;
                rejected.message = "Limit SELL not fillable: bar.high=" +
                                   std::to_string(current_bar->high) +
                                   " < limit=" + std::to_string(price);
                LOG_INFO("📵 LIMIT SELL rejected: bar.high=" << current_bar->high
                         << " < limit=" << price);
                return rejected;
            }
            // Bar reached our level — fill at limit price (or bar.high if that's worse)
            fill_price = std::min(price, current_bar->high);
        } else {
            // BUY / LONG: limit buy at 'price'. Bar must fall to that level.
            if (current_bar->low > price) {
                // Bar never fell to our buy price — reject the order.
                OrderResponse rejected;
                rejected.status = OrderStatus::REJECTED;
                rejected.message = "Limit BUY not fillable: bar.low=" +
                                   std::to_string(current_bar->low) +
                                   " > limit=" + std::to_string(price);
                LOG_INFO("📵 LIMIT BUY rejected: bar.low=" << current_bar->low
                         << " > limit=" << price);
                return rejected;
            }
            // Bar fell to our level — fill at limit price (or bar.low if that's worse)
            fill_price = std::max(price, current_bar->low);
        }
    }

    // Accepted — record position and return fill
    current_position_.symbol      = symbol;
    current_position_.direction   = direction;
    current_position_.entry_price = fill_price;
    current_position_.quantity    = quantity;
    current_position_.is_open     = true;
    if (current_bar_index_ < all_bars_.size()) {
        current_position_.entry_time = all_bars_[current_bar_index_].datetime;
    }

    OrderResponse response;
    response.order_id        = "SIM_LMT_" + std::to_string(order_history_.size() + 1);
    response.status          = OrderStatus::FILLED;
    response.filled_price    = fill_price;
    response.filled_quantity = quantity;
    response.message         = "Limit order filled at " + std::to_string(fill_price);

    OrderLog log;
    log.timestamp   = current_position_.entry_time;
    log.symbol      = symbol;
    log.order_type  = "ENTRY_LIMIT";
    log.direction   = direction;
    log.price       = fill_price;
    log.quantity    = quantity;
    log.exit_reason = "";
    order_history_.push_back(log);

    LOG_INFO("📝 LIMIT ORDER FILLED: " << direction << " " << quantity
             << " @ " << fill_price << " (limit=" << price << ")");
    return response;
}

bool CsvSimulatorBroker::check_position_exit(const Bar& current_bar) {
    if (!current_position_.is_open) {
        return false;
    }
    
    // Check SL/TP (if set)
    bool hit_sl = false;
    bool hit_tp = false;
    
    if (current_position_.direction == "BUY") {
        // Long position
        if (current_position_.stop_loss > 0 && current_bar.low <= current_position_.stop_loss) {
            hit_sl = true;
        }
        if (current_position_.take_profit > 0 && current_bar.high >= current_position_.take_profit) {
            hit_tp = true;
        }
    } else {
        // Short position
        if (current_position_.stop_loss > 0 && current_bar.high >= current_position_.stop_loss) {
            hit_sl = true;
        }
        if (current_position_.take_profit > 0 && current_bar.low <= current_position_.take_profit) {
            hit_tp = true;
        }
    }
    
    if (hit_sl || hit_tp) {
        // Auto-exit position
        std::string exit_reason = hit_sl ? "SL" : "TP";
        double exit_price = hit_sl ? current_position_.stop_loss : current_position_.take_profit;
        
        // Log exit
        OrderLog log;
        log.timestamp = current_bar.datetime;
        log.symbol = current_position_.symbol;
        log.order_type = "EXIT";
        log.direction = (current_position_.direction == "BUY" ? "SELL" : "BUY");
        log.price = exit_price;
        log.quantity = current_position_.quantity;
        log.exit_reason = exit_reason;
        order_history_.push_back(log);
        
        LOG_INFO("🔔 AUTO-EXIT: " << exit_reason << " triggered @ " << exit_price);
        
        current_position_.is_open = false;
        return true;
    }
    
    return false;
}

void CsvSimulatorBroker::skip_to_bar_index(size_t index) {
    if (index >= all_bars_.size()) {
        LOG_WARN("Cannot skip to index " << index << " (max: " << all_bars_.size() << ")");
        current_bar_index_ = all_bars_.size();
    } else {
        current_bar_index_ = index;
        current_tick_in_bar_ = 0;
        LOG_INFO("⏩ Simulator positioned at bar " << index);
    }
}

Core::Bar CsvSimulatorBroker::get_bar_at_index(size_t index) const {
    if (index >= all_bars_.size()) {
        LOG_ERROR("Bar index " << index << " out of range (max: " << all_bars_.size() << ")");
        return all_bars_.empty() ? Core::Bar{} : all_bars_.back();
    }
    return all_bars_[index];
}

void CsvSimulatorBroker::export_order_log(const std::string& output_path) const {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        LOG_ERROR("Cannot create order log: " << output_path);
        return;
    }
    
    // Write header
    file << "timestamp,symbol,order_type,direction,price,quantity,exit_reason\n";
    
    // Write orders
    for (const auto& order : order_history_) {
        file << order.timestamp << ","
             << order.symbol << ","
             << order.order_type << ","
             << order.direction << ","
             << std::fixed << std::setprecision(2) << order.price << ","
             << order.quantity << ","
             << order.exit_reason << "\n";
    }
    
    file.close();
    LOG_INFO("Order log exported: " << output_path << " (" << order_history_.size() << " orders)");
}

bool CsvSimulatorBroker::is_connected() const {
    return !all_bars_.empty();
}

OrderResponse CsvSimulatorBroker::place_stop_order(const std::string& symbol,
                                                   const std::string& direction,
                                                   int quantity,
                                                   double stop_price) {
    // Treat stop orders as market orders for simulation
    LOG_INFO("Converting stop order to market order (simulation)");
    return place_market_order(symbol, direction, quantity);
}

bool CsvSimulatorBroker::cancel_order(const std::string& order_id) {
    LOG_INFO("Cancelling order (simulated): " << order_id);
    return true;  // Assume successful cancellation
}

std::vector<Position> CsvSimulatorBroker::get_positions() {
    std::vector<Position> positions;
    if (current_position_.is_open) {
        Position pos;
        pos.symbol = current_position_.symbol;
        pos.direction = current_position_.direction;
        pos.quantity = current_position_.quantity;
        pos.entry_price = current_position_.entry_price;
        pos.current_price = get_ltp(current_position_.symbol);
        pos.unrealized_pnl = (pos.current_price - pos.entry_price) * pos.quantity;
        positions.push_back(pos);
    }
    return positions;
}

double CsvSimulatorBroker::get_account_balance() {
    return 100000.0;  // Simulated account balance
}

bool CsvSimulatorBroker::has_position(const std::string& symbol) {
    return current_position_.is_open && current_position_.symbol == symbol;
}

std::vector<Core::Bar> CsvSimulatorBroker::get_historical_bars(
    const std::string& symbol,
    const std::string& interval,
    int count
) {
    // Return empty vector - simulator doesn't provide historical data
    // The actual bars are delivered sequentially via get_latest_bar
    return std::vector<Core::Bar>();
}

} // namespace Live
} // namespace SDTrading