
/*
PATCH: Replace existing should_deactivate_zone() in zone_manager.cpp
*/

bool ZoneManager::should_deactivate_zone(const Zone& zone,
                                          const ZoneMetadata& metadata,
                                          const BacktestConfig& config) const {

    if (zone.state == ZoneState::VIOLATED)
        return true;

    if (zone.touch_count >= config.scoring.max_zone_touch_count)
        return true;

    double decay = 1.0 - (zone.touch_count * 0.25);
    if (decay <= 0.25)
        return true;

    if (metadata.bars_since_formation > 300)
        return true;

    return false;
}
