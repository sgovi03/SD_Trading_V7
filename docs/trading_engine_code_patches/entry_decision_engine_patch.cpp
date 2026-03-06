
/*
PATCH: Add confirmation candle filter inside calculate_entry()
*/

if (current_bar != nullptr && bar_history != nullptr) {

    const Bar& bar = *current_bar;

    double body = std::abs(bar.close - bar.open);
    double range = bar.high - bar.low;
    double body_pct = (range > 0) ? body / range : 0;

    bool rejection = false;

    if (zone.type == ZoneType::DEMAND) {
        rejection = (bar.close > bar.open) && (body_pct > 0.5);
    } else {
        rejection = (bar.close < bar.open) && (body_pct > 0.5);
    }

    if (!rejection) {
        decision.should_enter = false;
        decision.rejection_reason = "No rejection candle";
        return decision;
    }
}
