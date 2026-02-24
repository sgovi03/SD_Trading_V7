// ============================================================
// FILE: src/backtest/trade_manager.cpp
// CHANGE: Replace the entire POST-FILL CAP CHECK block
//         (starting at the comment "⭐ POST-FILL CAP CHECK")
// ============================================================

    // ⭐ POST-FILL CAP CHECK: Revalidate risk using ACTUAL fill price.
    // Breakeven mode: decision.entry_price is set to the zone's breakeven level,
    // NOT the actual market fill. The broker fills deeper inside the zone,
    // making the actual SL distance wider than the pre-fill estimate.
    // Instead of squaring off, we SAVE the trade by tightening the SL
    // to the maximum distance allowed by max_loss_per_trade.
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
                // ⭐ FIX: SL TIGHTENING — save the trade instead of squaring off.
                //
                // Root cause: In breakeven entry mode, decision.entry_price is set
                // to the zone's breakeven level (e.g. 25597.22), but the broker fills
                // at the actual market price (e.g. 25511.50). The SL (e.g. 25680.58)
                // was correctly sized against the decision entry, but with the real fill
                // price the actual SL distance is wider (169 pts vs 153 pts cap).
                // The gap is typically small (< 50 pts). Tightening SL by this margin
                // saves the trade — which in many cases would be a winner (e.g. Rs17,577
                // profit on zone 91, Rs15,218 on zone 54).
                //
                // Strategy: adjust stop loss to exactly max_loss_per_trade / (lot_size * lots)
                // distance from the fill price. For SHORT: new_sl = fill + max_sl_dist.
                //           For LONG:  new_sl = fill - max_sl_dist.
                //
                double max_sl_distance = config.max_loss_per_trade / (config.lot_size * static_cast<double>(position_size));
                double adjusted_sl = 0.0;

                if (direction == "LONG") {
                    adjusted_sl = fill_price - max_sl_distance;
                } else {
                    // SHORT
                    adjusted_sl = fill_price + max_sl_distance;
                }

                // Safety guard: the new SL must still be on the protective side,
                // and the distance must be meaningful (> 5 pts minimum).
                bool sl_valid = (direction == "LONG")
                    ? (adjusted_sl < fill_price)
                    : (adjusted_sl > fill_price);

                if (!sl_valid || max_sl_distance < 5.0) {
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

                // Accept the trade with tightened SL
                double old_sl = decision.stop_loss;
                double old_dist = fill_stop_dist;
                decision.stop_loss = adjusted_sl;
                double new_risk = max_sl_distance * config.lot_size * position_size;

                LOG_WARN("Post-fill SL TIGHTENED (cap rescue): fill=" + std::to_string(fill_price) +
                         " original_sl=" + std::to_string(old_sl) +
                         " -> adjusted_sl=" + std::to_string(adjusted_sl) +
                         " dist=" + std::to_string(old_dist) + "->" + std::to_string(max_sl_distance) +
                         " pts  risk=Rs" + std::to_string(new_risk));
                std::cout << "  ⚠️  Post-fill SL TIGHTENED (cap rescue):\n";
                std::cout << "    Original SL:   " << std::fixed << std::setprecision(2) << old_sl
                          << " (dist " << old_dist << " pts, risk Rs" << actual_fill_risk << ")\n";
                std::cout << "    Adjusted SL:   " << adjusted_sl
                          << " (dist " << max_sl_distance << " pts, risk Rs" << new_risk << ")\n";
                std::cout << "    SL tightened by: " << (old_dist - max_sl_distance) << " pts\n";
                std::cout << "    Trade SAVED: proceeding with 1 lot at capped risk\n";
                std::cout.flush();

                // Update position to reflect the tightened SL
                // (position_size stays at 1 — already the minimum)

            } else {
                // Reduce position size — SL is fine but lots need reducing
                int old_size = position_size;
                position_size = affordable_at_fill;
                total_units = position_size * config.lot_size;

                std::cout << "  ⚠️  Post-fill position size adjusted: "
                          << old_size << " -> " << position_size
                          << " lots (actual fill risk cap)\n";
                std::cout.flush();
                LOG_WARN("Post-fill size adjusted: " + std::to_string(old_size) +
                         " -> " + std::to_string(position_size) + " lots");
            }
        } else {
            std::cout << "  ✓ Post-fill risk within cap\n";
            std::cout.flush();
        }
    }
