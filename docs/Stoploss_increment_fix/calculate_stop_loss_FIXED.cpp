// ============================================================================
// FIXED calculate_stop_loss() function
// File: src/scoring/entry_decision_engine.cpp
// Lines: 95-116 (REPLACE THIS FUNCTION)
// ============================================================================

double EntryDecisionEngine::calculate_stop_loss(const Zone& zone, double entry_price, double atr) const {
    // Always calculate the ORIGINAL stop loss (not breakeven)
    // The breakeven logic is applied later in calculate_entry()
    (void)entry_price;  // Suppress unused parameter warning
    
    double zone_height = std::abs(zone.proximal_line - zone.distal_line);
    
    // ⭐ STOP LOSS WIDENING FIX (Feb 28, 2026)
    // Based on live results analysis: 8/24 (33%) trades hit stop with 32-55pt distances
    // Problem: Stops too tight for NIFTY 1-min volatility
    // Solution: Enforce minimum 100-point stop distance
    //
    // Calculate buffer as the LARGEST of:
    // 1. Percentage of zone height (structural buffer)
    // 2. ATR multiplier (volatility buffer)
    // 3. Minimum distance (prevents noise stops) ⭐ NEW
    
    double buffer_zone = zone_height * config.sl_buffer_zone_pct / 100.0;
    double buffer_atr = atr * config.sl_buffer_atr;
    double buffer_min = config.min_stop_distance_points;
    
    // Use the LARGEST buffer (most conservative)
    double buffer = std::max<double>({buffer_zone, buffer_atr, buffer_min});
    
    // Debug logging to track buffer selection
    LOG_DEBUG("Stop buffer calculation:"
             << " zone=" << std::fixed << std::setprecision(1) << buffer_zone
             << " pts, atr=" << buffer_atr
             << " pts, min=" << buffer_min
             << " pts → final=" << buffer << " pts");
    
    // Place stop loss beyond zone with calculated buffer
    double stop_loss;
    if (zone.type == ZoneType::DEMAND) {
        stop_loss = zone.distal_line - buffer;  // Below demand zone
    } else {
        stop_loss = zone.distal_line + buffer;  // Above supply zone
    }
    
    return stop_loss;
}
