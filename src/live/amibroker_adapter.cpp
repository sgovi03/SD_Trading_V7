// ============================================================
// SD TRADING V8 - AMIBROKER DATA ADAPTER IMPLEMENTATION
// src/live/amibroker_adapter.cpp
// Milestone M2.5
// ============================================================

#include "amibroker_adapter.h"
#include <stdexcept>
#include <algorithm>

namespace SDTrading {
namespace Live {

AmiBrokerAdapter::AmiBrokerAdapter(const std::string& symbol,
                                   const std::string& interval)
    : symbol_(symbol), interval_(interval) {
    LOG_INFO("[AmiBrokerAdapter:" << symbol_ << "] Created.");
    LOG_INFO("[AmiBrokerAdapter:" << symbol_ << "]"
             << " Data source: AmiBroker named pipe"
             << " | Execution: OrderSubmitter (Spring Boot)");
}

// ============================================================
// on_bar_validated — called by EventBus on BarValidatedEvent
// ============================================================

void AmiBrokerAdapter::on_bar_validated(const Core::Bar& bar) {
    std::lock_guard<std::mutex> lock(bar_mutex_);

    // Ignore if same timestamp as current bar (intra-bar tick update)
    // LiveEngine handles same-datetime → in-bar update vs new-bar logic
    latest_bar_ = bar;
    has_bar_    = true;

    // Append to history only if it is a new bar
    if (bar_history_.empty() || bar_history_.back().datetime != bar.datetime) {
        bar_history_.push_back(bar);
        // Cap history to last 2000 bars (enough for zone detection lookback)
        if (bar_history_.size() > 2000) {
            bar_history_.pop_front();
        }
    } else {
        // Same bar — update in place (handles intra-bar OHLC updates from AFL)
        bar_history_.back() = bar;
    }

    ++bars_received_;

    LOG_DEBUG("[AmiBrokerAdapter:" << symbol_ << "] Bar received: "
              << bar.datetime << " C=" << bar.close
              << " V=" << bar.volume << " OI=" << bar.oi
              << " (total=" << bars_received_.load() << ")");
}

// ============================================================
// BrokerInterface — Connection
// ============================================================

bool AmiBrokerAdapter::connect() {
    connected_ = true;
    LOG_INFO("[AmiBrokerAdapter:" << symbol_ << "] Connected."
             << " Waiting for bars from AmiBroker pipe...");
    return true;
}

void AmiBrokerAdapter::disconnect() {
    connected_ = false;
    LOG_INFO("[AmiBrokerAdapter:" << symbol_ << "] Disconnected."
             << " Total bars received: " << bars_received_.load());
}

bool AmiBrokerAdapter::is_connected() const {
    return connected_.load();
}

// ============================================================
// get_latest_bar — called by LiveEngine::update_bar_history()
// ============================================================

Core::Bar AmiBrokerAdapter::get_latest_bar(const std::string& /*symbol*/,
                                           const std::string& /*interval*/) {
    std::lock_guard<std::mutex> lock(bar_mutex_);

    if (!has_bar_) {
        LOG_DEBUG("[AmiBrokerAdapter:" << symbol_ << "] No bar yet — returning empty.");
        return Core::Bar{};
    }

    return latest_bar_;
}

// ============================================================
// get_ltp
// ============================================================

double AmiBrokerAdapter::get_ltp(const std::string& /*symbol*/) {
    std::lock_guard<std::mutex> lock(bar_mutex_);
    return has_bar_ ? latest_bar_.close : 0.0;
}

// ============================================================
// get_historical_bars
// ============================================================

std::vector<Core::Bar> AmiBrokerAdapter::get_historical_bars(
    const std::string& /*symbol*/,
    const std::string& /*interval*/,
    int count)
{
    std::lock_guard<std::mutex> lock(bar_mutex_);

    if (bar_history_.empty()) return {};

    int start = static_cast<int>(bar_history_.size()) - count;
    if (start < 0) start = 0;

    return std::vector<Core::Bar>(bar_history_.begin() + start,
                                   bar_history_.end());
}

// ============================================================
// Execution stubs — NOT used by AmiBrokerAdapter
// All order execution flows through OrderSubmitter (Spring Boot).
// These methods raise clear errors if accidentally called.
// ============================================================

OrderResponse AmiBrokerAdapter::place_market_order(
    const std::string& /*symbol*/,
    const std::string& /*direction*/,
    int /*quantity*/)
{
    LOG_ERROR("[AmiBrokerAdapter] place_market_order() called — "
              "this adapter is DATA ONLY. Use OrderSubmitter for execution.");
    OrderResponse r;
    r.status  = OrderStatus::REJECTED;
    r.message = "AmiBrokerAdapter is data-only. Route orders through OrderSubmitter.";
    return r;
}

OrderResponse AmiBrokerAdapter::place_limit_order(
    const std::string& /*symbol*/,
    const std::string& /*direction*/,
    int /*quantity*/,
    double /*price*/)
{
    LOG_ERROR("[AmiBrokerAdapter] place_limit_order() called — data-only adapter.");
    OrderResponse r;
    r.status  = OrderStatus::REJECTED;
    r.message = "AmiBrokerAdapter is data-only.";
    return r;
}

OrderResponse AmiBrokerAdapter::place_stop_order(
    const std::string& /*symbol*/,
    const std::string& /*direction*/,
    int /*quantity*/,
    double /*stop_price*/)
{
    LOG_ERROR("[AmiBrokerAdapter] place_stop_order() called — data-only adapter.");
    OrderResponse r;
    r.status  = OrderStatus::REJECTED;
    r.message = "AmiBrokerAdapter is data-only.";
    return r;
}

bool AmiBrokerAdapter::cancel_order(const std::string& /*order_id*/) {
    LOG_ERROR("[AmiBrokerAdapter] cancel_order() called — data-only adapter.");
    return false;
}

std::vector<Position> AmiBrokerAdapter::get_positions() {
    LOG_WARN("[AmiBrokerAdapter] get_positions() called on data-only adapter.");
    return {};
}

double AmiBrokerAdapter::get_account_balance() {
    LOG_WARN("[AmiBrokerAdapter] get_account_balance() called on data-only adapter.");
    return 0.0;
}

bool AmiBrokerAdapter::has_position(const std::string& /*symbol*/) {
    return false;
}

} // namespace Live
} // namespace SDTrading
