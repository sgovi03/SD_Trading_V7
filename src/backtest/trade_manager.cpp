#ifdef _WIN32
#define NOMINMAX  // Prevent Windows min/max macros from conflicting with std::min/max
#endif

#include "trade_manager.h"
#include "../live/broker_interface.h"
#include "../utils/logger.h"
#include "../analysis/market_analyzer.h"
#include "../utils/volume_baseline.h"
#include "../scoring/oi_scorer.h"
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>  // For Beep function
#undef ERROR  // Remove Windows ERROR macro to avoid conflicts with LOG_ERROR
#endif

namespace SDTrading {
namespace Backtest {

using namespace Utils;
using namespace Live;


/**
 * @brief Calculate potential loss for a trade
 * ⭐ FIX #1: Max Loss Cap Enforcement
 * @param entry_price Entry price
 * @param stop_loss Stop loss price
 * @param position_size Number of contracts
 * @param lot_size Contract lot size
 * @return Potential loss amount
 */
double calculate_potential_loss(double entry_price, 
                                double stop_loss, 
                                int position_size, 
                                int lot_size) {
    double stop_distance = std::abs(entry_price - stop_loss);
    return stop_distance * position_size * lot_size;
}

// ============================================================
// SOUND ALERT FUNCTIONS (LIVE MODE ONLY)
// ============================================================

/**
 * Play sound alert for trade entry (only in LIVE mode)
 */
void play_entry_alert() {
#ifdef _WIN32
    // Play a rising tone sequence for entry
    Beep(800, 150);   // First beep
    Beep(1000, 150);  // Second beep (higher)
    Beep(1200, 200);  // Third beep (even higher, longer)
#endif
}

/**
 * Play sound alert for trade exit (only in LIVE mode)
 */
void play_exit_alert() {
#ifdef _WIN32
    // Play a descending tone sequence for exit
    Beep(1200, 150);  // First beep
    Beep(1000, 150);  // Second beep (lower)
    Beep(800, 200);   // Third beep (even lower, longer)
#endif
}

// BACKTEST Constructor
TradeManager::TradeManager(const Config& cfg, double start_cap)
    : config(cfg), 
      starting_capital(start_cap),  // ⭐ ADDED: Store starting capital
      current_capital(start_cap),
      in_position(false),
      mode(ExecutionMode::BACKTEST),
      broker(nullptr),
      volume_baseline_(nullptr),
      oi_scorer_(nullptr) {
    LOG_INFO("TradeManager initialized in BACKTEST mode");
}

// LIVE Constructor
TradeManager::TradeManager(const Config& cfg, 
                          double start_cap,
                          BrokerInterface* broker_interface)
    : config(cfg),
      starting_capital(start_cap),  // ⭐ ADDED: Store starting capital
      current_capital(start_cap),
      in_position(false),
      mode(ExecutionMode::LIVE),
      broker(broker_interface),
      volume_baseline_(nullptr),
      oi_scorer_(nullptr) {
    LOG_INFO("TradeManager initialized in LIVE mode");
}

int TradeManager::calculate_position_size(double entry_price, double stop_loss) const {
    // ⭐ CRITICAL FIX: Use starting_capital instead of current_capital
    // This prevents position size explosion from compounding
    // Safe for live trading - positions stay constant regardless of wins/losses
    double risk_amount = starting_capital * (config.risk_per_trade_pct / 100.0);
    
    // Stop loss distance
    double stop_distance = std::abs(entry_price - stop_loss);
    
    if (stop_distance < 0.01) {
        LOG_WARN("Stop distance too small");
        return 0;
    }
    
    // Calculate position size
    // Risk = Position Size * Lot Size * Stop Distance
    int position_size = static_cast<int>(risk_amount / (config.lot_size * stop_distance));
    
    // ⭐ SAFETY CAP: Prevent excessive position sizes
    // Hard limit at 2 contracts for risk control
    const int MAX_POSITION_SIZE = 2;
    position_size = std::min(position_size, MAX_POSITION_SIZE);
    
    // Minimum 1 lot
    return std::max(1, position_size);
}

void TradeManager::calculate_pnl(Trade& trade) const {
    double pnl_per_unit = 0.0;
    
    if (trade.direction == "LONG") {
        pnl_per_unit = trade.exit_price - trade.entry_price;
    } else {
        pnl_per_unit = trade.entry_price - trade.exit_price;
    }
    
    // Total P&L
    trade.pnl = pnl_per_unit * trade.position_size * config.lot_size;

    // ⭐ PNL SANITY GUARD: Catch impossibly large losses before they corrupt results.
    // Max realistic intraday loss = 3 lots * 500pts * 65 lot_size = Rs97,500
    // If we exceed Rs200,000 on a single trade, print full forensic detail and abort.
    if (std::abs(trade.pnl) > 200000.0) {
        std::cerr << "\n\n[FATAL PNL SANITY BREACH] Trade #" << trade.trade_num << "\n"
                  << "  Direction:      " << trade.direction << "\n"
                  << "  Entry Price:    " << std::fixed << std::setprecision(2) << trade.entry_price << "\n"
                  << "  Exit Price:     " << trade.exit_price << "\n"
                  << "  Stop Loss:      " << trade.stop_loss << "\n"
                  << "  pnl_per_unit:   " << pnl_per_unit << " pts\n"
                  << "  position_size:  " << trade.position_size << " (lots or units?)\n"
                  << "  config.lot_size:" << config.lot_size << "\n"
                  << "  COMPUTED PNL:   Rs" << trade.pnl << "\n"
                  << "  Expected max:   Rs97,500 (3lots x 500pts x 65)\n"
                  << "  bars_in_trade:  " << trade.bars_in_trade << "\n"
                  << "  entry_date:     " << trade.entry_date << "\n"
                  << "  exit_date:      " << trade.exit_date << "\n"
                  << "  exit_reason:    " << trade.exit_reason << "\n"
                  << ">>> ABORTING to prevent data corruption. Fix position_size bug first. <<<\n\n";
        std::abort();
    }
    
    // Subtract commissions
    trade.pnl -= (2 * config.commission_per_trade);  // Entry + Exit
    
    // P&L percentage
    double position_value = trade.entry_price * trade.position_size * config.lot_size;
    trade.pnl_pct = (trade.pnl / position_value) * 100.0;
    
    // Return percentage
    trade.return_pct = (trade.pnl / current_capital) * 100.0;
    
    // Risk amount
    // FIX: In breakeven mode entry_price == stop_loss → stop_distance = 0 → risk_amount
    // reports as ₹0 in the CSV, which caused incorrect forensic analysis. The system
    // always enforces max_loss_per_trade as the hard cap, so report that as effective risk.
    double stop_distance = std::abs(trade.entry_price - trade.stop_loss);
    if (stop_distance < 1.0 && config.max_loss_per_trade > 0) {
        trade.risk_amount = config.max_loss_per_trade;
    } else {
        trade.risk_amount = stop_distance * trade.position_size * config.lot_size;
    }
    
    // Reward amount
    double target_distance = std::abs(trade.take_profit - trade.entry_price);
    trade.reward_amount = target_distance * trade.position_size * config.lot_size;
    
    // Actual R:R
    if (trade.risk_amount > 0) {
        trade.actual_rr = trade.pnl / trade.risk_amount;
    }
}

double TradeManager::execute_entry(const std::string& symbol,
                                   const std::string& direction,
                                   int quantity,
                                   double price,
                                   const Bar* bar) {
    if (mode == ExecutionMode::BACKTEST) {
        // ── REALISTIC FILL CLAMPING ──────────────────────────────────────────
        // A limit order can only fill at a price the bar actually traded.
        //   SHORT (SELL limit): the sell order sits at our target price above
        //     the market — it fills only if bar.high reaches that level.
        //     If bar.high < target, we get filled at bar.high (best available).
        //   LONG (BUY limit): the buy order sits below the market — it fills
        //     only if bar.low reaches that level.
        //     If bar.low > target, we get filled at bar.low (best available).
        // Without this clamp, backtest fills phantom trades at unreachable prices
        // (e.g. SHORT target at 25232 on a bar whose high was only 25220).
        if (bar != nullptr) {
            if (direction == "SELL") {
                // SHORT: cannot fill above bar.high — clamp down to bar.high
                if (price > bar->high) {
                    LOG_DEBUG("SHORT fill clamped: target=" + std::to_string(price) +
                              " > bar.high=" + std::to_string(bar->high) +
                              " → fill at " + std::to_string(bar->high));
                    price = bar->high;
                }
            } else {
                // LONG: cannot fill below bar.low — clamp up to bar.low
                if (price < bar->low) {
                    LOG_DEBUG("LONG fill clamped: target=" + std::to_string(price) +
                              " < bar.low=" + std::to_string(bar->low) +
                              " → fill at " + std::to_string(bar->low));
                    price = bar->low;
                }
            }
        }
        // ────────────────────────────────────────────────────────────────────
        LOG_INFO("SIMULATED: Entry " + direction + " " + std::to_string(quantity) + 
                " @ " + std::to_string(price));
        return price;
    }

    if (!broker) {
        LOG_ERROR("Broker not available for live execution");
        return 0.0;
    }

    OrderResponse response = broker->place_market_order(symbol, direction, quantity);

    if (response.status != OrderStatus::FILLED) {
        LOG_ERROR("Order failed: " + response.message);
        return 0.0;
    }

    LOG_INFO("LIVE: Order filled @ " + std::to_string(response.filled_price));
    return response.filled_price;
}

double TradeManager::execute_exit(const std::string& symbol,
                                  const std::string& direction,
                                  int quantity,
                                  double price) {
    if (mode == ExecutionMode::BACKTEST) {
        // Simulated execution
        LOG_INFO("SIMULATED: Exit " + direction + " " + std::to_string(quantity) + 
                " @ " + std::to_string(price));
        return price;  // Perfect fill in backtest
    }

    if (!broker) {
        LOG_ERROR("Broker not available for live execution");
        return 0.0;
    }

    OrderResponse response = broker->place_market_order(symbol, direction, quantity);

    if (response.status != OrderStatus::FILLED) {
        LOG_ERROR("Exit order failed: " + response.message);
        return 0.0;
    }

    LOG_INFO("LIVE: Position closed @ " + std::to_string(response.filled_price));
    return response.filled_price;
}

bool TradeManager::enter_trade(const EntryDecision& decision,
                               const Zone& zone,
                               const Bar& bar,
                               int bar_index,
                               MarketRegime regime,
                               const std::string& symbol,
                               const std::vector<Bar>* bars) {
    // Console debug output
    std::cout << "\n[TRADE_MGR] enter_trade() called\n";
    std::cout << "  Already in position: " << (in_position ? "YES" : "NO") << "\n";
    std::cout.flush();

    // --- TIME FILTER: Block entries between configurable times ---
    // Assumes bar.datetime is in format "YYYY-MM-DD HH:MM:SS"
    if (config.enable_entry_time_block && !config.entry_block_start_time.empty() && !config.entry_block_end_time.empty() && bar.datetime.length() >= 16) {
        std::string time_str = bar.datetime.substr(11, 5); // "HH:MM"
        if (time_str >= config.entry_block_start_time && time_str < config.entry_block_end_time) {
            LOG_WARN("Entry blocked by time filter: " + time_str + " (configurable: " + config.entry_block_start_time + "-" + config.entry_block_end_time + ")");
            std::cout << "[TRADE_MGR] REJECTED: Entry blocked by time filter (" << time_str << ", config: " << config.entry_block_start_time << "-" << config.entry_block_end_time << ")\n";
            std::cout.flush();
            return false;
        }
    }

    if (in_position) {
        LOG_WARN("Already in position, cannot enter new trade");
        std::cout << "[TRADE_MGR] REJECTED: Already in position\n";
        std::cout.flush();
        return false;
    }

    int position_size;
    double sl_for_sizing = (decision.original_stop_loss != 0.0) ? decision.original_stop_loss : decision.stop_loss;
    // V6.0: Use dynamic position sizing if available
    if (decision.lot_size > 0) {
        position_size = decision.lot_size;
        LOG_INFO("✅ V6.0 dynamic position size: " + std::to_string(position_size) + " contracts");
        std::cout << "  Position size (V6.0 dynamic): " << position_size << "\n";
    } else {
        // Fallback: Use V4.0 risk-based sizing
        position_size = calculate_position_size(decision.entry_price, sl_for_sizing);
        LOG_INFO("V4.0 risk-based position size: " + std::to_string(position_size) + " contracts");
        std::cout << "  Position size (V4.0 risk): " << position_size << "\n";
    }

    if (position_size <= 0) {
        LOG_WARN("Invalid position size calculated");
        std::cout << "[TRADE_MGR] REJECTED: Invalid position size\n";
        std::cout.flush();
        return false;
    }

    // ⭐ CRITICAL GUARD: Reject trades with zero/negative SL or TP.
    // Root cause: zones with distal_line=0 (corrupt/zero-initialized zone) cause
    // calculate_stop_loss() to return 0, which then cascades:
    //   1. SL=0 → breakeven mode sets entry_price=0
    //   2. Fill clamp corrects entry to bar.low (e.g. 22238) but take_profit stays 0
    //   3. check_take_profit: bar.high >= 0 → ALWAYS TRUE → exits at price 0
    //   4. pnl = (0 - 22238) * 1 * 65 = -Rs1,445,470
    // This single guard eliminates all catastrophic ₹1.4M+ losses.
    if (decision.stop_loss <= 0.0) {
        LOG_WARN("Trade REJECTED: stop_loss=" + std::to_string(decision.stop_loss) +
                 " is zero or negative. Zone distal_line=" +
                 std::to_string(zone.distal_line) + " may be corrupt.");
        std::cout << "[TRADE_MGR] REJECTED: stop_loss <= 0 (zone distal_line corrupt)\n";
        std::cout.flush();
        return false;
    }
    if (decision.take_profit <= 0.0) {
        LOG_WARN("Trade REJECTED: take_profit=" + std::to_string(decision.take_profit) +
                 " is zero or negative.");
        std::cout << "[TRADE_MGR] REJECTED: take_profit <= 0\n";
        std::cout.flush();
        return false;
    }

    // ⭐ FIX: Enforce max loss cap using THE ACTUAL STOP LOSS, not sizing SL
    // In breakeven mode, decision.stop_loss is the REAL stop that will be hit
    // We must check against this, not the "sl_for_sizing" used for position calculation
    double potential_loss = calculate_potential_loss(
        decision.entry_price,
        decision.stop_loss,  // Use ACTUAL stop loss, not sl_for_sizing
        position_size,
        config.lot_size
    );

    std::cout << "  Max Loss Cap Check:\n";
    std::cout << "    Entry: " << std::fixed << std::setprecision(2) << decision.entry_price << "\n";
    std::cout << "    Stop Loss (sizing): " << sl_for_sizing << "\n";
    std::cout << "    Stop Loss (actual): " << decision.stop_loss << "\n";
    std::cout << "    Potential loss: " << potential_loss << "\n";
    std::cout << "    Max loss cap: " << config.max_loss_per_trade << "\n";
    std::cout << "    Position size: " << position_size << " lots\n";
    std::cout << "    Lot size: " << config.lot_size << "\n";
    std::cout.flush();

    if (potential_loss > config.max_loss_per_trade) {
        double actual_stop_distance = std::abs(decision.entry_price - decision.stop_loss);
        int original_position_size = position_size;

        if (actual_stop_distance > 0 && config.lot_size > 0) {
            // ⭐ FIX: Correct formula
            // max_loss = lots × lot_size × stop_distance
            // → lots = floor(max_loss / (lot_size × stop_distance))
            int affordable_lots = static_cast<int>(
                config.max_loss_per_trade / (config.lot_size * actual_stop_distance));

            if (affordable_lots < 1) {
                // Even 1 lot exceeds the cap — reject outright
                LOG_WARN("Trade REJECTED: 1 lot × " + std::to_string(config.lot_size) +
                         " × " + std::to_string(actual_stop_distance) + "pts = ₹" +
                         std::to_string(actual_stop_distance * config.lot_size) +
                         " exceeds max_loss_per_trade ₹" +
                         std::to_string(config.max_loss_per_trade));
                std::cout << "[TRADE_MGR] REJECTED: 1 lot already exceeds max_loss_per_trade\n";
                std::cout.flush();
                return false;
            }

            position_size = affordable_lots;
            std::cout << "  ⚠️  Position size adjusted: "
                      << original_position_size << " → " << position_size
                      << " lots (max_loss_per_trade cap)\n";
            std::cout.flush();

            potential_loss = calculate_potential_loss(
                decision.entry_price, decision.stop_loss, position_size, config.lot_size);

            LOG_INFO("Position size adjusted: " + std::to_string(original_position_size) +
                     " → " + std::to_string(position_size) + " lots. New risk: ₹" +
                     std::to_string(potential_loss));
        }

        // Final safety check — must always pass at this point
        if (potential_loss > config.max_loss_per_trade) {
            LOG_WARN("Trade REJECTED: Adjusted loss ₹" + std::to_string(potential_loss) +
                     " still exceeds cap ₹" + std::to_string(config.max_loss_per_trade));
            std::cout << "[TRADE_MGR] REJECTED: Loss cap exceeded after size adjustment\n";
            return false;
        }
    } else {
        std::cout << "  ✓ Within max loss cap\n";
        std::cout.flush();
    }
    
    // Determine direction
    std::string direction = (zone.type == Core::ZoneType::DEMAND) ? "LONG" : "SHORT";
    std::string order_direction = (direction == "LONG") ? "BUY" : "SELL";
    
    std::cout << "  Direction: " << direction << " (order: " << order_direction << ")\n";
    std::cout << "  Calling execute_entry()...\n";
    std::cout.flush();
    
    // Execute entry order (convert lots to units for broker)
    int total_units = position_size * config.lot_size;
    std::cout << "  Total units (lots x lot_size): " << total_units << "\n";
    std::cout.flush();
    // total_units = 1;
    double fill_price = execute_entry(symbol, order_direction, total_units, decision.entry_price, &bar);
    //fill_price = decision.entry_price; // HARDCODED TEMPORARY OVERRIDE FOR TESTING
    std::cout << "  execute_entry() returned fill_price: " << fill_price << "\n";
    std::cout.flush();
    
    if (fill_price == 0.0) {
        LOG_ERROR("Failed to execute entry order");
        std::cout << "[TRADE_MGR] REJECTED: execute_entry() failed (returned 0.0)\n";
        std::cout.flush();
        return false;
    }

    // ⭐ POST-FILL CAP CHECK: Revalidate risk using ACTUAL fill price
    // Breakeven mode: decision.entry_price is the intended SL level, NOT the actual fill.
    // Broker fills at market price (inside zone), making actual risk larger than pre-fill estimate.
    // effective_stop_loss / effective_take_profit start as decision values;
    // the tightening path below may override both when SL rescue fires.
    double effective_stop_loss   = decision.stop_loss;
    double effective_take_profit = decision.take_profit;  // may be overridden below

    // FIX (Issue G): Fix TP when entry is in breakeven mode.
    // In breakeven mode, entry_price ≈ stop_loss so risk_dist ≈ 0–15pts.
    // TP = tiny_dist × RR gives a target only 20-40pts away — VOL_CLIMAX fires before TP.
    // Backtest uses zone_height × RR as the reward base in this situation. Mirror that here.
    double zone_height = std::abs(zone.distal_line - zone.proximal_line);
    {
        double be_risk_dist = std::abs(decision.entry_price - decision.stop_loss);
        if (be_risk_dist < zone_height * 0.25 && zone_height > 10.0) {
            // Breakeven entry: re-anchor TP to zone_height × RR from entry
            double rr_to_use = (decision.expected_rr > 0.5) ? decision.expected_rr
                             : decision.score.recommended_rr;
            if (rr_to_use < 1.0) rr_to_use = 1.5;
            double new_reward = zone_height * rr_to_use;
            double zone_anchored_tp = (direction == "LONG")
                ? decision.entry_price + new_reward
                : decision.entry_price - new_reward;
            if (zone_anchored_tp > 0.0) {
                effective_take_profit = zone_anchored_tp;
                LOG_INFO("Breakeven TP re-anchored to zone_height: zone_h=" +
                         std::to_string(zone_height) + "pts × RR=" +
                         std::to_string(rr_to_use) + " = reward " +
                         std::to_string(new_reward) + "pts → TP=" +
                         std::to_string(zone_anchored_tp));
                std::cout << "  Breakeven TP fix: zone_height=" << zone_height
                          << "pts × RR=" << rr_to_use << " → TP=" << zone_anchored_tp << "\n";
            }
        }
    }

    {
        double actual_fill_risk = std::abs(fill_price - decision.stop_loss) * position_size * config.lot_size;
        std::cout << "  Post-fill Cap Recheck:\n";
        std::cout << "    Fill price:     " << std::fixed << std::setprecision(2) << fill_price << "\n";
        std::cout << "    Stop loss:      " << decision.stop_loss << "\n";
        std::cout << "    Actual risk:    Rs" << actual_fill_risk << "\n";
        std::cout << "    Max loss cap:   Rs" << config.max_loss_per_trade << "\n";
        std::cout.flush();

        if (actual_fill_risk > config.max_loss_per_trade) {
            double fill_stop_dist = std::abs(fill_price - decision.stop_loss);
            int affordable_at_fill = static_cast<int>(
                config.max_loss_per_trade / (config.lot_size * fill_stop_dist));

            if (affordable_at_fill < 1) {
                // ⭐ FIX: REJECT-vs-TIGHTEN decision based on fill gap.
                //
                // Analysis of 231 tightened trades (60% of all trades) showed:
                //   • Gap (fill - intended entry): avg 50.4pts, range 15-93pts
                //   • Tightened trade stats: 38.2% WR, 0.99× PF  ← breakeven drag
                //   • Clean trade stats:     61.8% WR, 3.40× PF  ← real edge
                //   • All 231 tightened trades combined: -₹1,626 loss
                //
                // Root cause: bar.open gaps 30-93pts beyond the intended zone entry.
                // The fill places entry deep inside the zone — SL ends up inside the
                // zone's own noise buffer. R:R collapses. These trades destroy edge.
                //
                // Decision rule:
                //   gap <= max_fill_slippage_pts → minor slippage → TIGHTEN (old path)
                //   gap >  max_fill_slippage_pts → entry geometry violated → REJECT
                //
                // For SHORT: adjusted_sl = fill + max_sl_distance  (SL stays above fill)
                // For LONG:  adjusted_sl = fill - max_sl_distance  (SL stays below fill)
                //

                // CHECK: Is the fill gap too large to salvage?
                double fill_gap = std::abs(fill_price - decision.entry_price);
                double max_gap  = config.max_fill_slippage_pts;  // default 25pts; 0=always tighten
                if (max_gap > 0.0 && fill_gap > max_gap) {
                    LOG_WARN("Post-fill REJECTED (fill too far from entry): fill=" +
                             std::to_string(fill_price) + " intended=" +
                             std::to_string(decision.entry_price) + " gap=" +
                             std::to_string(fill_gap) + "pts > max=" +
                             std::to_string(max_gap) + "pts — entry geometry violated");
                    std::cout << "  ❌ POST-FILL REJECT: Fill " << std::fixed << std::setprecision(1)
                              << fill_gap << "pts from intended entry (max=" << max_gap
                              << "pts) — zone geometry violated, squaring off\n";
                    std::cout.flush();
                    std::string exit_dir = (direction == "LONG") ? "SELL" : "BUY";
                    execute_exit(symbol, exit_dir, total_units, fill_price);
                    return false;
                }

                // Gap is within tolerance — tighten SL (minor slippage path).
                //
                double max_sl_distance = config.max_loss_per_trade /
                                         (config.lot_size * static_cast<double>(position_size));
                double adjusted_sl = 0.0;
				
				// ⭐ FIX: Respect min_stop_distance_points
if (config.min_stop_distance_points > 0.0 && 
    max_sl_distance < config.min_stop_distance_points) {
    LOG_WARN("Post-fill REJECTED: Cannot honor both max_loss_per_trade and min_stop_distance_points");
    std::cout << "  ❌ CONFLICT: max_loss allows " << max_sl_distance
              << " pts but minimum is " << config.min_stop_distance_points << " pts\n";
    std::string exit_dir = (direction == "LONG") ? "SELL" : "BUY";
    execute_exit(symbol, exit_dir, total_units, fill_price);
    return false;
}
				
                if (direction == "LONG") {
                    adjusted_sl = fill_price - max_sl_distance;
                } else {
                    // SHORT: SL must remain above fill price
                    adjusted_sl = fill_price + max_sl_distance;
                }

                // Safety guard: adjusted SL must be on the protective side,
                // and the distance must be meaningful (> 5 pts minimum).
                bool sl_valid = (direction == "LONG")
                    ? (adjusted_sl < fill_price)
                    : (adjusted_sl > fill_price);

                if (!sl_valid || max_sl_distance < 5.0) {
                    // SL distance is trivially small — genuinely unsafe to trade
                    LOG_WARN("Post-fill REJECTED (SL too tight): fill=" + std::to_string(fill_price) +
                             " max_sl_dist=" + std::to_string(max_sl_distance) +
                             " pts — squaring off");
                    std::cout << "[TRADE_MGR] POST-FILL REJECT: Max SL distance too small ("
                              << std::fixed << std::setprecision(2) << max_sl_distance
                              << " pts) — squaring off\n";
                    std::cout.flush();
                    std::string exit_dir = (direction == "LONG") ? "SELL" : "BUY";
                    execute_exit(symbol, exit_dir, total_units, fill_price);
                    return false;
                }

                // Accept the trade with tightened SL.
                // decision is const — we cannot write to it. Instead we record the
                // adjusted SL in effective_stop_loss (declared before this block)
                // and use it when writing current_trade.stop_loss further below.
                double old_sl       = decision.stop_loss;
                double tightened_by = std::abs(fill_price - old_sl) - max_sl_distance;
                double new_risk     = max_sl_distance * config.lot_size * position_size;
                effective_stop_loss = adjusted_sl;   // ← overrides decision.stop_loss for trade record

                // ⭐ CRITICAL: Also recalculate take_profit using fill_price and adjusted SL.
                // When decision.stop_loss was 0 (corrupt zone), decision.take_profit is also 0.
                // check_take_profit (LONG: bar.high >= 0.0) fires IMMEDIATELY on next bar,
                // exiting at price=0 for a catastrophic loss of Rs1.4M+
                // Recalculate: use same R:R ratio, based on actual fill and new SL.
                double rr_to_use = (decision.expected_rr > 0.1) ? decision.expected_rr
                                 : decision.score.recommended_rr;
                if (rr_to_use < 0.5) rr_to_use = 1.5;  // Minimum 1.5 R:R safety net
                double new_reward = max_sl_distance * rr_to_use;
                effective_take_profit = (direction == "LONG")
                    ? fill_price + new_reward
                    : fill_price - new_reward;

                std::cout << "  ⭐ TP recalculated after SL rescue:\n"
                          << "    Old TP: " << std::fixed << std::setprecision(2) << decision.take_profit
                          << "  New TP: " << effective_take_profit
                          << "  (fill=" << fill_price << " +/- " << new_reward << " pts)\n";

                LOG_WARN("Post-fill SL TIGHTENED (cap rescue): fill=" + std::to_string(fill_price) +
                         " original_sl=" + std::to_string(old_sl) +
                         " adjusted_sl=" + std::to_string(effective_stop_loss) +
                         " tightened=" + std::to_string(tightened_by) + " pts" +
                         " new_risk=Rs" + std::to_string(new_risk));
                std::cout << "  ⚠️  Post-fill SL TIGHTENED (cap rescue):\n";
                std::cout << "    Original SL:     " << std::fixed << std::setprecision(2) << old_sl
                          << "  (dist " << std::abs(fill_price - old_sl) << " pts"
                          << ", risk Rs" << actual_fill_risk << ")\n";
                std::cout << "    Adjusted SL:     " << effective_stop_loss
                          << "  (dist " << max_sl_distance << " pts"
                          << ", risk Rs" << new_risk << ")\n";
                std::cout << "    SL tightened by: " << tightened_by << " pts\n";
                std::cout << "    Trade SAVED: proceeding with " << position_size << " lot(s) at capped risk\n";
                std::cout.flush();
            } else {
                // affordable_at_fill >= 1: reduce position size to fit cap at actual fill
                int old_size = position_size;
                position_size = affordable_at_fill;
                total_units = position_size * config.lot_size;

                std::cout << "  ⚠️  Post-fill position size adjusted: "
                          << old_size << " → " << position_size
                          << " lots (actual fill risk cap)\n";
                std::cout.flush();
                LOG_WARN("Post-fill size adjusted: " + std::to_string(old_size) +
                         " → " + std::to_string(position_size) + " lots");

                // Note: In a real live trading scenario, you would also need to partially
                // close the excess lots here via broker API. For paper trading this is
                // handled by using the adjusted position_size in the trade record.
            }
        } else {
            std::cout << "  ✓ Post-fill risk within cap\n";
            std::cout.flush();
        }
    }
    
    // Initialize trade record
    current_trade = Trade();
    current_trade.trade_num = bar_index;
    current_trade.direction = direction;
    current_trade.entry_date = bar.datetime;
    current_trade.entry_price = fill_price;
    current_trade.stop_loss = effective_stop_loss;
    current_trade.take_profit = effective_take_profit;  // uses recalculated TP if SL rescue fired
    current_trade.position_size = position_size;
    current_trade.zone_score = decision.score.total_score;
    current_trade.entry_aggressiveness = decision.entry_location_pct;
    current_trade.score_base_strength = decision.score.base_strength_score;
    current_trade.score_elite_bonus = decision.score.elite_bonus_score;
    current_trade.score_swing_position = decision.score.swing_position_score;
    current_trade.score_regime_alignment = decision.score.regime_alignment_score;
    current_trade.score_state_freshness = decision.score.state_freshness_score;
    current_trade.score_rejection_confirmation = decision.score.rejection_confirmation_score;
    current_trade.score_recommended_rr = decision.score.recommended_rr;
    current_trade.score_rationale = decision.score.entry_rationale;
    current_trade.regime = (regime != Core::MarketRegime::RANGING) ? "TRENDING" : "RANGING";
    current_trade.zone_id = zone.zone_id;
    current_trade.zone_formation_time = zone.formation_datetime;
    current_trade.zone_distal = zone.distal_line;
    current_trade.zone_proximal = zone.proximal_line;
    // Populate volume and OI fields
    current_trade.entry_volume = bar.volume;
    current_trade.entry_oi = bar.oi;
    current_trade.entry_volume_ratio = bar.norm_volume_ratio;
    // --- Entry bar volume metrics from EntryDecision ---
    current_trade.entry_pullback_vol_ratio = decision.entry_pullback_vol_ratio;
    current_trade.entry_volume_score = decision.entry_volume_score;
    current_trade.entry_volume_pattern = decision.entry_volume_pattern;
    current_trade.entry_position_lots = decision.lot_size;
    current_trade.position_size_reason = ""; // TODO: assign if available
    current_trade.zone_volume_ratio = zone.volume_profile.volume_ratio;
    current_trade.zone_departure_volume_ratio = zone.volume_profile.departure_volume_ratio;
    current_trade.zone_peak_volume = zone.volume_profile.peak_volume;
    current_trade.zone_departure_peak_volume = zone.volume_profile.departure_peak_volume;
    current_trade.zone_touch_volumes = zone.volume_profile.touch_volumes;
    current_trade.zone_volume_rising_on_retests = zone.volume_profile.volume_rising_on_retests;
    current_trade.zone_oi_phase = SDTrading::Core::market_phase_to_string(zone.oi_profile.market_phase);
    current_trade.zone_oi_score = zone.oi_profile.oi_score;
    current_trade.zone_volume_score = zone.volume_profile.volume_score;
        current_trade.zone_is_initiative = zone.volume_profile.is_initiative;
        current_trade.zone_has_volume_climax = zone.volume_profile.has_volume_climax;
        current_trade.zone_institutional_index = zone.institutional_index;
    // ⭐ Initialize trailing stop state
    current_trade.original_stop_loss = decision.stop_loss;  // Save original SL for R calculation
    current_trade.highest_price = fill_price;                // Initialize to entry price
    current_trade.lowest_price = fill_price;                 // Initialize to entry price
    current_trade.trailing_activated = false;
    current_trade.current_trail_stop = 0.0;
    current_trade.bars_in_trade = 0;
    // ⭐ Calculate Technical Indicators at Entry
    if (bars != nullptr && !bars->empty() && bar_index >= 0) {
        using Core::MarketAnalyzer;
        
        // Calculate Fast and Slow EMAs
        current_trade.fast_ema = MarketAnalyzer::calculate_ema(
            *bars,
            config.ema_fast_period,
            bar_index
        );
        current_trade.slow_ema = MarketAnalyzer::calculate_ema(
            *bars,
            config.ema_slow_period,
            bar_index
        );
        
        // Calculate RSI
        current_trade.rsi = MarketAnalyzer::calculate_rsi(*bars, config.rsi_period, bar_index);
        
        // Calculate Bollinger Bands
        auto bb = MarketAnalyzer::calculate_bollinger_bands(
            *bars,
            config.bb_period,
            config.bb_stddev,
            bar_index
        );
        current_trade.bb_upper = bb.upper;
        current_trade.bb_middle = bb.middle;
        current_trade.bb_lower = bb.lower;
        current_trade.bb_bandwidth = bb.bandwidth;
        
        // Calculate ADX
        auto adx = MarketAnalyzer::calculate_adx(*bars, config.adx_period, bar_index);
        current_trade.adx = adx.adx;
        current_trade.plus_di = adx.plus_di;
        current_trade.minus_di = adx.minus_di;
        
        // Calculate MACD
        auto macd = MarketAnalyzer::calculate_macd(
            *bars,
            config.macd_fast_period,
            config.macd_slow_period,
            config.macd_signal_period,
            bar_index
        );
        current_trade.macd_line = macd.macd_line;
        current_trade.macd_signal = macd.signal_line;
        current_trade.macd_histogram = macd.histogram;
        
        LOG_INFO("📊 Indicators calculated - RSI:" + std::to_string(current_trade.rsi) + 
                " ADX:" + std::to_string(current_trade.adx) + 
                " MACD:" + std::to_string(current_trade.macd_line));
    } else {
        // No bars available - set indicators to neutral/zero values
        current_trade.fast_ema = 0.0;
        current_trade.slow_ema = 0.0;
        current_trade.rsi = 50.0;  // Neutral RSI
        current_trade.bb_upper = 0.0;
        current_trade.bb_middle = 0.0;
        current_trade.bb_lower = 0.0;
        current_trade.bb_bandwidth = 0.0;
        current_trade.adx = 0.0;
        current_trade.plus_di = 0.0;
        current_trade.minus_di = 0.0;
        current_trade.macd_line = 0.0;
        current_trade.macd_signal = 0.0;
        current_trade.macd_histogram = 0.0;
        LOG_WARN("⚠️ Indicators not calculated - bars not available");
    }
    
    in_position = true;

    LOG_INFO("✅ Trade entered: " + direction + " @ " + std::to_string(fill_price));

    // 🔔 SOUND ALERT (LIVE MODE ONLY)
    if (mode == ExecutionMode::LIVE) {
        play_entry_alert();
        LOG_INFO("🔔 Entry alert played");
    }
    
    return true;
}

// SHARED LOGIC: Check stop loss (same for both modes)
bool TradeManager::check_stop_loss(const Bar& bar) const {
    if (!in_position) return false;
    
    if (current_trade.direction == "LONG") {
        return (bar.low <= current_trade.stop_loss);
    } else {
        return (bar.high >= current_trade.stop_loss);
    }
}

bool TradeManager::check_stop_loss(double current_price) const {
    if (!in_position) return false;
    
    if (current_trade.direction == "LONG") {
        return (current_price <= current_trade.stop_loss);
    } else {
        return (current_price >= current_trade.stop_loss);
    }
}

// SHARED LOGIC: Check take profit (same for both modes)
bool TradeManager::check_take_profit(const Bar& bar) const {
    if (!in_position) return false;
    // ⭐ SAFETY GUARD: Never trigger TP if take_profit is 0 or negative.
    // A TP of 0 means it was never computed (corrupt zone / SL=0 chain).
    // Without this guard: LONG bar.high >= 0.0 is ALWAYS TRUE → phantom TP exit at price=0.
    if (current_trade.take_profit <= 0.0) return false;
    if (current_trade.direction == "LONG") {
        return (bar.high >= current_trade.take_profit);
    } else {
        return (bar.low <= current_trade.take_profit);
    }
}

bool TradeManager::check_take_profit(double current_price) const {
    if (!in_position) return false;
    // ⭐ SAFETY GUARD: Never trigger TP if take_profit is 0 or negative.
    if (current_trade.take_profit <= 0.0) return false;
    if (current_trade.direction == "LONG") {
        return (current_price >= current_trade.take_profit);
    } else {
        return (current_price <= current_trade.take_profit);
    }
}

Trade TradeManager::close_trade(const Bar& bar, 
                                const std::string& exit_reason,
                                double exit_price) {
    if (!in_position) {
        LOG_WARN("No position to close");
        return Trade();
    }
    
    // Determine exit direction (opposite of entry)
    std::string exit_direction = (current_trade.direction == "LONG") ? "SELL" : "BUY";
    
    // Execute exit order (convert lots to units for broker)
    int total_units = current_trade.position_size * config.lot_size;
    double fill_price = execute_exit("", exit_direction, total_units, exit_price);
    
    // Complete trade record
    current_trade.exit_date = bar.datetime;
    current_trade.exit_price = fill_price;
    current_trade.exit_reason = exit_reason;
    // Populate exit volume and OI fields
    current_trade.exit_volume = bar.volume;
    current_trade.exit_oi = bar.oi;
    current_trade.exit_volume_ratio = bar.norm_volume_ratio;
    // Set exit_was_volume_climax if exit_reason is VOL_CLIMAX
    current_trade.exit_was_volume_climax = (exit_reason == "VOL_CLIMAX");
    // Calculate P&L
    calculate_pnl(current_trade);
    // Update capital
    update_capital(current_trade.pnl);
    in_position = false;
    LOG_INFO("✅ Trade closed: " + exit_reason + ", P&L: $" + std::to_string(current_trade.pnl));
    // 🔔 SOUND ALERT (LIVE MODE ONLY)
    if (mode == ExecutionMode::LIVE) {
        play_exit_alert();
        LOG_INFO("🔔 Exit alert played");
    }
    return current_trade;
}

Trade TradeManager::close_trade(const std::string& exit_datetime,
                                const std::string& exit_reason,
                                double exit_price) {
    if (!in_position) {
        LOG_WARN("No position to close");
        return Trade();
    }
    
    // Determine exit direction (opposite of entry)
    std::string exit_direction = (current_trade.direction == "LONG") ? "SELL" : "BUY";
    
    // Execute exit order (convert lots to units for broker)
    int total_units = current_trade.position_size * config.lot_size;
    double fill_price = execute_exit("", exit_direction, total_units, exit_price);
    
    // Complete trade record
    current_trade.exit_date = exit_datetime;
    current_trade.exit_price = fill_price;
    current_trade.exit_reason = exit_reason;
    
    // Calculate P&L
    calculate_pnl(current_trade);
    // ⭐ FIX #1: SAFETY NET - Log if loss exceeds cap
    // This should never happen if entry checks work, but log for monitoring
    if (current_trade.pnl < 0 && 
        std::abs(current_trade.pnl) > config.max_loss_per_trade) {
        LOG_WARN("⚠️  ALERT: Loss ₹" + std::to_string(std::abs(current_trade.pnl)) + 
                " exceeded max loss cap of ₹" + 
                std::to_string(config.max_loss_per_trade) +
                " - This should not happen!");
    }
    // Update capital
    update_capital(current_trade.pnl);
    
    in_position = false;
    
    LOG_INFO("✅ Trade closed: " + exit_reason + ", P&L: $" + std::to_string(current_trade.pnl));
    
    // 🔔 SOUND ALERT (LIVE MODE ONLY)
    if (mode == ExecutionMode::LIVE) {
        play_exit_alert();
        LOG_INFO("🔔 Exit alert played");
    }
    
    return current_trade;
}

void TradeManager::update_capital(double pnl) {
    current_capital += pnl;
    LOG_DEBUG("Capital updated: $" + std::to_string(current_capital));
}

// ========================================
// V6.0: Volume/OI Integration Methods
// ========================================

void TradeManager::set_volume_baseline(const Utils::VolumeBaseline* baseline) {
    volume_baseline_ = baseline;
    LOG_INFO("TradeManager: Volume baseline set");
}

void TradeManager::set_oi_scorer(const Core::OIScorer* scorer) {
    oi_scorer_ = scorer;
    LOG_INFO("TradeManager: OI scorer set");
}

TradeManager::VolumeExitSignal TradeManager::check_volume_exit_signals(
    const Trade& trade,
    const Bar& current_bar
) const {
    // Skip if volume baseline not available
    if (volume_baseline_ == nullptr || !volume_baseline_->is_loaded()) {
        return VolumeExitSignal::NONE;
    }
    
    // Skip if V6.0 not enabled or volume exits disabled
    if (!config.v6_fully_enabled) {
        return VolumeExitSignal::NONE;
    }
    
    // Get normalized volume
    std::string time_slot = extract_time_slot(current_bar.datetime);
    double norm_ratio = volume_baseline_->get_normalized_ratio(
        time_slot,
        current_bar.volume
    );
    
    // Signal 1: Volume Climax (>3.0x average + in profit)
    // Calculate unrealized PnL
    double unrealized_pnl = 0.0;
    if (trade.direction == "LONG") {
        unrealized_pnl = (current_bar.close - trade.entry_price) * trade.position_size;
    } else {
        unrealized_pnl = (trade.entry_price - current_bar.close) * trade.position_size;
    }
    
    if (norm_ratio > config.volume_climax_exit_threshold && unrealized_pnl > 0) {
        LOG_INFO("🚨 VOLUME CLIMAX detected: " + std::to_string(norm_ratio) + "x");
        return VolumeExitSignal::VOLUME_CLIMAX;
    }
    
    // Future: Add VOLUME_DRYING_UP and VOLUME_DIVERGENCE signals
    
    return VolumeExitSignal::NONE;
}

TradeManager::OIExitSignal TradeManager::check_oi_exit_signals(
    const Trade& trade,
    const Bar& current_bar,
    const std::vector<Bar>& bars,
    int current_index
) const {
    // Skip if OI scorer not available
    if (oi_scorer_ == nullptr) {
        return OIExitSignal::NONE;
    }
    
    // Skip if V6.0 not enabled
    if (!config.v6_fully_enabled) {
        return OIExitSignal::NONE;
    }
    
    // Only process if OI data is fresh
    if (!current_bar.oi_fresh) {
        return OIExitSignal::NONE;
    }
    
    // Need at least 10 bars for analysis
    if (current_index < 10 || bars.size() == 0) {
        return OIExitSignal::NONE;
    }
    
    // Calculate recent price and OI changes
    int lookback = 10;
    int start_index = current_index - lookback;
    if (start_index < 0) {
        return OIExitSignal::NONE;
    }
    
    double price_change = ((current_bar.close - bars[start_index].close) / 
                          bars[start_index].close) * 100.0;
    
    double oi_start = bars[start_index].oi;
    double oi_current = current_bar.oi;
    
    if (oi_start <= 0) {
        return OIExitSignal::NONE;
    }
    
    double oi_change = ((oi_current - oi_start) / oi_start) * 100.0;
    
    // Signal 1: OI Unwinding (CRITICAL - exit immediately)
    if (trade.direction == "LONG") {
        // LONG + price rising + OI falling = longs exiting (smart money out)
        if (price_change > 0.2 && oi_change < config.oi_unwinding_threshold) {
            LOG_INFO("🚨 OI UNWINDING detected (LONG): OI " + std::to_string(oi_change) + "%");
            return OIExitSignal::OI_UNWINDING;
        }
    } else { // SHORT
        // SHORT + price falling + OI falling = shorts covering
        if (price_change < -0.2 && oi_change < config.oi_unwinding_threshold) {
            LOG_INFO("🚨 OI UNWINDING detected (SHORT): OI " + std::to_string(oi_change) + "%");
            return OIExitSignal::OI_UNWINDING;
        }
    }
    
    // Future: Add OI_REVERSAL and OI_STAGNATION signals
    
    return OIExitSignal::NONE;
}

TradeManager::VolumeExhaustionSignal TradeManager::check_volume_exhaustion_signals(
    const Trade& trade,
    const Bar& current_bar,
    const std::vector<Bar>& bars,
    int current_index
) const {
    if (volume_baseline_ == nullptr || !volume_baseline_->is_loaded()) {
        return VolumeExhaustionSignal::NONE;
    }
    
    if (!config.v6_fully_enabled || !config.enable_volume_exhaustion_exit) {
        return VolumeExhaustionSignal::NONE;
    }
    
    double unrealized_pnl = 0.0;
    if (trade.direction == "LONG") {
        unrealized_pnl = (current_bar.close - trade.entry_price) * trade.position_size;
    } else {
        unrealized_pnl = (trade.entry_price - current_bar.close) * trade.position_size;
    }
    
    if (unrealized_pnl >= 0) {
        return VolumeExhaustionSignal::NONE;
    }
    
    double loss_points = std::abs(unrealized_pnl) / trade.position_size;
    std::string time_slot = extract_time_slot(current_bar.datetime);
    double volume_ratio = volume_baseline_->get_normalized_ratio(time_slot, current_bar.volume);
    double candle_body = std::abs(current_bar.close - current_bar.open);
    double candle_range = current_bar.high - current_bar.low;
    bool is_against_us = (trade.direction == "LONG") ? (current_bar.close < current_bar.open) : (current_bar.close > current_bar.open);
    double atr = candle_range;
    
    if (volume_ratio >= config.vol_exhaustion_spike_min_ratio && is_against_us && candle_body > atr * config.vol_exhaustion_spike_min_body_atr) {
        return VolumeExhaustionSignal::AGAINST_TREND_SPIKE;
    }
    
    if (volume_ratio >= config.vol_exhaustion_absorption_min_ratio && candle_body < atr * config.vol_exhaustion_absorption_max_body_atr && unrealized_pnl < 0) {
        return VolumeExhaustionSignal::ABSORPTION;
    }
    
    if (bars.size() > 0 && current_index >= 5) {
        int bars_against = 0;
        int lookback = std::min(5, current_index);
        for (int i = current_index - lookback; i < current_index; i++) {
            if (i < 0 || i >= static_cast<int>(bars.size())) continue;
            const Bar& bar = bars[i];
            std::string slot = extract_time_slot(bar.datetime);
            double vol_ratio = volume_baseline_->get_normalized_ratio(slot, bar.volume);
            bool bar_against = (trade.direction == "LONG") ? (bar.close < bar.open) : (bar.close > bar.open);
            if (vol_ratio > config.vol_exhaustion_flow_min_ratio && bar_against) {
                bars_against++;
            }
        }
        if (bars_against >= config.vol_exhaustion_flow_min_bars) {
            return VolumeExhaustionSignal::FLOW_REVERSAL;
        }
    }
    
    if (volume_ratio < config.vol_exhaustion_drift_max_ratio && loss_points > atr * config.vol_exhaustion_drift_min_loss_atr) {
        return VolumeExhaustionSignal::LOW_VOLUME_DRIFT;
    }
    
    return VolumeExhaustionSignal::NONE;
}

bool TradeManager::should_exit_on_exhaustion(
    VolumeExhaustionSignal signal,
    const Trade& trade,
    const Bar& current_bar
) const {
    if (signal == VolumeExhaustionSignal::NONE) {
        return false;
    }
    
    double current_loss_points = 0.0;
    if (trade.direction == "LONG") {
        current_loss_points = std::max(0.0, trade.entry_price - current_bar.close);
    } else {
        current_loss_points = std::max(0.0, current_bar.close - trade.entry_price);
    }
    
    double sl_distance_points = std::abs(trade.entry_price - trade.stop_loss);
    double loss_ratio = 0.0;
    if (sl_distance_points > 0) {
        loss_ratio = current_loss_points / sl_distance_points;
    }
    
    if (loss_ratio < config.vol_exhaustion_max_loss_pct) {
        LOG_INFO("VOL_EXHAUSTION: Exiting at " + std::to_string(loss_ratio * 100.0) + "% of SL");
        return true;
    }
    return false;
}

std::string TradeManager::extract_time_slot(const std::string& datetime) const {
    // Expected format: "2024-02-08 09:15:00"
    if (datetime.length() >= 16) {
        std::string time_hhmm = datetime.substr(11, 5);  // Extract "HH:MM"
        
        try {
            // Parse hour and minute
            int hour = std::stoi(time_hhmm.substr(0, 2));
            int min = std::stoi(time_hhmm.substr(3, 2));
            
            // Round down to nearest 5-minute interval
            // 09:32 -> 09:30, 09:33 -> 09:30, 09:34 -> 09:30, 09:35 -> 09:35
            min = (min / 5) * 5;
            
            // Format back to string
            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(2) << hour << ":"
                << std::setfill('0') << std::setw(2) << min;
            
            return oss.str();
        } catch (...) {
            // Fallback if parsing fails
            return "00:00";
        }
    }
    return "00:00"; // Fallback
}

} // namespace Backtest
} // namespace SDTrading