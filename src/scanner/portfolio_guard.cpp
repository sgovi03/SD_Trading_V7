// ============================================================
// SD TRADING V8 - PORTFOLIO GUARD IMPLEMENTATION
// src/scanner/portfolio_guard.cpp
// ============================================================

#include "scanner/portfolio_guard.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cmath>

namespace SDTrading {
namespace Scanner {

using namespace Utils;

// ============================================================
// Construction
// ============================================================

PortfolioGuard::PortfolioGuard(const PortfolioConfig& cfg)
    : cfg_               (cfg)
    , capital_available_ (cfg.starting_capital)
{
    LOG_INFO("[PortfolioGuard] Created."
             << " capital=₹" << cfg_.starting_capital
             << " risk_pct="  << cfg_.risk_per_trade_pct
             << " max_lots="  << cfg_.max_position_lots
             << " daily_limit=" << cfg_.daily_loss_limit_pct << "%");
}

// ============================================================
// Pre-filter
// ============================================================

bool PortfolioGuard::entry_allowed(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Daily loss limit
    if (daily_limit_hit_) {
        LOG_INFO("[PortfolioGuard] Entry BLOCKED [" << symbol
                 << "] — daily loss limit hit. daily_pnl=₹" << daily_pnl_);
        return false;
    }

    // 2. Symbol already has open position
    auto it = positions_.find(symbol);
    if (it != positions_.end() && it->second.is_open) {
        LOG_DEBUG("[PortfolioGuard] Entry BLOCKED [" << symbol
                  << "] — position already open.");
        return false;
    }

    // 3. Capital exhausted (less than 1 lot worth of risk)
    if (capital_available_ <= 0.0) {
        LOG_WARN("[PortfolioGuard] Entry BLOCKED [" << symbol
                 << "] — capital exhausted.");
        return false;
    }

    return true;
}

// ============================================================
// Lot size calculation — EV-weighted
// ============================================================

int PortfolioGuard::calculate_lots(
    const std::string& symbol,
    double             score,
    double             rr_ratio,
    double             sl_distance,
    double             lot_value) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (sl_distance <= 0.0 || lot_value <= 0.0) {
        LOG_WARN("[PortfolioGuard:" << symbol << "] Invalid sl_distance or lot_value "
                 << "— defaulting to 1 lot.");
        return 1;
    }

    // Risk amount from available capital
    double risk_amount = capital_available_ * cfg_.risk_per_trade_pct / 100.0;

    // Base lots from pure risk
    double raw_lots = risk_amount / (sl_distance * lot_value);

    // EV-weighted multiplier
    // expected_value = (score/100) x rr_ratio
    // baseline_ev    = baseline_score x baseline_rr
    // score_mult     = EV / baseline_EV
    //
    // Example:
    //   score=75, rr=2.5 → EV=1.875, baseline=1.1375 → mult=1.65 → more lots
    //   score=65, rr=1.4 → EV=0.910, baseline=1.1375 → mult=0.80 → fewer lots

    double ev       = (score / 100.0) * rr_ratio;
    double base_ev  = baseline_ev();
    double mult     = (base_ev > 0.0) ? (ev / base_ev) : 1.0;

    double adjusted = raw_lots * mult;

    // Clamp to [1, max_position_lots]
    int final_lots = static_cast<int>(std::round(adjusted));
    final_lots = std::max(1, std::min(final_lots, cfg_.max_position_lots));

    LOG_INFO("[PortfolioGuard:" << symbol << "] Lot calc:"
             << " capital=₹" << std::fixed << std::setprecision(0) << capital_available_
             << " risk=₹"    << risk_amount
             << " sl="       << sl_distance
             << " score="    << score
             << " rr="       << rr_ratio
             << " ev="       << std::setprecision(3) << ev
             << " mult="     << mult
             << " raw="      << raw_lots
             << " final="    << final_lots << " lots");

    return final_lots;
}

// ============================================================
// Position lifecycle
// ============================================================

void PortfolioGuard::notify_position_opened(
    const std::string& symbol,
    const std::string& direction,
    int                lots,
    double             entry_price)
{
    std::lock_guard<std::mutex> lock(mutex_);

    PositionState& ps = positions_[symbol];
    ps.is_open      = true;
    ps.lots         = lots;
    ps.entry_price  = entry_price;
    ps.direction    = direction;
    ps.unrealised_pnl = 0.0;

    LOG_INFO("[PortfolioGuard] Position OPENED: " << symbol
             << " " << direction
             << " lots=" << lots
             << " entry=₹" << entry_price
             << " | capital=₹" << capital_available_
             << " open_positions=" << open_position_count());
}

void PortfolioGuard::notify_position_closed(
    const std::string& symbol,
    double             exit_price,
    double             realised_pnl)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = positions_.find(symbol);
    if (it != positions_.end()) {
        it->second.is_open = false;
        it->second.lots    = 0;
    }

    // Update capital and daily P&L
    capital_available_ += realised_pnl;
    daily_pnl_         += realised_pnl;

    // Check daily loss limit
    double daily_loss_limit = cfg_.starting_capital
                              * cfg_.daily_loss_limit_pct / 100.0;
    if (daily_pnl_ < 0.0 && std::abs(daily_pnl_) >= daily_loss_limit) {
        daily_limit_hit_ = true;
        LOG_WARN("[PortfolioGuard] DAILY LOSS LIMIT HIT."
                 << " daily_pnl=₹" << daily_pnl_
                 << " limit=₹" << daily_loss_limit
                 << " — all new entries blocked for today.");
    }

    LOG_INFO("[PortfolioGuard] Position CLOSED: " << symbol
             << " exit=₹" << exit_price
             << " pnl=₹"  << realised_pnl
             << " | capital=₹" << capital_available_
             << " daily_pnl=₹" << daily_pnl_);
}

// ============================================================
// Daily reset
// ============================================================

void PortfolioGuard::reset_daily_pnl() {
    std::lock_guard<std::mutex> lock(mutex_);
    daily_pnl_      = 0.0;
    daily_limit_hit_ = false;
    LOG_INFO("[PortfolioGuard] Daily P&L reset. capital=₹" << capital_available_);
}

// ============================================================
// Accessors
// ============================================================

double PortfolioGuard::capital_available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capital_available_;
}

double PortfolioGuard::daily_pnl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return daily_pnl_;
}

bool PortfolioGuard::daily_limit_hit() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return daily_limit_hit_;
}

int PortfolioGuard::open_position_count() const {
    // Caller must hold mutex_ when calling from other locked methods,
    // so we use a non-locking version here.
    int count = 0;
    for (const auto& [sym, ps] : positions_) {
        if (ps.is_open) ++count;
    }
    return count;
}

bool PortfolioGuard::has_open_position(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = positions_.find(symbol);
    return it != positions_.end() && it->second.is_open;
}

std::string PortfolioGuard::status_string() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(0);
    ss << "capital=₹" << capital_available_
       << " daily_pnl=₹" << daily_pnl_
       << " limit_hit=" << (daily_limit_hit_ ? "YES" : "NO")
       << " open_positions=" << open_position_count();
    for (const auto& [sym, ps] : positions_) {
        if (ps.is_open) {
            ss << " [" << sym << " " << ps.direction
               << " x" << ps.lots << "]";
        }
    }
    return ss.str();
}

} // namespace Scanner
} // namespace SDTrading
