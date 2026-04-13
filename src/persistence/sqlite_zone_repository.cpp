// ============================================================
// SD TRADING V8 - SQLITE ZONE REPOSITORY IMPLEMENTATION
// src/persistence/sqlite_zone_repository.cpp
// ============================================================
#include "persistence/sqlite_zone_repository.h"

namespace SDTrading {
namespace Persistence {

SqliteZoneRepository::SqliteZoneRepository(SqliteConnection& conn)
    : conn_(conn)
{}

bool SqliteZoneRepository::upsert_zone(const Events::ZoneSnapshotEvent& e) {
    try {
        const char* sql = R"SQL(
            INSERT INTO zones (
                zone_id, symbol, zone_type,
                base_low, base_high, distal_line, proximal_line,
                formation_bar, formation_datetime, strength,
                state, touch_count, consecutive_losses, exhausted_at_datetime,
                is_elite, departure_imbalance, retest_speed, bars_to_retest,
                was_swept, sweep_bar, bars_inside_after_sweep, reclaim_eligible,
                is_flipped_zone, parent_zone_id,
                swing_percentile, is_at_swing_high, is_at_swing_low, swing_score,
                volume_ratio, departure_volume_ratio, zone_is_initiative,
                zone_has_vol_climax, zone_vol_score, institutional_index,
                oi_formation, oi_change_pct, oi_market_phase, oi_score,
                is_active, entry_retry_count, zone_json,
                created_at, updated_at
            ) VALUES (
                ?,?,?,  ?,?,?,?,  ?,?,?,
                ?,?,?,?,  ?,?,?,?,  ?,?,?,?,
                ?,?,  ?,?,?,?,  ?,?,?,?,?,?,
                ?,?,?,?,  ?,?,?,  datetime('now','localtime'), datetime('now','localtime')
            )
            ON CONFLICT(zone_id, symbol) DO UPDATE SET
                state                   = excluded.state,
                touch_count             = excluded.touch_count,
                consecutive_losses      = excluded.consecutive_losses,
                exhausted_at_datetime   = excluded.exhausted_at_datetime,
                was_swept               = excluded.was_swept,
                sweep_bar               = excluded.sweep_bar,
                bars_inside_after_sweep = excluded.bars_inside_after_sweep,
                reclaim_eligible        = excluded.reclaim_eligible,
                is_active               = excluded.is_active,
                entry_retry_count       = excluded.entry_retry_count,
                volume_ratio            = excluded.volume_ratio,
                departure_volume_ratio  = excluded.departure_volume_ratio,
                zone_is_initiative      = excluded.zone_is_initiative,
                zone_has_vol_climax     = excluded.zone_has_vol_climax,
                zone_vol_score          = excluded.zone_vol_score,
                institutional_index     = excluded.institutional_index,
                oi_formation            = excluded.oi_formation,
                oi_change_pct           = excluded.oi_change_pct,
                oi_market_phase         = excluded.oi_market_phase,
                oi_score                = excluded.oi_score,
                zone_json               = excluded.zone_json,
                updated_at              = datetime('now','localtime')
        )SQL";

        auto stmt = conn_.prepare(sql);
        int i = 1;
        stmt.bind_int   (i++, e.zone_id);
        stmt.bind_text  (i++, e.symbol);
        stmt.bind_text  (i++, e.zone_type);
        stmt.bind_double(i++, e.base_low);
        stmt.bind_double(i++, e.base_high);
        stmt.bind_double(i++, e.distal_line);
        stmt.bind_double(i++, e.proximal_line);
        stmt.bind_int   (i++, e.formation_bar);
        stmt.bind_text  (i++, e.formation_datetime);
        stmt.bind_double(i++, e.strength);
        stmt.bind_text  (i++, e.state);
        stmt.bind_int   (i++, e.touch_count);
        stmt.bind_int   (i++, e.consecutive_losses);
        stmt.bind_text  (i++, e.exhausted_at_datetime);
        stmt.bind_int   (i++, e.is_elite ? 1 : 0);
        stmt.bind_double(i++, e.departure_imbalance);
        stmt.bind_double(i++, e.retest_speed);
        stmt.bind_int   (i++, e.bars_to_retest);
        stmt.bind_int   (i++, e.was_swept ? 1 : 0);
        stmt.bind_int   (i++, e.sweep_bar);
        stmt.bind_int   (i++, e.bars_inside_after_sweep);
        stmt.bind_int   (i++, e.reclaim_eligible ? 1 : 0);
        stmt.bind_int   (i++, e.is_flipped_zone ? 1 : 0);
        stmt.bind_int   (i++, e.parent_zone_id);
        stmt.bind_double(i++, e.swing_percentile);
        stmt.bind_int   (i++, e.is_at_swing_high ? 1 : 0);
        stmt.bind_int   (i++, e.is_at_swing_low ? 1 : 0);
        stmt.bind_double(i++, e.swing_score);
        stmt.bind_double(i++, e.volume_ratio);
        stmt.bind_double(i++, e.departure_volume_ratio);
        stmt.bind_int   (i++, e.zone_is_initiative ? 1 : 0);
        stmt.bind_int   (i++, e.zone_has_vol_climax ? 1 : 0);
        stmt.bind_double(i++, e.zone_vol_score);
        stmt.bind_double(i++, e.institutional_index);
        stmt.bind_double(i++, e.oi_formation);
        stmt.bind_double(i++, e.oi_change_pct);
        stmt.bind_text  (i++, e.oi_market_phase);
        stmt.bind_double(i++, e.oi_score);
        stmt.bind_int   (i++, e.is_active ? 1 : 0);
        stmt.bind_int   (i++, e.entry_retry_count);
        stmt.bind_text  (i++, e.zone_json);

        stmt.step();
        return true;

    } catch (const std::exception& ex) {
        LOG_ERROR("[SqliteZoneRepository] upsert_zone failed for zone_id="
                  << e.zone_id << " symbol=" << e.symbol
                  << ": " << ex.what());
        return false;
    }
}

bool SqliteZoneRepository::update_zone_state(const Events::ZoneUpdateEvent& e) {
    try {
        const char* sql = R"SQL(
            UPDATE zones SET
                state              = ?,
                touch_count        = ?,
                consecutive_losses = ?,
                updated_at         = datetime('now','localtime')
            WHERE zone_id = ? AND symbol = ?
        )SQL";

        auto stmt = conn_.prepare(sql);
        stmt.bind_text(1, e.new_state);
        stmt.bind_int (2, e.touch_count);
        stmt.bind_int (3, e.consecutive_losses);
        stmt.bind_int (4, e.zone_id);
        stmt.bind_text(5, e.symbol);
        stmt.step();
        return true;

    } catch (const std::exception& ex) {
        LOG_ERROR("[SqliteZoneRepository] update_zone_state failed: " << ex.what());
        return false;
    }
}

int SqliteZoneRepository::count_zones(const std::string& symbol) {
    try {
        auto stmt = conn_.prepare(
            "SELECT COUNT(*) FROM zones WHERE symbol = ?");
        stmt.bind_text(1, symbol);
        if (stmt.step()) return stmt.column_int(0);
    } catch (...) {}
    return 0;
}

} // namespace Persistence
} // namespace SDTrading
