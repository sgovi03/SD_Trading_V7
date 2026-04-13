// ============================================================
// SD TRADING V8 - SQLITE BAR REPOSITORY
// src/persistence/sqlite_bar_repository.h
// Milestone M3.1
//
// Replaces: file-based live_data.csv write path
// Writes to: bars table and bar_rejections table (M0.2 schema)
//
// Called by: DbWriter (async EventBus subscriber)
// Never called on the engine's critical path.
// ============================================================

#ifndef SDTRADING_SQLITE_BAR_REPOSITORY_H
#define SDTRADING_SQLITE_BAR_REPOSITORY_H

#include "sqlite_connection.h"
#include "../events/event_types.h"
#include "../utils/logger.h"
#include <memory>
#include <string>
#include <vector>
#include "common_types.h"

namespace SDTrading {
namespace Persistence {

class SqliteBarRepository {
public:
    explicit SqliteBarRepository(SqliteConnection& conn);

    // ── Write path (called by DbWriter on BarValidatedEvent) ──

    // Insert a validated bar into the bars table.
    // Uses INSERT OR IGNORE so re-plays are safe.
    // Returns true on success.
    bool insert_bar(const Events::BarValidatedEvent& evt);

    // Insert a rejection record into bar_rejections table.
    bool insert_rejection(const Events::BarRejectedEvent& evt);

    // ── Read path (for historical queries, DuckDB analytics) ──

    // Load bars for a symbol between two ISO8601 timestamps.
    std::vector<Core::Bar> get_bars(const std::string& symbol,
                                    const std::string& from_ts,
                                    const std::string& to_ts,
                                    const std::string& timeframe = "5min");

    // Count bars for a symbol (for diagnostics).
    int count_bars(const std::string& symbol,
                   const std::string& timeframe = "5min");

    // Count rejections for a symbol since a timestamp.
    int count_rejections(const std::string& symbol,
                         const std::string& since_ts = "");

private:
    SqliteConnection& conn_;

    // Prepared statement SQL (prepared lazily)
    static constexpr const char* INSERT_BAR_SQL =
        "INSERT OR IGNORE INTO bars "
        "(time, symbol, timeframe, open, high, low, close, volume, open_interest, source) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    static constexpr const char* INSERT_REJECTION_SQL =
        "INSERT INTO bar_rejections "
        "(symbol, timeframe, raw_data, rejection_tier, rejection_reason, "
        " prev_close, prev_atr, oi_value, prev_oi) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
};

} // namespace Persistence
} // namespace SDTrading

#endif // SDTRADING_SQLITE_BAR_REPOSITORY_H
