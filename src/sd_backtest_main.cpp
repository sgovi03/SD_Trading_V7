// ============================================================
// SD BACKTEST MAIN V5.0
// Simplified backtest file compatible with V5.0 architecture
// ============================================================

#include "sd_engine_core.h"
#include "zone_manager.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>

// Simple trade tracking
struct TradeResult {
    int trade_num = 0;
    std::string entry_date;
    std::string exit_date;
    std::string direction;
    double entry_price = 0.0;
    double exit_price = 0.0;
    double pnl = 0.0;
    double pnl_pct = 0.0;
    bool win = false;
};

void print_trade_summary(const std::vector<TradeResult>& trades, double starting_capital) {
    if (trades.empty()) {
        std::cout << "No trades executed" << std::endl;
        return;
    }

    int wins = 0;
    int losses = 0;
    double total_pnl = 0.0;
    double max_win = 0.0;
    double max_loss = 0.0;

    for (const auto& trade : trades) {
        total_pnl += trade.pnl;
        if (trade.win) {
            wins++;
            max_win = std::max(max_win, trade.pnl);
        } else {
            losses++;
            max_loss = std::min(max_loss, trade.pnl);
        }
    }

    double win_rate = (wins * 100.0) / trades.size();
    double final_capital = starting_capital + total_pnl;
    double roi = (total_pnl / starting_capital) * 100.0;

    std::cout << "\n========================================" << std::endl;
    std::cout << "BACKTEST RESULTS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total trades:     " << trades.size() << std::endl;
    std::cout << "Wins:             " << wins << std::endl;
    std::cout << "Losses:           " << losses << std::endl;
    std::cout << "Win rate:         " << win_rate << "%" << std::endl;
    std::cout << "Total P&L:        ₹" << total_pnl << std::endl;
    std::cout << "ROI:              " << roi << "%" << std::endl;
    std::cout << "Starting capital: ₹" << starting_capital << std::endl;
    std::cout << "Final capital:    ₹" << final_capital << std::endl;
    std::cout << "Max win:          ₹" << max_win << std::endl;
    std::cout << "Max loss:         ₹" << max_loss << std::endl;
    std::cout << "========================================" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "SD Trading System V5.0 - Backtest" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    try {
        // Load configuration
        std::string config_file = "phase1_enhanced_v3_1_config.txt";
        if (argc > 1) {
            config_file = argv[1];
        }

        std::cout << "[INFO] Loading configuration from: " << config_file << std::endl;
        BacktestConfig config = load_config(config_file);
        
        if (!config.validate()) {
            std::cerr << "❌ Configuration validation failed!" << std::endl;
            return 1;
        }
        
        config.print_summary();

        // Load CSV data
        std::string data_file = "data/backtest_data.csv";
        if (argc > 2) {
            data_file = argv[2];
        }

        std::cout << "\n[INFO] Loading backtest data from: " << data_file << std::endl;
        std::vector<Bar> bars = load_csv_data(data_file);
        
        if (bars.empty()) {
            std::cerr << "❌ No data loaded!" << std::endl;
            return 1;
        }

        std::cout << "[INFO] Loaded " << bars.size() << " bars" << std::endl;
        std::cout << "[INFO] Date range: " << bars.front().datetime 
                 << " to " << bars.back().datetime << std::endl;

        // Initialize
        ZoneManager zone_manager;
        std::vector<TradeResult> trades;
        double current_capital = config.starting_capital;
        
        // Track active trade
        bool in_trade = false;
        TradeResult current_trade;
        int trade_entry_idx = 0;

        // Run backtest
        std::cout << "\n[INFO] Running backtest..." << std::endl;
        
        int start_idx = std::max(config.lookback_for_zones + 50, 0);
        
        for (int i = start_idx; i < static_cast<int>(bars.size()); ++i) {
            try {
                // Update zones
                zone_manager.update(bars, i, config);
                
                // Check exit conditions for active trade
                if (in_trade) {
                    const Bar& current_bar = bars[i];
                    bool exit_trade = false;
                    
                    if (current_trade.direction == "LONG") {
                        // Check stop loss
                        if (current_bar.low <= current_trade.entry_price * 0.99) {
                            current_trade.exit_price = current_trade.entry_price * 0.99;
                            exit_trade = true;
                        }
                        // Check take profit
                        else if (current_bar.high >= current_trade.entry_price * 1.02) {
                            current_trade.exit_price = current_trade.entry_price * 1.02;
                            exit_trade = true;
                        }
                    } else {
                        // Check stop loss
                        if (current_bar.high >= current_trade.entry_price * 1.01) {
                            current_trade.exit_price = current_trade.entry_price * 1.01;
                            exit_trade = true;
                        }
                        // Check take profit
                        else if (current_bar.low <= current_trade.entry_price * 0.98) {
                            current_trade.exit_price = current_trade.entry_price * 0.98;
                            exit_trade = true;
                        }
                    }
                    
                    if (exit_trade) {
                        current_trade.exit_date = current_bar.datetime;
                        
                        // Calculate P&L
                        if (current_trade.direction == "LONG") {
                            current_trade.pnl = (current_trade.exit_price - current_trade.entry_price) * config.lot_size;
                        } else {
                            current_trade.pnl = (current_trade.entry_price - current_trade.exit_price) * config.lot_size;
                        }
                        
                        current_trade.pnl_pct = (current_trade.pnl / current_capital) * 100.0;
                        current_trade.win = current_trade.pnl > 0;
                        
                        current_capital += current_trade.pnl;
                        trades.push_back(current_trade);
                        
                        std::cout << "[TRADE " << current_trade.trade_num << "] "
                                 << (current_trade.win ? "WIN" : "LOSS") << " "
                                 << current_trade.direction << " "
                                 << "P&L: ₹" << std::fixed << std::setprecision(2) 
                                 << current_trade.pnl << std::endl;
                        
                        in_trade = false;
                    }
                }
                
                // Look for entry if not in trade
                if (!in_trade && trades.size() < 100) {  // Limit trades for demo
                    const std::vector<Zone>& active_zones = zone_manager.get_active_zones();
                    MarketRegime regime = determine_htf_regime(bars, i, config);
                    
                    for (const Zone& zone : active_zones) {
                        EntryDecision decision = evaluate_entry(zone, bars, i, regime, config);
                        
                        if (decision.should_enter) {
                            // Enter trade
                            current_trade = TradeResult();
                            current_trade.trade_num = trades.size() + 1;
                            current_trade.entry_date = bars[i].datetime;
                            current_trade.direction = (zone.type == ZoneType::DEMAND) ? "LONG" : "SHORT";
                            current_trade.entry_price = decision.entry_price;
                            
                            trade_entry_idx = i;
                            in_trade = true;
                            
                            std::cout << "[ENTRY " << current_trade.trade_num << "] "
                                     << current_trade.direction << " @ "
                                     << current_trade.entry_price
                                     << " [Score: " << decision.score.total_score << "]"
                                     << std::endl;
                            
                            break;  // One entry per bar
                        }
                    }
                }
                
                // Progress update
                if (i % 100 == 0) {
                    int progress = (i * 100) / bars.size();
                    std::cout << "[PROGRESS] " << progress << "% "
                             << "(Bar " << i << "/" << bars.size() << ")" << std::endl;
                }
                
            } catch (const std::exception& e) {
                std::cerr << "❌ [ERROR] Backtest failed at bar " << i 
                         << ": " << e.what() << std::endl;
                return 1;
            }
        }

        // Close any open trade
        if (in_trade) {
            current_trade.exit_date = bars.back().datetime;
            current_trade.exit_price = bars.back().close;
            
            if (current_trade.direction == "LONG") {
                current_trade.pnl = (current_trade.exit_price - current_trade.entry_price) * config.lot_size;
            } else {
                current_trade.pnl = (current_trade.entry_price - current_trade.exit_price) * config.lot_size;
            }
            
            current_trade.win = current_trade.pnl > 0;
            current_capital += current_trade.pnl;
            trades.push_back(current_trade);
        }

        // Print results
        print_trade_summary(trades, config.starting_capital);

        std::cout << "\nPress Enter to exit...";
        std::cin.get();

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ [FATAL ERROR] " << e.what() << std::endl;
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    }
}
