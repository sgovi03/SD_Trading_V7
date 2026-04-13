// ============================================================
// SD TRADING V8 - EVENT TYPE DEFINITIONS
// src/events/event_types.h
// Milestone M2.4
//
// All events published on the in-process EventBus.
// Every event is a plain struct — no virtual dispatch, no heap.
// Publishers know nothing about subscribers.
//
// Wire format from pipe (set by AFL DataBridge):
//   SOURCE|SYMBOL|TIMEFRAME|TIMESTAMP|O|H|L|C|V|OI
//   e.g.: AMIBROKER|NIFTY|5min|2026-04-09T09:30:00+05:30|
//         22950.00|23010.50|22940.00|22985.00|142500|9875400
// ============================================================

#ifndef SDTRADING_EVENT_TYPES_H
#define SDTRADING_EVENT_TYPES_H

// Prevent Windows.h from defining ERROR, BOOL, min, max macros
// that collide with C++ identifiers. Must appear before any
// Windows/WinSock/curl headers in this translation unit.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef ERROR
#undef ERROR
#endif

#include <string>
#include <cstdint>
#include "common_types.h"   // Bar, Zone, Trade types

namespace SDTrading {
namespace Events {

// ============================================================
// BAR VALIDATED EVENT
// Published by BarValidator after a bar passes all checks.
// Subscribers: ScannerOrchestrator (SYNC), DbWriter (ASYNC).
// ============================================================
struct BarValidatedEvent {
    std::string  symbol;
    std::string  timeframe;          // "5min"
    std::string  timestamp;          // ISO8601: 2026-04-09T09:30:00+05:30
    double       open;
    double       high;
    double       low;
    double       close;
    double       volume;
    double       open_interest;
    std::string  source;             // "AMIBROKER"

    // Derived fields populated by validator
    double       prev_close  = 0.0;  // For gap check logging
    double       prev_atr    = 0.0;  // For spike check logging
};

// ============================================================
// BAR REJECTED EVENT
// Published by BarValidator when a bar fails any check.
// Subscribers: DbWriter (logs to bar_rejections table).
// ============================================================
struct BarRejectedEvent {
    std::string  symbol;
    std::string  timeframe;
    std::string  raw_wire_data;      // Original pipe message (for audit)
    int          rejection_tier;     // 1=structural, 2=anomaly, 3=sequence
    std::string  rejection_reason;   // Human-readable description
    double       prev_close  = 0.0;
    double       prev_atr    = 0.0;
    double       prev_oi     = 0.0;
    double       raw_oi      = 0.0;
};

// ============================================================
// SIGNAL EVENT
// Published by ScannerOrchestrator when a zone entry fires.
// Subscribers: DbWriter (signals table), SpringBootConsumer,
//              AlertManager.
// ============================================================
struct SignalEvent {
    std::string  symbol;
    std::string  signal_time;        // ISO8601
    std::string  direction;          // "LONG" | "SHORT"
    double       score;
    int          zone_id;
    std::string  zone_type;          // "DEMAND" | "SUPPLY"
    double       entry_price;
    double       stop_loss;
    double       target_1;
    double       target_2;
    double       rr_ratio;
    int          lot_size;
    std::string  order_tag;          // SDT+MMDDHHMMSS+[L/S]+[CC]

    // Score breakdown (for audit/analytics)
    double       score_base_strength     = 0.0;
    double       score_elite_bonus       = 0.0;
    double       score_swing             = 0.0;
    double       score_regime            = 0.0;
    double       score_freshness         = 0.0;
    double       score_rejection         = 0.0;
    std::string  score_rationale;
    std::string  regime;             // "BULL" | "BEAR" | "RANGING"

    // Market state at signal time
    double       fast_ema       = 0.0;
    double       slow_ema       = 0.0;
    double       rsi            = 0.0;
    double       adx            = 0.0;
    double       di_plus        = 0.0;
    double       di_minus       = 0.0;
    double       macd_line      = 0.0;
    double       macd_signal    = 0.0;
    double       macd_histogram = 0.0;
    double       bb_upper       = 0.0;
    double       bb_middle      = 0.0;
    double       bb_lower       = 0.0;
    double       bb_bandwidth   = 0.0;

    // Volume/OI at signal time
    double       zone_departure_vol_ratio  = 0.0;
    bool         zone_is_initiative        = false;
    bool         zone_vol_climax           = false;
    double       zone_vol_score            = 0.0;
    double       zone_institutional_index  = 0.0;
    double       entry_pullback_vol_ratio  = 0.0;
    int          entry_volume_score        = 0;
    std::string  entry_volume_pattern;
    double       entry_volume              = 0.0;
    double       entry_oi                  = 0.0;
};

// ============================================================
// TRADE OPEN EVENT
// Published by Spring Boot layer after successful order fill.
// Subscriber: DbWriter (trades table).
// ============================================================
struct TradeOpenEvent {
    std::string  order_tag;
    std::string  symbol;
    std::string  direction;
    std::string  entry_time;
    double       entry_price;
    double       stop_loss;
    double       take_profit;
    int          lots;
    int          signal_id;         // FK into signals table
    int          zone_id;
    std::string  broker_order_id;   // Fyers order ID
};

// ============================================================
// TRADE CLOSE EVENT
// Published by Spring Boot layer after position is closed.
// Subscriber: DbWriter (updates trades table), MetricsUpdater.
// ============================================================
struct TradeCloseEvent {
    std::string  order_tag;
    std::string  symbol;
    std::string  exit_time;
    double       exit_price;
    std::string  exit_reason;       // "TP"|"SL"|"TRAIL_SL"|"MANUAL"|"EOD"
    double       gross_pnl;
    double       brokerage;
    double       net_pnl;
    int          bars_in_trade;
    double       exit_volume        = 0.0;
    bool         exit_was_vol_climax = false;
};


// ============================================================
// ZONE SNAPSHOT EVENT
// Published by LiveEngine after bootstrap zone detection and
// whenever a new zone is detected during live scanning.
// Subscriber: DbWriter (upserts into zones table).
// Contains full zone data needed for DB insert/update.
// ============================================================
struct ZoneSnapshotEvent {
    // Identity
    std::string  symbol;
    int          zone_id;
    std::string  zone_type;          // "DEMAND" | "SUPPLY"

    // Geometry
    double       base_low;
    double       base_high;
    double       distal_line;
    double       proximal_line;

    // Formation
    int          formation_bar;
    std::string  formation_datetime;
    double       strength;

    // State
    std::string  state;              // "FRESH"|"TESTED"|"VIOLATED"|"RECLAIMED"|"EXHAUSTED"
    int          touch_count;
    int          consecutive_losses;
    std::string  exhausted_at_datetime;

    // Elite / geometry detail
    bool         is_elite;
    double       departure_imbalance;
    double       retest_speed;
    int          bars_to_retest;

    // Sweep / reclaim
    bool         was_swept;
    int          sweep_bar;
    int          bars_inside_after_sweep;
    bool         reclaim_eligible;

    // Flip tracking
    bool         is_flipped_zone;
    int          parent_zone_id;

    // Swing analysis
    double       swing_percentile;
    bool         is_at_swing_high;
    bool         is_at_swing_low;
    double       swing_score;

    // V6.0 Volume profile
    double       volume_ratio;
    double       departure_volume_ratio;
    bool         zone_is_initiative;
    bool         zone_has_vol_climax;
    double       zone_vol_score;
    double       institutional_index;

    // V6.0 OI profile
    double       oi_formation;
    double       oi_change_pct;
    std::string  oi_market_phase;
    double       oi_score;

    // Active flag
    bool         is_active;

    // Retry tracking
    int          entry_retry_count;

    // Full JSON blob (serialised Zone for complete fidelity)
    std::string  zone_json;
};

// ============================================================
// ZONE UPDATE EVENT
// Published by ScannerOrchestrator when zone state changes.
// Subscriber: DbWriter (zones table).
// ============================================================
struct ZoneUpdateEvent {
    std::string  symbol;
    int          zone_id;
    std::string  new_state;        // "FRESH"|"TESTED"|"VIOLATED"|"EXHAUSTED"
    std::string  updated_at;
    int          touch_count;
    int          consecutive_losses;
};

// ============================================================
// SYSTEM ALERT EVENT
// Published by any component when abnormal condition detected.
// Subscriber: AlertManager (logs, notifies).
// ============================================================
// ERR used instead of ERROR to avoid Windows SDK macro collision.
enum class AlertSeverity { INFO, WARN, ERROR, CRITICAL };

struct SystemAlertEvent {
    AlertSeverity severity;
    std::string   source;          // Component name
    std::string   symbol;          // Empty if system-wide
    std::string   message;
    std::string   timestamp;
};

} // namespace Events
} // namespace SDTrading

#endif // SDTRADING_EVENT_TYPES_H