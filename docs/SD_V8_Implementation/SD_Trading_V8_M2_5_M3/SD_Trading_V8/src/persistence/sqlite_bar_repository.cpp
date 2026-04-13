// ============================================================
// SD TRADING V8 - SQLITE BAR REPOSITORY IMPLEMENTATION
// src/persistence/sqlite_bar_repository.cpp
// Milestone M3.1
// ============================================================

#include "sqlite_bar_repository.h"
#include <sstream>

namespace SDTrading {
namespace Persistence {

SqliteBarRepository::SqliteBarRepository(SqliteConnection& conn)
    : conn_(conn) {}

// ============================================================
// insert_bar
// ============================================================

bool SqliteBarRepository::insert_bar(const Events::BarValidatedEvent& evt) {
    try {
        auto stmt = conn_.prepare(INSERT_BAR_SQL);
        stmt.bind_text(1,   evt.timestamp);
        stmt.bind_text(2,   evt.symbol);
        stmt.bind_text(3,   evt.timeframe);
        stmt.bind_double(4, evt.open);
        stmt.bind_double(5, evt.high);
        stmt.bind_double(6, evt.low);
        stmt.bind_double(7, evt.close);
        stmt.bind_double(8, evt.volume);
        stmt.bind_double(9, evt.open_interest);
        stmt.bind_text(10,  evt.source);
        stmt.execute();
        return true;
    } catch (const SqliteConstraintException&) {
        // INSERT OR IGNORE: duplicate (time, symbol, timeframe) — expected on replay
        LOG_DEBUG("[SqliteBarRepo] Duplicate bar ignored: "
                  << evt.symbol << " " << evt.timestamp);
        return true;
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteBarRepo] insert_bar failed: " << e.what());
        return false;
    }
}

// ============================================================
// insert_rejection
// ============================================================

bool SqliteBarRepository::insert_rejection(const Events::BarRejectedEvent& evt) {
    try {
        auto stmt = conn_.prepare(INSERT_REJECTION_SQL);
        stmt.bind_text(1,   evt.symbol);
        stmt.bind_text(2,   evt.timeframe);
        stmt.bind_text(3,   evt.raw_wire_data);
        stmt.bind_int(4,    evt.rejection_tier);
        stmt.bind_text(5,   evt.rejection_reason);
        stmt.bind_double(6, evt.prev_close);
        stmt.bind_double(7, evt.prev_atr);
        stmt.bind_double(8, evt.raw_oi);
        stmt.bind_double(9, evt.prev_oi);
        stmt.execute();
        return true;
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteBarRepo] insert_rejection failed: " << e.what());
        return false;
    }
}

// ============================================================
// get_bars
// ============================================================

std::vector<Core::Bar> SqliteBarRepository::get_bars(
    const std::string& symbol,
    const std::string& from_ts,
    const std::string& to_ts,
    const std::string& timeframe)
{
    std::vector<Core::Bar> result;
    try {
        std::string sql =
            "SELECT time, open, high, low, close, volume, open_interest "
            "FROM bars WHERE symbol=? AND timeframe=? "
            "AND time >= ? AND time <= ? "
            "ORDER BY time ASC";
        auto stmt = conn_.prepare(sql);
        stmt.bind_text(1, symbol);
        stmt.bind_text(2, timeframe);
        stmt.bind_text(3, from_ts);
        stmt.bind_text(4, to_ts);
        while (stmt.step()) {
            Core::Bar bar;
            bar.datetime = stmt.column_text(0);
            bar.open     = stmt.column_double(1);
            bar.high     = stmt.column_double(2);
            bar.low      = stmt.column_double(3);
            bar.close    = stmt.column_double(4);
            bar.volume   = stmt.column_double(5);
            bar.oi       = stmt.column_double(6);
            result.push_back(bar);
        }
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteBarRepo] get_bars failed: " << e.what());
    }
    return result;
}

// ============================================================
// count_bars
// ============================================================

int SqliteBarRepository::count_bars(const std::string& symbol,
                                     const std::string& timeframe) {
    try {
        auto stmt = conn_.prepare(
            "SELECT COUNT(*) FROM bars WHERE symbol=? AND timeframe=?");
        stmt.bind_text(1, symbol);
        stmt.bind_text(2, timeframe);
        if (stmt.step()) return stmt.column_int(0);
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteBarRepo] count_bars failed: " << e.what());
    }
    return 0;
}

// ============================================================
// count_rejections
// ============================================================

int SqliteBarRepository::count_rejections(const std::string& symbol,
                                           const std::string& since_ts) {
    try {
        std::string sql = since_ts.empty()
            ? "SELECT COUNT(*) FROM bar_rejections WHERE symbol=?"
            : "SELECT COUNT(*) FROM bar_rejections WHERE symbol=? AND rejected_at >= ?";
        auto stmt = conn_.prepare(sql);
        stmt.bind_text(1, symbol);
        if (!since_ts.empty()) stmt.bind_text(2, since_ts);
        if (stmt.step()) return stmt.column_int(0);
    } catch (const SqliteException& e) {
        LOG_ERROR("[SqliteBarRepo] count_rejections failed: " << e.what());
    }
    return 0;
}

} // namespace Persistence
} // namespace SDTrading
