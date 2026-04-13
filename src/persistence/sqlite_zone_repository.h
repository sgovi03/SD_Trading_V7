// ============================================================
// SD TRADING V8 - SQLITE ZONE REPOSITORY
// src/persistence/sqlite_zone_repository.h
// ============================================================
#ifndef SDTRADING_SQLITE_ZONE_REPOSITORY_H
#define SDTRADING_SQLITE_ZONE_REPOSITORY_H

#include "sqlite_connection.h"
#include "../events/event_types.h"
#include "../utils/logger.h"
#include <string>
#include <vector>

namespace SDTrading {
namespace Persistence {

class SqliteZoneRepository {
public:
    explicit SqliteZoneRepository(SqliteConnection& conn);

    // Upsert a zone snapshot — INSERT if new, UPDATE if exists.
    // Keyed on (zone_id, symbol). Safe to call on every bootstrap.
    // Returns true on success.
    bool upsert_zone(const Events::ZoneSnapshotEvent& evt);

    // Update only the mutable state fields when a zone state changes.
    bool update_zone_state(const Events::ZoneUpdateEvent& evt);

    // Count zones for a symbol (for diagnostics).
    int count_zones(const std::string& symbol);

private:
    SqliteConnection& conn_;
};

} // namespace Persistence
} // namespace SDTrading

#endif // SDTRADING_SQLITE_ZONE_REPOSITORY_H
