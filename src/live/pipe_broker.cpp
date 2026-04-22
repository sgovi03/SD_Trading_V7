#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
// ============================================================
// SD TRADING V8 - PIPE BROKER IMPLEMENTATION
// src/live/pipe_broker.cpp
// ============================================================

#include "live/pipe_broker.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace SDTrading {
namespace Live {

using namespace Utils;

// ============================================================
// Construction
// ============================================================

PipeBroker::PipeBroker(
    const std::string&               symbol,
    const OrderSubmitConfig&         order_cfg,
    std::function<double()>          get_balance,
    std::function<bool()>            entry_allowed,
    std::function<void(int, double)> on_order_ok)
    : symbol_        (symbol)
    , order_submitter_(order_cfg)
    , get_balance_   (std::move(get_balance))
    , entry_allowed_ (std::move(entry_allowed))
    , on_order_ok_   (std::move(on_order_ok))
{
    bar_history_.reserve(HISTORY_CAPACITY);
    LOG_INFO("[PipeBroker:" << symbol_ << "] Created.");
}

// ============================================================
// Push interface — called from BarRouter (pipe thread)
// ============================================================

void PipeBroker::push_bar(const Core::Bar& bar) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        bar_queue_.push(bar);
    }
    bar_cv_.notify_one();
    ++bars_pushed_;

    // Maintain ring buffer for get_historical_bars()
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        if (static_cast<int>(bar_history_.size()) >= HISTORY_CAPACITY) {
            bar_history_.erase(bar_history_.begin());
        }
        bar_history_.push_back(bar);
    }

    LOG_DEBUG("[PipeBroker:" << symbol_ << "] Bar pushed: "
              << bar.datetime << " C=" << bar.close);
}

void PipeBroker::shutdown() {
    shutdown_.store(true);
    bar_cv_.notify_all();   // Unblock any waiting LiveEngine thread
    LOG_INFO("[PipeBroker:" << symbol_ << "] Shutdown signalled.");
}

// ============================================================
// BrokerInterface — get_latest_bar
// Blocks until bar available or shutdown signalled.
// ============================================================

Core::Bar PipeBroker::get_latest_bar(
    const std::string& /*symbol*/,
    const std::string& /*interval*/)
{
    std::unique_lock<std::mutex> lock(queue_mutex_);

    // Block until a bar is available or shutdown is requested
    bar_cv_.wait(lock, [this] {
        return !bar_queue_.empty() || shutdown_.load();
    });

    if (shutdown_.load() && bar_queue_.empty()) {
        // Return sentinel bar — LiveEngine checks datetime == "" to stop
        LOG_INFO("[PipeBroker:" << symbol_ << "] Shutdown — returning sentinel bar.");
        return Core::Bar{};
    }

    Core::Bar bar = bar_queue_.front();
    bar_queue_.pop();
    latest_bar_ = bar;
    ++bars_consumed_;

    LOG_DEBUG("[PipeBroker:" << symbol_ << "] Bar consumed: "
              << bar.datetime << " C=" << bar.close);
    return bar;
}

// ============================================================
// BrokerInterface — LTP
// ============================================================

double PipeBroker::get_ltp(const std::string& /*symbol*/) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return latest_bar_.close;
}

// ============================================================
// BrokerInterface — historical bars
// ============================================================

std::vector<Core::Bar> PipeBroker::get_historical_bars(
    const std::string& /*symbol*/,
    const std::string& /*interval*/,
    int count)
{
    std::lock_guard<std::mutex> lock(history_mutex_);
    int available = static_cast<int>(bar_history_.size());
    int start     = std::max(0, available - count);
    return std::vector<Core::Bar>(
        bar_history_.begin() + start,
        bar_history_.end());
}

// ============================================================
// BrokerInterface — portfolio pre-filter
// ============================================================

bool PipeBroker::is_entry_allowed() {
    if (!entry_allowed_) return true;
    bool allowed = entry_allowed_();
    if (!allowed) {
        LOG_DEBUG("[PipeBroker:" << symbol_ << "] Entry blocked by PortfolioGuard.");
    }
    return allowed;
}

// ============================================================
// BrokerInterface — connection lifecycle
// ============================================================

bool PipeBroker::connect() {
    connected_.store(true);
    if (!order_submitter_.initialize()) {
        LOG_WARN("[PipeBroker:" << symbol_ << "] OrderSubmitter init failed — HTTP orders will fail.");
    }
    LOG_INFO("[PipeBroker:" << symbol_ << "] Connected (pipe-mode).");
    return true;
}

void PipeBroker::disconnect() {
    connected_.store(false);
    shutdown();
    LOG_INFO("[PipeBroker:" << symbol_ << "] Disconnected.");
}

bool PipeBroker::is_connected() const {
    return connected_.load();
}

// ============================================================
// BrokerInterface — order placement
// ============================================================

OrderResponse PipeBroker::place_market_order(
    const std::string& symbol,
    const std::string& direction,
    int                quantity)
{
    LOG_INFO("[PipeBroker:" << symbol_ << "] Placing MARKET order: "
             << direction << " x" << quantity);

    // Generate order tag  SDT+MMDDHHMMSS+[L/S]
    // LiveEngine generates its own tag — we submit as-is via OrderSubmitter
    // The tag comes through the order metadata set by LiveEngine.
    // For direct calls (e.g. from place_market_order with a pre-built tag),
    // we construct a minimal one here.
    auto now      = std::chrono::system_clock::now();
    // Use typedef to avoid Windows macro conflict with to_time_t
    using clk     = std::chrono::system_clock;
    auto time_val = clk::to_time_t(now);
    std::tm tm_info{};
#ifdef _WIN32
    localtime_s(&tm_info, &time_val);
#else
    localtime_r(&time_val, &tm_info);
#endif
    std::ostringstream tag_ss;
    tag_ss << "SDT"
           << std::setfill('0')
           << std::setw(2) << (tm_info.tm_mon + 1)
           << std::setw(2) << tm_info.tm_mday
           << std::setw(2) << tm_info.tm_hour
           << std::setw(2) << tm_info.tm_min
           << std::setw(2) << tm_info.tm_sec
           << (direction == "BUY" ? "L" : "S");

    // OrderSubmitter::validate_order_params requires "LONG"/"SHORT" not "BUY"/"SELL"
    const std::string submit_direction = (direction == "BUY") ? "LONG" : "SHORT";

    OrderSubmitResult result = order_submitter_.submit_order(
        submit_direction,   // "LONG" or "SHORT"
        quantity,           // lot count
        0,                  // zone_id (not known at this level)
        0.0,                // zone_score
        latest_bar_.close,  // entry_price estimate
        0.0,                // stop_loss (managed by LiveEngine)
        0.0,                // take_profit (managed by LiveEngine)
        tag_ss.str()        // order_tag
    );

    if (result.success) {
        ++orders_sent_;
        double entry_price = latest_bar_.close;  // Best estimate
        if (on_order_ok_) {
            on_order_ok_(quantity, entry_price);  // Notify PortfolioGuard
        }
        LOG_INFO("[PipeBroker:" << symbol_ << "] Order placed OK. tag=" << tag_ss.str());
    } else {
        ++orders_failed_;
        LOG_ERROR("[PipeBroker:" << symbol_ << "] Order FAILED: " << result.error_message);
    }

    return map_result_to_response(result, direction, quantity, latest_bar_.close);
}

OrderResponse PipeBroker::place_limit_order(
    const std::string& symbol,
    const std::string& direction,
    int                quantity,
    double             /*price*/)
{
    // V8 uses market orders only — delegate to market order
    LOG_WARN("[PipeBroker:" << symbol_ << "] Limit order requested — using MARKET.");
    return place_market_order(symbol, direction, quantity);
}

OrderResponse PipeBroker::place_stop_order(
    const std::string& symbol,
    const std::string& direction,
    int                quantity,
    double             /*stop_price*/)
{
    // Stop exits are handled by LiveEngine internally via SL monitoring.
    // Actual exit orders go via Spring Boot squareOff endpoint.
    LOG_WARN("[PipeBroker:" << symbol_ << "] Stop order requested — using MARKET.");
    return place_market_order(symbol, direction, quantity);
}

bool PipeBroker::cancel_order(const std::string& order_id) {
    LOG_WARN("[PipeBroker:" << symbol_ << "] cancel_order not implemented: " << order_id);
    return false;
}

// ============================================================
// BrokerInterface — account / position
// ============================================================

std::vector<Position> PipeBroker::get_positions() {
    std::lock_guard<std::mutex> lock(position_mutex_);
    std::vector<Position> positions;
    if (has_position_) {
        Position p;
        p.symbol        = symbol_;
        p.current_price = latest_bar_.close;
        positions.push_back(p);
    }
    return positions;
}

double PipeBroker::get_account_balance() {
    if (get_balance_) return get_balance_();
    return 0.0;
}

bool PipeBroker::has_position(const std::string& /*symbol*/) {
    std::lock_guard<std::mutex> lock(position_mutex_);
    return has_position_;
}

// ============================================================
// Private helpers
// ============================================================

OrderResponse PipeBroker::map_result_to_response(
    const OrderSubmitResult& result,
    const std::string&       direction,
    int                      quantity,
    double                   price)
{
    OrderResponse resp;
    resp.status         = result.success ? OrderStatus::FILLED : OrderStatus::REJECTED;
    resp.message        = result.success ? "OK" : result.error_message;
    resp.filled_price   = result.success ? price : 0.0;
    resp.filled_quantity = result.success ? quantity : 0;
    return resp;
}

} // namespace Live
} // namespace SDTrading
