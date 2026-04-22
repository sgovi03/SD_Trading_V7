// ============================================================
// SD TRADING V8 - SQLITE TRADE REPOSITORY IMPLEMENTATION
// src/persistence/sqlite_trade_repository.cpp
// Milestone M3.3
// ============================================================

#include "sqlite_trade_repository.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace SDTrading {
namespace Persistence {

SqliteTradeRepository::SqliteTradeRepository(SqliteConnection& conn)
    : conn_(conn) {}

// ============================================================
// insert_signal
// ============================================================

int64_t SqliteTradeRepository::insert_signal(const Events::SignalEvent& evt) {
    static const char* SQL =
        "INSERT INTO signals ("
        "  signal_time, symbol, direction, zone_id,"
        "  zone_score, zone_formation_time, zone_distal, zone_proximal,"
        "  score_base_strength, score_elite_bonus, score_swing_position,"
        "  score_regime_alignment, score_state_freshness, score_rejection_confirmation,"
        "  recommended_rr, score_rationale, aggressiveness,"
        "  swing_percentile, at_swing_extreme,"
        "  entry_price, stop_loss, target_1, target_2, rr_ratio, lot_size, order_tag,"
        "  regime,"
        "  fast_ema, slow_ema, rsi, bb_upper, bb_middle, bb_lower, bb_bandwidth,"
        "  adx, di_plus, di_minus, macd_line, macd_signal, macd_histogram,"
        "  zone_departure_vol_ratio, zone_is_initiative, zone_vol_climax, zone_vol_score,"
        "  zone_institutional_index, entry_pullback_vol_ratio, entry_volume_score,"
        "  entry_volume_pattern, entry_volume, entry_oi, entry_volume_ratio,"
        "  acted_upon"
        ") VALUES ("
        "  ?,?,?,?, ?,?,?,?, ?,?,?, ?,?,?, ?,?,?, ?,?,"
        "  ?,?,?,?,?,?,?, ?,"
        "  ?,?,?,?,?,?,?, ?,?,?,?,?,?,"
        "  ?,?,?,?, ?,?,?, ?,?,?,?,"
        "  0"
        ")";

    try {
        auto stmt = conn_.prepare(SQL);
        int i = 1;
        stmt.bind_text(i++,   evt.signal_time);
        stmt.bind_text(i++,   evt.symbol);
        stmt.bind_text(i++,   evt.direction);
        stmt.bind_int(i++,    evt.zone_id);
        stmt.bind_double(i++, evt.score);
        stmt.bind_text(i++,   "");   // zone_formation_time (filled from zone data)
        stmt.bind_double(i++, 0.0);  // zone_distal
        stmt.bind_double(i++, 0.0);  // zone_proximal
        stmt.bind_double(i++, evt.score_base_strength);
        stmt.bind_double(i++, evt.score_elite_bonus);
        stmt.bind_double(i++, evt.score_swing);
        stmt.bind_double(i++, evt.score_regime);
        stmt.bind_double(i++, evt.score_freshness);
        stmt.bind_double(i++, evt.score_rejection);
        stmt.bind_double(i++, 0.0);  // recommended_rr
        stmt.bind_text(i++,   evt.score_rationale);
        stmt.bind_double(i++, 0.0);  // aggressiveness
        stmt.bind_double(i++, 0.0);  // swing_percentile
        stmt.bind_bool(i++,   false);// at_swing_extreme
        stmt.bind_double(i++, evt.entry_price);
        stmt.bind_double(i++, evt.stop_loss);
        stmt.bind_double(i++, evt.target_1);
        stmt.bind_double(i++, evt.target_2);
        stmt.bind_double(i++, evt.rr_ratio);
        stmt.bind_int(i++,    evt.lot_size);
        stmt.bind_text(i++,   evt.order_tag);
        stmt.bind_text(i++,   evt.regime);
        stmt.bind_double(i++, evt.fast_ema);
        stmt.bind_double(i++, evt.slow_ema);
        stmt.bind_double(i++, evt.rsi);
        stmt.bind_double(i++, evt.bb_upper);
        stmt.bind_double(i++, evt.bb_middle);
        stmt.bind_double(i++, evt.bb_lower);
        stmt.bind_double(i++, evt.bb_bandwidth);
        stmt.bind_double(i++, evt.adx);
        stmt.bind_double(i++, evt.di_plus);
        stmt.bind_double(i++, evt.di_minus);
        stmt.bind_double(i++, evt.macd_line);
        stmt.bind_double(i++, evt.macd_signal);
        stmt.bind_double(i++, evt.macd_histogram);
        stmt.bind_double(i++, evt.zone_departure_vol_ratio);
        stmt.bind_bool(i++,   evt.zone_is_initiative);
        stmt.bind_bool(i++,   evt.zone_vol_climax);
        stmt.bind_double(i++, evt.zone_vol_score);
        stmt.bind_double(i++, evt.zone_institutional_index);
        stmt.bind_double(i++, evt.entry_pullback_vol_ratio);
        stmt.bind_int(i++,    evt.entry_volume_score);
        stmt.bind_text(i++,   evt.entry_volume_pattern);
        stmt.bind_double(i++, evt.entry_volume);
        stmt.bind_double(i++, evt.entry_oi);
        stmt.bind_double(i++, 0.0);  // entry_volume_ratio
        stmt.execute();
        int64_t id = conn_.last_insert_rowid();
        LOG_INFO("[SqliteTradeRepo] Signal inserted: id=" << id
                 << " tag=" << evt.order_tag << " " << evt.symbol
                 << " " << evt.direction << " score=" << evt.score);
        return id;
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] insert_signal failed: " << e.what());
        return -1;
    }
}

// ============================================================
// mark_signal_acted
// ============================================================

bool SqliteTradeRepository::mark_signal_acted(const std::string& order_tag) {
    try {
        auto stmt = conn_.prepare(
            "UPDATE signals SET acted_upon=1 WHERE order_tag=?");
        stmt.bind_text(1, order_tag);
        stmt.execute();
        return true;
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] mark_signal_acted failed: " << e.what());
        return false;
    }
}

// ============================================================
// get_signal_id_by_tag
// ============================================================

int64_t SqliteTradeRepository::get_signal_id_by_tag(const std::string& order_tag) {
    try {
        auto stmt = conn_.prepare(
            "SELECT id FROM signals WHERE order_tag=? LIMIT 1");
        stmt.bind_text(1, order_tag);
        if (stmt.step()) return stmt.column_int64(0);
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] get_signal_id_by_tag failed: " << e.what());
    }
    return -1;
}

int64_t SqliteTradeRepository::get_trade_id_by_tag(const std::string& order_tag) {
    try {
        auto stmt = conn_.prepare(
            "SELECT id FROM trades WHERE order_tag=? LIMIT 1");
        stmt.bind_text(1, order_tag);
        if (stmt.step()) return stmt.column_int64(0);
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] get_trade_id_by_tag failed: " << e.what());
    }
    return -1;
}

// ============================================================
// insert_trade_open
// ============================================================

bool SqliteTradeRepository::insert_trade_open(const Events::TradeOpenEvent& evt) {
    static const char* SQL =
        "INSERT OR IGNORE INTO trades ("
        "  order_tag, symbol, direction, signal_id, zone_id,"
        "  entry_time, entry_price, entry_position_lots, position_size,"
        "  stop_loss, take_profit, trade_status"
        ") VALUES (?,?,?,?,?, ?,?,?,?, ?,?, 'OPEN')";

    try {
        int64_t sig_id = get_signal_id_by_tag(evt.order_tag);
        auto stmt = conn_.prepare(SQL);
        stmt.bind_text(1,   evt.order_tag);
        stmt.bind_text(2,   evt.symbol);
        stmt.bind_text(3,   evt.direction);
        if (sig_id > 0) stmt.bind_int64(4, sig_id); else stmt.bind_null(4);
        stmt.bind_int(5,    evt.zone_id);
        stmt.bind_text(6,   evt.entry_time);
        stmt.bind_double(7, evt.entry_price);
        stmt.bind_int(8,    evt.lots);
        stmt.bind_int(9,    evt.lots);
        stmt.bind_double(10, evt.stop_loss);
        stmt.bind_double(11, evt.take_profit);
        int rows_affected = stmt.execute();

        // Mark signal as acted upon
        mark_signal_acted(evt.order_tag);

        // ⚠️  FIX: INSERT OR IGNORE means the second call (from SignalConsumer after
        //    LiveEngine already published TradeOpenEvent) silently inserts nothing.
        //    rows_affected=0 means the row already existed — log at DEBUG not INFO
        //    to avoid the confusing duplicate "[SqliteTradeRepo] Trade opened" log lines.
        if (rows_affected > 0) {
            LOG_INFO("[SqliteTradeRepo] Trade opened: " << evt.order_tag
                     << " " << evt.symbol << " " << evt.direction
                     << " @" << evt.entry_price << " lots=" << evt.lots);
        } else {
            LOG_DEBUG("[SqliteTradeRepo] Trade open duplicate skipped (INSERT OR IGNORE): "
                      << evt.order_tag);
        }
        return true;
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] insert_trade_open failed: " << e.what());
        return false;
    }
}

// ============================================================
// update_trade_close
// ============================================================

bool SqliteTradeRepository::update_trade_close(const Events::TradeCloseEvent& evt) {
    static const char* SQL =
        "UPDATE trades SET"
        "  exit_time=?, exit_price=?, exit_reason=?,"
        "  exit_volume=?, exit_was_vol_climax=?,"
        "  gross_pnl=?, brokerage=?, net_pnl=?,"
        "  bars_in_trade=?, trade_status='CLOSED',"
        "  updated_at=datetime('now','localtime')"
        " WHERE order_tag=?"
        "   AND trade_status!='CLOSED'";

    try {
        auto stmt = conn_.prepare(SQL);
        stmt.bind_text(1,   evt.exit_time);
        stmt.bind_double(2, evt.exit_price);
        stmt.bind_text(3,   evt.exit_reason);
        stmt.bind_double(4, evt.exit_volume);
        stmt.bind_bool(5,   evt.exit_was_vol_climax);
        stmt.bind_double(6, evt.gross_pnl);
        stmt.bind_double(7, evt.brokerage);
        stmt.bind_double(8, evt.net_pnl);
        stmt.bind_int(9,    evt.bars_in_trade);
        stmt.bind_text(10,  evt.order_tag);
        int rows = stmt.execute();

        if (rows == 0) {
            LOG_INFO("[SqliteTradeRepo] update_trade_close: skipped"
                     << " (already closed or missing) tag=" << evt.order_tag);
        } else {
            LOG_INFO("[SqliteTradeRepo] Trade closed: " << evt.order_tag
                     << " pnl=" << evt.net_pnl
                     << " reason=" << evt.exit_reason);
        }
        return rows > 0;
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] update_trade_close failed: " << e.what());
        return false;
    }
}

// ============================================================
// append_equity_point
// ============================================================

bool SqliteTradeRepository::append_equity_point(const std::string& symbol,
                                                  const std::string& timestamp,
                                                  double running_equity,
                                                  double peak_equity,
                                                  int64_t trade_id) {
    static const char* SQL =
        "INSERT OR IGNORE INTO equity_curve (time, symbol, equity, peak_equity,"
        " drawdown, drawdown_pct, trade_id)"
        " VALUES (?,?,?,?,?,?,?)";
    try {
        double drawdown     = peak_equity - running_equity;
        double drawdown_pct = peak_equity > 0
                              ? (drawdown / peak_equity * 100.0) : 0.0;
        auto stmt = conn_.prepare(SQL);
        stmt.bind_text(1,   timestamp);
        stmt.bind_text(2,   symbol);
        stmt.bind_double(3, running_equity);
        stmt.bind_double(4, peak_equity);
        stmt.bind_double(5, drawdown);
        stmt.bind_double(6, drawdown_pct);
        if (trade_id > 0) {
            stmt.bind_int64(7, trade_id);
        } else {
            stmt.bind_null(7);
        }
        stmt.execute();
        return true;
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] append_equity_point failed: " << e.what());
        return false;
    }
}

// ============================================================
// get_closed_trades
// ============================================================

std::vector<Core::Trade> SqliteTradeRepository::get_closed_trades(
    const std::string& symbol)
{
    std::vector<Core::Trade> result;
    try {
        std::string sql =
            "SELECT t.*, s.regime, s.fast_ema, s.slow_ema, s.rsi,"
            "  s.adx, s.di_plus, s.di_minus,"
            "  s.macd_line, s.macd_signal, s.macd_histogram,"
            "  s.bb_upper, s.bb_middle, s.bb_lower, s.bb_bandwidth,"
            "  s.score_base_strength, s.score_elite_bonus,"
            "  s.score_swing_position, s.score_regime_alignment,"
            "  s.score_state_freshness, s.score_rejection_confirmation,"
            "  s.recommended_rr, s.score_rationale"
            " FROM trades t"
            " LEFT JOIN signals s ON t.signal_id = s.id"
            " WHERE t.symbol=? AND t.trade_status='CLOSED'"
            " ORDER BY t.entry_time ASC";

        auto stmt = conn_.prepare(sql);
        stmt.bind_text(1, symbol);

        while (stmt.step()) {
            Core::Trade trade;
            trade.order_tag    = stmt.column_text(1);  // order_tag
            trade.direction    = stmt.column_text(3);  // direction
            trade.entry_date   = stmt.column_text(7);  // entry_time
            trade.exit_date    = stmt.column_text(14); // exit_time
            trade.entry_price  = stmt.column_double(8);
            trade.exit_price   = stmt.column_double(15);
            trade.exit_reason  = stmt.column_text(17);
            trade.stop_loss    = stmt.column_double(11);
            trade.take_profit  = stmt.column_double(12);
            trade.position_size= stmt.column_int(9);
            trade.pnl          = stmt.column_double(21); // net_pnl → Trade::pnl
            // gross_pnl also mapped to pnl (Trade struct has single pnl field)
            trade.bars_in_trade= stmt.column_int(23);
            trade.zone_id      = stmt.column_int(5);
            result.push_back(trade);
        }
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] get_closed_trades failed: " << e.what());
    }
    return result;
}

// ============================================================
// get_all_closed_trades (portfolio)
// ============================================================

std::vector<Core::Trade> SqliteTradeRepository::get_all_closed_trades() {
    std::vector<Core::Trade> result;
    try {
        auto stmt = conn_.prepare(
            "SELECT order_tag, symbol, direction, entry_time, exit_time,"
            "  entry_price, exit_price, stop_loss, take_profit,"
            "  entry_position_lots, net_pnl, exit_reason, bars_in_trade, zone_id"
            " FROM trades WHERE trade_status='CLOSED'"
            " ORDER BY entry_time ASC");

        while (stmt.step()) {
            Core::Trade t;
            t.order_tag    = stmt.column_text(0);
            t.direction    = stmt.column_text(2);
            t.entry_date   = stmt.column_text(3);
            t.exit_date    = stmt.column_text(4);
            t.entry_price  = stmt.column_double(5);
            t.exit_price   = stmt.column_double(6);
            t.stop_loss    = stmt.column_double(7);
            t.take_profit  = stmt.column_double(8);
            t.position_size= stmt.column_int(9);
            t.pnl          = stmt.column_double(10); // net_pnl → Trade::pnl
            // Trade struct has single pnl field; net_pnl from DB maps here
            t.exit_reason  = stmt.column_text(11);
            t.bars_in_trade= stmt.column_int(12);
            t.zone_id      = stmt.column_int(13);
            result.push_back(t);
        }
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] get_all_closed_trades failed: " << e.what());
    }
    return result;
}

// ============================================================
// count_open_trades
// ============================================================

int SqliteTradeRepository::count_open_trades(const std::string& symbol) {
    try {
        auto stmt = conn_.prepare(
            "SELECT COUNT(*) FROM trades "
            "WHERE symbol=? AND trade_status='OPEN'");
        stmt.bind_text(1, symbol);
        if (stmt.step()) return stmt.column_int(0);
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] count_open_trades: " << e.what());
    }
    return 0;
}

// ============================================================
// export_trades_csv
// Produces the exact column order of the original trades.csv
// ============================================================

std::string SqliteTradeRepository::export_trades_csv(const std::string& symbol) {
    std::ostringstream out;

    // Header — matches CSVReporter::write_trade_header exactly
    out << "Trade#,Direction,Entry Date,Exit Date,Entry Price,Exit Price,"
        << "Stop Loss,Take Profit,Position Size,Risk Amount,Reward Amount,"
        << "P&L,Return %,Bars in Trade,Exit Reason,Zone ID,Zone Score,"
        << "Base Strength,Elite Bonus,Swing Score,Regime Score,"
        << "State Freshness,Rejection Score,Recommended RR,Score Rationale,"
        << "Aggressiveness,Regime,Zone Formation,Zone Distal,Zone Proximal,"
        << "Swing Percentile,At Swing Extreme,"
        << "Fast EMA,Slow EMA,RSI,BB Upper,BB Middle,BB Lower,BB Bandwidth,"
        << "ADX,+DI,-DI,MACD Line,MACD Signal,MACD Histogram,"
        << "Zone Departure Vol Ratio,Zone Initiative,Zone Vol Climax,"
        << "Zone Vol Score,Zone Institutional Index,"
        << "Entry Pullback Vol Ratio,Entry Volume Score,Entry Volume Pattern,"
        << "Entry Position Lots,Position Size Reason,"
        << "Exit Volume Ratio,Exit Was Vol Climax\n";

    try {
        std::string sql =
            "SELECT t.id, t.direction, t.entry_time, t.exit_time,"
            "  t.entry_price, t.exit_price, t.stop_loss, t.take_profit,"
            "  t.position_size, t.net_pnl, t.bars_in_trade, t.exit_reason,"
            "  t.zone_id, t.entry_position_lots,"
            "  t.exit_volume, t.exit_was_vol_climax,"
            "  s.zone_score, s.score_base_strength, s.score_elite_bonus,"
            "  s.score_swing_position, s.score_regime_alignment,"
            "  s.score_state_freshness, s.score_rejection_confirmation,"
            "  s.recommended_rr, s.score_rationale, s.aggressiveness,"
            "  s.regime,"
            "  s.fast_ema, s.slow_ema, s.rsi,"
            "  s.bb_upper, s.bb_middle, s.bb_lower, s.bb_bandwidth,"
            "  s.adx, s.di_plus, s.di_minus,"
            "  s.macd_line, s.macd_signal, s.macd_histogram,"
            "  s.zone_departure_vol_ratio, s.zone_is_initiative, s.zone_vol_climax,"
            "  s.zone_vol_score, s.zone_institutional_index,"
            "  s.entry_pullback_vol_ratio, s.entry_volume_score, s.entry_volume_pattern"
            " FROM trades t LEFT JOIN signals s ON t.signal_id = s.id"
            " WHERE t.symbol=? AND t.trade_status='CLOSED'"
            " ORDER BY t.entry_time ASC";

        auto stmt = conn_.prepare(sql);
        stmt.bind_text(1, symbol);

        int row_num = 1;
        auto fmt2 = [](double v) {
            std::ostringstream s; s << std::fixed << std::setprecision(2) << v;
            return s.str();
        };

        while (stmt.step()) {
            out << row_num++ << ","
                << stmt.column_text(1) << ","         // Direction
                << stmt.column_text(2) << ","         // Entry Date
                << stmt.column_text(3) << ","         // Exit Date
                << fmt2(stmt.column_double(4)) << "," // Entry Price
                << fmt2(stmt.column_double(5)) << "," // Exit Price
                << fmt2(stmt.column_double(6)) << "," // Stop Loss
                << fmt2(stmt.column_double(7)) << "," // Take Profit
                << stmt.column_int(8) << ","          // Position Size
                << "0,"                               // Risk Amount (computed separately)
                << "0,"                               // Reward Amount
                << fmt2(stmt.column_double(9)) << "," // P&L
                << "0,"                               // Return %
                << stmt.column_int(10) << ","         // Bars in Trade
                << stmt.column_text(11) << ","        // Exit Reason
                << stmt.column_int(12) << ","         // Zone ID
                << fmt2(stmt.column_double(16)) << "," // Zone Score
                << fmt2(stmt.column_double(17)) << "," // Base Strength
                << fmt2(stmt.column_double(18)) << "," // Elite Bonus
                << fmt2(stmt.column_double(19)) << "," // Swing Score
                << fmt2(stmt.column_double(20)) << "," // Regime Score
                << fmt2(stmt.column_double(21)) << "," // State Freshness
                << fmt2(stmt.column_double(22)) << "," // Rejection Score
                << fmt2(stmt.column_double(23)) << "," // Recommended RR
                << stmt.column_text(24) << ","         // Score Rationale
                << fmt2(stmt.column_double(25)) << "," // Aggressiveness
                << stmt.column_text(26) << ","         // Regime
                << ","                                 // Zone Formation (from zones table)
                << "0,0,"                              // Zone Distal, Proximal
                << "50.0,NO,"                          // Swing Percentile, At Swing Extreme
                << fmt2(stmt.column_double(27)) << "," // Fast EMA
                << fmt2(stmt.column_double(28)) << "," // Slow EMA
                << fmt2(stmt.column_double(29)) << "," // RSI
                << fmt2(stmt.column_double(30)) << "," // BB Upper
                << fmt2(stmt.column_double(31)) << "," // BB Middle
                << fmt2(stmt.column_double(32)) << "," // BB Lower
                << fmt2(stmt.column_double(33)) << "," // BB Bandwidth
                << fmt2(stmt.column_double(34)) << "," // ADX
                << fmt2(stmt.column_double(35)) << "," // +DI
                << fmt2(stmt.column_double(36)) << "," // -DI
                << fmt2(stmt.column_double(37)) << "," // MACD Line
                << fmt2(stmt.column_double(38)) << "," // MACD Signal
                << fmt2(stmt.column_double(39)) << "," // MACD Histogram
                << fmt2(stmt.column_double(40)) << "," // Zone Departure Vol Ratio
                << (stmt.column_bool(41) ? "YES" : "NO") << "," // Zone Initiative
                << (stmt.column_bool(42) ? "YES" : "NO") << "," // Zone Vol Climax
                << fmt2(stmt.column_double(43)) << "," // Zone Vol Score
                << fmt2(stmt.column_double(44)) << "," // Zone Institutional Index
                << fmt2(stmt.column_double(45)) << "," // Entry Pullback Vol Ratio
                << stmt.column_int(46) << ","          // Entry Volume Score
                << stmt.column_text(47) << ","         // Entry Volume Pattern
                << stmt.column_int(13) << ","          // Entry Position Lots
                << ","                                  // Position Size Reason
                << fmt2(stmt.column_double(14)) << "," // Exit Volume Ratio
                << (stmt.column_bool(15) ? "YES" : "NO") // Exit Was Vol Climax
                << "\n";
        }
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] export_trades_csv failed: " << e.what());
    }
    return out.str();
}

// ============================================================
// export_trades_csv_all — portfolio master
// ============================================================

std::string SqliteTradeRepository::export_trades_csv_all() {
    // Get all active symbols from registry, then union per-symbol exports
    // For now: query trades table directly with a symbol column prepended
    std::ostringstream out;
    out << "Symbol,Trade#,Direction,Entry Date,Exit Date,"
        << "Entry Price,Exit Price,Stop Loss,Take Profit,"
        << "Position Size,P&L,Bars in Trade,Exit Reason,Zone ID\n";

    try {
        auto stmt = conn_.prepare(
            "SELECT symbol, id, direction, entry_time, exit_time,"
            "  entry_price, exit_price, stop_loss, take_profit,"
            "  position_size, net_pnl, bars_in_trade, exit_reason, zone_id"
            " FROM trades WHERE trade_status='CLOSED'"
            " ORDER BY entry_time ASC");

        int n = 0;
        while (stmt.step()) {
            out << stmt.column_text(0) << ","   // Symbol
                << ++n << ","
                << stmt.column_text(2) << ","
                << stmt.column_text(3) << ","
                << stmt.column_text(4) << ","
                << std::fixed << std::setprecision(2)
                << stmt.column_double(5) << ","
                << stmt.column_double(6) << ","
                << stmt.column_double(7) << ","
                << stmt.column_double(8) << ","
                << stmt.column_int(9) << ","
                << stmt.column_double(10) << ","
                << stmt.column_int(11) << ","
                << stmt.column_text(12) << ","
                << stmt.column_int(13) << "\n";
        }
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteTradeRepo] export_trades_csv_all failed: " << e.what());
    }
    return out.str();
}

} // namespace Persistence
} // namespace SDTrading