// ============================================================
// FYERS BROKER ADAPTER - CORRECTED IMPLEMENTATION
// CSV-Based Simulation Mode with Stateful Reading
// WINDOWS-COMPATIBLE VERSION
// ============================================================

// CRITICAL: Define NOMINMAX before any Windows includes to prevent min/max macro conflicts
#ifdef _WIN32
#define NOMINMAX
#endif

#include "fyers_adapter.h"
#include "../utils/logger.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>  // For std::max, std::min
#include <array>
#include <memory>
#include <stdexcept>

namespace SDTrading {
namespace Live {

using namespace Utils;
using namespace Core;

// Simple JSON value extraction (no external library needed)
std::string extract_json_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return "";
    }
    
    pos += search.length();
    
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    
    if (pos >= json.length()) return "";
    
    // Check if value is string (starts with ")
    if (json[pos] == '"') {
        pos++;  // Skip opening quote
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    } else {
        // Number or boolean
        size_t end = pos;
        while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
            end++;
        }
        return json.substr(pos, end - pos);
    }
}

// Helper function to execute Python script and get JSON response
std::string execute_python_command(const std::string& script_path, const std::vector<std::string>& args) {
#ifdef _WIN32
    // Build command: python script.py arg1 arg2 ...
    // Force UTF-8 mode to avoid Windows console encoding issues
    std::string command = "python -X utf8 \"" + script_path + "\"";
    for (const auto& arg : args) {
        command += " \"" + arg + "\"";
    }
    
    // Use _popen to capture output
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
    
    if (!pipe) {
        return "{\"s\":\"error\",\"message\":\"Failed to execute Python command\"}";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    return result;
#else
    // Linux/Mac support (not implemented)
    return "{\"s\":\"error\",\"message\":\"Python execution not implemented for this platform\"}";
#endif
}

FyersAdapter::FyersAdapter(const std::string& key,
                          const std::string& token,
                          const std::string& client,
                          const std::string& data_path)
    : api_key(key),
      access_token(token),
      client_id(client),
      data_file_path(data_path),
      connected(false),
      account_balance(1000000.0),  // $1M simulated balance
      current_bar_index_(0),
      csv_loaded_(false) {
    
    LOG_INFO("FyersAdapter created (CSV Simulation Mode)");
    LOG_INFO("  Data file: " << data_file_path);
}

bool FyersAdapter::connect() {
    LOG_INFO("==================================================");
    LOG_INFO("  Connecting to Fyers Broker (CSV Simulation)");
    LOG_INFO("==================================================");
    
    // ⭐ CRITICAL FIX: Load CSV data at connection time
    if (!load_csv_bars()) {
        LOG_ERROR("Failed to load CSV data from: " << data_file_path);
        return false;
    }
    
    connected = true;
    current_bar_index_ = 0;  // Start at beginning
    
    LOG_INFO("✅ CSV Connection Successful");
    LOG_INFO("   Total bars loaded: " << csv_bars_.size());
    if (!csv_bars_.empty()) {
        LOG_INFO("   First bar: " << csv_bars_.front().datetime);
        LOG_INFO("   Last bar:  " << csv_bars_.back().datetime);
    }
    LOG_INFO("==================================================");
    
    return true;
}

bool FyersAdapter::load_csv_bars() {
    LOG_INFO("Loading bars from CSV: " << data_file_path);
    
    csv_bars_.clear();
    csv_loaded_ = false;
    
    std::ifstream file(data_file_path);
    if (!file.is_open()) {
        LOG_ERROR("Cannot open CSV file: " << data_file_path);
        return false;
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    
    int line_num = 1;
    int valid_bars = 0;
    
    while (std::getline(file, line)) {
        line_num++;
        if (line.empty()) continue;
        
        std::istringstream ss(line);
        std::string field;
        Bar bar;
        int col = 0;
        
        // CSV format: Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OpenInterest,OI_Fresh,OI_Age_Seconds (V6.0)
        while (std::getline(ss, field, ',')) {
            try {
                // Remove carriage return if present
                if (!field.empty() && field.back() == '\r') {
                    field.pop_back();
                }
                
                switch (col) {
                    case 0: /* timestamp */ break;
                    case 1: 
                        bar.datetime = field; 
                        break;
                    case 2: /* symbol */ break;
                    case 3: bar.open = std::stod(field); break;
                    case 4: bar.high = std::stod(field); break;
                    case 5: bar.low = std::stod(field); break;
                    case 6: bar.close = std::stod(field); break;
                    case 7: 
                        // Safe conversion for volume
                        bar.volume = static_cast<long long>(std::stod(field)); 
                        break;
                    case 8: 
                        bar.oi = std::stod(field); 
                        break;
                    case 9: /* NEW V6.0: OI_Fresh */
                        bar.oi_fresh = (field == "1" || field == "true" || field == "True");
                        break;
                    case 10: /* NEW V6.0: OI_Age_Seconds */
                        bar.oi_age_seconds = std::stoi(field);
                        break;
                }
            } catch (const std::exception& e) {
                LOG_WARN("Parse error at line " << line_num << ", col " << col 
                        << ": " << e.what() << " (field: '" << field << "')");
            }
            col++;
        }
        
        if (col >= 8) {  // Valid bar with all required fields (backward compatible)
            csv_bars_.push_back(bar);
            valid_bars++;
        } else if (col > 0) {
            LOG_WARN("Skipping incomplete line " << line_num << " (only " << col << " fields)");
        }
    }
    
    file.close();
    csv_loaded_ = true;
    
    LOG_INFO("✅ CSV Loaded Successfully");
    LOG_INFO("   Total lines read: " << line_num);
    LOG_INFO("   Valid bars: " << valid_bars);
    
    if (csv_bars_.empty()) {
        LOG_ERROR("No valid bars found in CSV file!");
        csv_loaded_ = false;
        return false;
    }
    
    // Log first and last few bars for verification
    LOG_INFO("   Sample bars:");
    size_t sample_count = (std::min)(size_t(3), csv_bars_.size());
    for (size_t i = 0; i < sample_count; i++) {
        LOG_INFO("     [" << i << "] " << csv_bars_[i].datetime 
                << " C=" << csv_bars_[i].close);
    }
    if (csv_bars_.size() > 3) {
        LOG_INFO("     ...");
        for (size_t i = csv_bars_.size() - 3; i < csv_bars_.size(); i++) {
            LOG_INFO("     [" << i << "] " << csv_bars_[i].datetime 
                    << " C=" << csv_bars_[i].close);
        }
    }
    
    return true;
}

void FyersAdapter::disconnect() {
    LOG_INFO("Disconnecting from Fyers broker (CSV mode)");
    connected = false;
    csv_bars_.clear();
    csv_loaded_ = false;
    current_bar_index_ = 0;
    last_returned_datetime_.clear();
    positions_cache.clear();
}

bool FyersAdapter::is_connected() const {
    return connected && csv_loaded_;
}

double FyersAdapter::get_ltp(const std::string& symbol) {
    if (!connected || !csv_loaded_) {
        LOG_ERROR("Not connected or CSV not loaded");
        return 0.0;
    }
    
    // ⭐ CRITICAL FIX: Return current bar's close price from CSV
    if (current_bar_index_ == 0) {
        LOG_DEBUG("No bars processed yet, returning 0.0");
        return 0.0;
    }
    
    // Return close of most recently returned bar
    size_t idx = (current_bar_index_ > 0) ? current_bar_index_ - 1 : 0;
    
    if (idx < csv_bars_.size()) {
        double ltp = csv_bars_[idx].close;
        LOG_DEBUG("LTP for " << symbol << ": " << ltp 
                 << " (bar " << idx << ": " << csv_bars_[idx].datetime << ")");
        return ltp;
    }
    
    LOG_WARN("Bar index out of range: " << idx << " / " << csv_bars_.size());
    return 0.0;
}

Bar FyersAdapter::get_latest_bar(const std::string& symbol, const std::string& interval) {
    if (!connected || !csv_loaded_) {
        LOG_ERROR("Not connected or CSV not loaded");
        return Bar();
    }
    
    // ⭐ CRITICAL FIX: Return next bar from CSV, not simulated data
    
    // Check if we've reached the end of CSV data
    if (current_bar_index_ >= csv_bars_.size()) {
        LOG_DEBUG("End of CSV data reached (bar " << current_bar_index_ 
                 << "/" << csv_bars_.size() << ") - checking for appended data...");

        // Attempt to reload CSV to pick up newly appended bars
        size_t previous_count = csv_bars_.size();
        if (!load_csv_bars()) {
            LOG_WARN("Reload failed while at end-of-file; returning last known bar if available");
            return csv_bars_.empty() ? Bar() : csv_bars_.back();
        }

        size_t new_count = csv_bars_.size();
        if (new_count > previous_count) {
            LOG_INFO("📈 Detected new bars appended to CSV: " << previous_count << " -> " << new_count);
        } else {
            LOG_DEBUG("No new bars appended to CSV yet; returning last known bar");
            return csv_bars_.empty() ? Bar() : csv_bars_.back();
        }

        // If new bars were loaded, fall through to normal path below
    }
    
    // Get next bar from CSV
    Bar bar = csv_bars_[current_bar_index_];
    
    // Check if this is a new bar (different datetime from last returned)
    if (bar.datetime != last_returned_datetime_) {
        LOG_DEBUG("📊 New CSV bar [" << current_bar_index_ << "/" << csv_bars_.size() 
                 << "]: " << bar.datetime 
                 << " O=" << bar.open << " H=" << bar.high 
                 << " L=" << bar.low << " C=" << bar.close);
        
        // ⭐ Also print to console (not just log)
        std::cout << "\n[BROKER] New bar from CSV: " << bar.datetime 
                  << " | Close: " << bar.close << std::endl;
        std::cout.flush();
        
        last_returned_datetime_ = bar.datetime;
        current_bar_index_++;  // Advance to next bar
    } else {
        LOG_DEBUG("Same bar as last call: " << bar.datetime);
    }
    
    return bar;
}

std::vector<Bar> FyersAdapter::get_historical_bars(const std::string& symbol,
                                                   const std::string& interval,
                                                   int count) {
    if (!connected || !csv_loaded_) {
        LOG_ERROR("Not connected or CSV not loaded");
        return std::vector<Bar>();
    }
    
    LOG_INFO("Getting " << count << " historical bars for " << symbol);
    
    // Return last 'count' bars from CSV
    std::vector<Bar> result;
    
    if (csv_bars_.empty()) {
        LOG_WARN("No bars available in CSV");
        return result;
    }
    
    // Use parentheses to avoid Windows min/max macro conflict
    int start_idx = (std::max)(0, static_cast<int>(csv_bars_.size()) - count);
    
    for (size_t i = static_cast<size_t>(start_idx); i < csv_bars_.size(); i++) {
        result.push_back(csv_bars_[i]);
    }
    
    LOG_INFO("Returning " << result.size() << " bars from CSV");
    return result;
}

OrderResponse FyersAdapter::place_market_order(const std::string& symbol,
                                               const std::string& direction,
                                               int quantity) {
    OrderResponse response;
    
    if (!connected) {
        LOG_ERROR("Not connected to broker");
        response.status = OrderStatus::REJECTED;
        response.message = "Not connected";
        return response;
    }
    
    std::cout << "\n+====================================================+\n";
    std::cout << "| [BROKER] PLACING REAL ORDER TO FYERS              |\n";
    std::cout << "+====================================================+\n";
    std::cout << "  Symbol:     " << symbol << "\n";
    std::cout << "  Direction:  " << direction << "\n";
    std::cout << "  Quantity:   " << quantity << "\n";
    std::cout << "  Type:       MARKET\n";
    std::cout.flush();
    
    LOG_INFO("📤 REAL MARKET ORDER TO FYERS");
    LOG_INFO("   Symbol:     " << symbol);
    LOG_INFO("   Direction:  " << direction);
    LOG_INFO("   Quantity:   " << quantity);
    
    // Path to Python order manager script
    std::string script_path = "D:/ClaudeAi/Alert/scripts/fyers_order_manager.py";
    
    // Build arguments
    std::vector<std::string> args = {
        "place_market",
        symbol,
        direction,
        std::to_string(quantity)
    };
    
    std::cout << "  Calling Python order manager...\n";
    std::cout.flush();
    
    // Execute Python script
    std::string json_response = execute_python_command(script_path, args);
    
    std::cout << "  Response:   " << json_response << "\n";
    std::cout.flush();
    
    LOG_INFO("API Response: " << json_response);
    
    // Parse JSON response (simple extraction)
    std::string status = extract_json_value(json_response, "s");
    std::string error_message = extract_json_value(json_response, "message");
    std::string order_id = extract_json_value(json_response, "id");
    
    if (status == "ok") {
        // Order placed successfully
        response.order_id = order_id.empty() ? "UNKNOWN" : order_id;
        response.status = OrderStatus::FILLED;  // Assume filled for market orders
        response.filled_price = get_ltp(symbol);  // Get current price
        response.filled_quantity = quantity;
        response.message = "Order placed successfully";
        
        std::cout << "  Status:     SUCCESS\n";
        std::cout << "  Order ID:   " << response.order_id << "\n";
        std::cout << "  Filled @:   " << response.filled_price << "\n";
        std::cout << "+====================================================+\n";
        std::cout.flush();
        
        LOG_INFO("✅ Order placed: " << response.order_id);
        
        // Update positions cache (optimistic)
        Position pos;
        pos.symbol = symbol;
        pos.direction = direction;
        pos.quantity = quantity;
        pos.entry_price = response.filled_price;
        pos.current_price = response.filled_price;
        pos.unrealized_pnl = 0;
        positions_cache[symbol] = pos;
        
    } else {
        // Order rejected
        response.status = OrderStatus::REJECTED;
        response.message = error_message.empty() ? "Unknown error" : error_message;
        
        std::cout << "  Status:     REJECTED\n";
        std::cout << "  Reason:     " << response.message << "\n";
        std::cout << "+====================================================+\n";
        std::cout.flush();
        
        LOG_ERROR("❌ Order rejected: " << response.message);
    }
    
    return response;
}

OrderResponse FyersAdapter::place_limit_order(const std::string& symbol,
                                              const std::string& direction,
                                              int quantity,
                                              double price) {
    OrderResponse response;
    
    if (!connected) {
        LOG_ERROR("Not connected to broker");
        response.status = OrderStatus::REJECTED;
        response.message = "Not connected";
        return response;
    }
    
    LOG_INFO("📤 SIMULATED LIMIT ORDER (CSV Mode)");
    LOG_INFO("   Symbol:     " << symbol);
    LOG_INFO("   Direction:  " << direction);
    LOG_INFO("   Quantity:   " << quantity);
    LOG_INFO("   Limit Price:" << price);
    
    // Simulate order placement
    response.order_id = "ORD" + std::to_string(std::time(0));
    response.status = OrderStatus::PENDING;
    response.message = "Order placed (SIMULATED)";
    
    LOG_INFO("✅ Limit order placed: " << response.order_id);
    
    return response;
}

OrderResponse FyersAdapter::place_stop_order(const std::string& symbol,
                                             const std::string& direction,
                                             int quantity,
                                             double stop_price) {
    OrderResponse response;
    
    if (!connected) {
        LOG_ERROR("Not connected to broker");
        response.status = OrderStatus::REJECTED;
        response.message = "Not connected";
        return response;
    }
    
    LOG_INFO("📤 SIMULATED STOP ORDER (CSV Mode)");
    LOG_INFO("   Symbol:     " << symbol);
    LOG_INFO("   Direction:  " << direction);
    LOG_INFO("   Quantity:   " << quantity);
    LOG_INFO("   Stop Price: " << stop_price);
    
    // Simulate order placement
    response.order_id = "ORD" + std::to_string(std::time(0));
    response.status = OrderStatus::PENDING;
    response.message = "Stop order placed (SIMULATED)";
    
    LOG_INFO("✅ Stop order placed: " << response.order_id);
    
    return response;
}

bool FyersAdapter::cancel_order(const std::string& order_id) {
    if (!connected) {
        LOG_ERROR("Not connected to broker");
        return false;
    }
    
    LOG_INFO("📤 SIMULATED ORDER CANCELLATION (CSV Mode)");
    LOG_INFO("   Order ID: " << order_id);
    LOG_INFO("✅ Order cancelled (SIMULATED)");
    
    return true;
}

std::vector<Position> FyersAdapter::get_positions() {
    if (!connected) {
        LOG_ERROR("Not connected to broker");
        return std::vector<Position>();
    }
    
    std::vector<Position> positions;
    for (const auto& pair : positions_cache) {
        positions.push_back(pair.second);
    }
    
    return positions;
}

double FyersAdapter::get_account_balance() {
    if (!connected) {
        LOG_ERROR("Not connected to broker");
        return 0.0;
    }
    
    return account_balance;
}

bool FyersAdapter::has_position(const std::string& symbol) {
    return positions_cache.find(symbol) != positions_cache.end();
}

}  // namespace Live
}  // namespace SDTrading