-- ============================================================
-- SD TRADING V8 - SQLITE DATABASE SCHEMA
-- Milestone M0.2
-- 
-- Design principles:
--   WAL mode enabled at connection time (not here)
--   All timestamps stored as TEXT in ISO8601 format
--   REAL for all price/financial values (SQLite REAL = double)
--   INTEGER for all counts and flags (booleans are 0/1)
--   JSON stored as TEXT for complex structures
--   Schema versioned via schema_migrations table
-- ============================================================

PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;
PRAGMA foreign_keys = ON;
PRAGMA cache_size   = -32000;   -- 32 MB page cache
PRAGMA temp_store   = MEMORY;

-- ============================================================
-- SCHEMA MIGRATIONS (version tracking)
-- ============================================================

CREATE TABLE IF NOT EXISTS schema_migrations (
    version         INTEGER     PRIMARY KEY,
    description     TEXT        NOT NULL,
    applied_at      TEXT        NOT NULL DEFAULT (datetime('now', 'localtime'))
);

INSERT OR IGNORE INTO schema_migrations (version, description)
VALUES (1, 'Initial schema - M0.2');

-- ============================================================
-- SYMBOL REGISTRY
-- Source of truth for all tradeable symbols.
-- Replaces the single trading.symbol in system_config.json.
-- ============================================================

CREATE TABLE IF NOT EXISTS symbol_registry (
    symbol                  TEXT        PRIMARY KEY,           -- e.g. NIFTY, BANKNIFTY
    display_name            TEXT        NOT NULL,
    asset_class             TEXT        NOT NULL,              -- INDEX_FUTURES, STOCK_FUTURES
    exchange                TEXT        NOT NULL DEFAULT 'NSE',
    lot_size                INTEGER     NOT NULL,
    tick_size               REAL        NOT NULL,
    margin_per_lot          REAL,
    expiry_day              TEXT,                              -- THURSDAY, WEDNESDAY
    live_feed_ticker        TEXT,                              -- NSE:NIFTY26APRFUT
    config_file_path        TEXT,                              -- symbols/NIFTY.config
    active                  INTEGER     NOT NULL DEFAULT 1,    -- 0/1 boolean
    created_at              TEXT        NOT NULL DEFAULT (datetime('now', 'localtime')),
    updated_at              TEXT        NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- Seed: NIFTY (preserves existing system behaviour)
INSERT OR IGNORE INTO symbol_registry (
    symbol, display_name, asset_class, exchange,
    lot_size, tick_size, margin_per_lot,
    expiry_day, live_feed_ticker, config_file_path, active
) VALUES (
    'NIFTY', 'Nifty 50 Futures', 'INDEX_FUTURES', 'NSE',
    75, 0.05, 60000,
    'THURSDAY', 'NSE:NIFTY26APRFUT', 'config/symbols/NIFTY.config', 1
);

-- ============================================================
-- BARS  (time-series OHLCV + OI)
-- One row per completed bar per symbol.
-- Primary index on (symbol, time) for engine lookups.
-- Bar already has OI in common_types.h Bar struct.
-- ============================================================

CREATE TABLE IF NOT EXISTS bars (
    id                      INTEGER     PRIMARY KEY AUTOINCREMENT,
    time                    TEXT        NOT NULL,              -- ISO8601: 2026-04-01 10:30:00
    symbol                  TEXT        NOT NULL REFERENCES symbol_registry(symbol),
    timeframe               TEXT        NOT NULL DEFAULT '5min',
    open                    REAL        NOT NULL,
    high                    REAL        NOT NULL,
    low                     REAL        NOT NULL,
    close                   REAL        NOT NULL,
    volume                  REAL        NOT NULL,
    open_interest           REAL        NOT NULL DEFAULT 0,
    source                  TEXT        NOT NULL DEFAULT 'AMIBROKER',
    UNIQUE (time, symbol, timeframe)
);

CREATE INDEX IF NOT EXISTS idx_bars_symbol_time
    ON bars (symbol, time DESC);

-- ============================================================
-- BAR REJECTIONS
-- Every bar that fails validation is logged here.
-- Enables audit of data quality issues (like the Fyers bad
-- candle that caused ₹9,500 P&L damage).
-- ============================================================

CREATE TABLE IF NOT EXISTS bar_rejections (
    id                      INTEGER     PRIMARY KEY AUTOINCREMENT,
    rejected_at             TEXT        NOT NULL DEFAULT (datetime('now', 'localtime')),
    symbol                  TEXT        NOT NULL,
    timeframe               TEXT        NOT NULL DEFAULT '5min',
    raw_data                TEXT        NOT NULL,              -- original pipe wire format
    rejection_tier          INTEGER     NOT NULL,              -- 1=structural, 2=anomaly, 3=sequence
    rejection_reason        TEXT        NOT NULL,
    prev_close              REAL,
    prev_atr                REAL,
    prev_oi                 REAL,
    raw_oi                  REAL
);

CREATE INDEX IF NOT EXISTS idx_bar_rejections_symbol
    ON bar_rejections (symbol, rejected_at DESC);

-- ============================================================
-- ZONES
-- One row per zone per symbol. Zone data is stored as indexed
-- columns (for querying) plus a JSON blob (for full fidelity).
-- Replaces ZonePersistenceAdapter JSON files.
-- Zone struct fields mapped directly from common_types.h Zone.
-- ============================================================

CREATE TABLE IF NOT EXISTS zones (
    zone_id                 INTEGER     PRIMARY KEY,           -- matches Zone::zone_id
    symbol                  TEXT        NOT NULL REFERENCES symbol_registry(symbol),

    -- Core geometry
    zone_type               TEXT        NOT NULL,              -- DEMAND | SUPPLY
    base_low                REAL        NOT NULL,
    base_high               REAL        NOT NULL,
    distal_line             REAL        NOT NULL,
    proximal_line           REAL        NOT NULL,

    -- Formation metadata
    formation_bar           INTEGER     NOT NULL,
    formation_datetime      TEXT        NOT NULL,
    strength                REAL        NOT NULL DEFAULT 0,

    -- State lifecycle (ZoneState enum)
    state                   TEXT        NOT NULL DEFAULT 'FRESH',  -- FRESH|TESTED|VIOLATED|RECLAIMED|EXHAUSTED
    touch_count             INTEGER     NOT NULL DEFAULT 0,
    consecutive_losses      INTEGER     NOT NULL DEFAULT 0,
    exhausted_at_datetime   TEXT,

    -- Elite zone properties
    is_elite                INTEGER     NOT NULL DEFAULT 0,    -- 0/1
    departure_imbalance     REAL        NOT NULL DEFAULT 0,
    retest_speed            REAL        NOT NULL DEFAULT 0,
    bars_to_retest          INTEGER     NOT NULL DEFAULT 0,

    -- Sweep / reclaim tracking
    was_swept               INTEGER     NOT NULL DEFAULT 0,
    sweep_bar               INTEGER     NOT NULL DEFAULT -1,
    bars_inside_after_sweep INTEGER     NOT NULL DEFAULT 0,
    reclaim_eligible        INTEGER     NOT NULL DEFAULT 0,

    -- Flip zone tracking
    is_flipped_zone         INTEGER     NOT NULL DEFAULT 0,
    parent_zone_id          INTEGER     DEFAULT -1,

    -- Swing analysis (from SwingAnalysis struct)
    swing_percentile        REAL        DEFAULT 50.0,
    is_at_swing_high        INTEGER     DEFAULT 0,
    is_at_swing_low         INTEGER     DEFAULT 0,
    swing_score             REAL        DEFAULT 0,

    -- V6.0: Volume profile summary columns (for querying)
    volume_ratio            REAL        DEFAULT 0,
    departure_volume_ratio  REAL        DEFAULT 0,
    zone_is_initiative      INTEGER     DEFAULT 0,
    zone_has_vol_climax     INTEGER     DEFAULT 0,
    zone_vol_score          REAL        DEFAULT 0,
    institutional_index     REAL        DEFAULT 0,

    -- V6.0: OI profile summary columns
    oi_formation            REAL        DEFAULT 0,
    oi_change_pct           REAL        DEFAULT 0,
    oi_market_phase         TEXT        DEFAULT 'NEUTRAL',
    oi_score                REAL        DEFAULT 0,

    -- Active flag for range-based zone filtering
    is_active               INTEGER     NOT NULL DEFAULT 1,

    -- Entry retry tracking
    entry_retry_count       INTEGER     NOT NULL DEFAULT 0,

    -- Full JSON blob for complete fidelity (volume_profile, oi_profile,
    -- state_history, zone_score breakdown, swing_analysis detail)
    zone_json               TEXT,

    -- Audit timestamps
    created_at              TEXT        NOT NULL DEFAULT (datetime('now', 'localtime')),
    updated_at              TEXT        NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_zones_symbol_active
    ON zones (symbol, is_active, state);

CREATE INDEX IF NOT EXISTS idx_zones_symbol_type
    ON zones (symbol, zone_type, state);

CREATE INDEX IF NOT EXISTS idx_zones_formation
    ON zones (symbol, formation_datetime DESC);

-- ============================================================
-- SIGNALS
-- One row per signal emitted by the scanner.
-- Contains ALL market state at signal time:
--   zone scores, indicators, volume/OI analysis.
-- Maps directly to trades.csv columns from analysis.
-- ============================================================

CREATE TABLE IF NOT EXISTS signals (
    id                          INTEGER     PRIMARY KEY AUTOINCREMENT,
    signal_time                 TEXT        NOT NULL,
    symbol                      TEXT        NOT NULL REFERENCES symbol_registry(symbol),
    direction                   TEXT        NOT NULL,          -- LONG | SHORT

    -- Zone state at signal time
    zone_id                     INTEGER     REFERENCES zones(zone_id),
    zone_score                  REAL,
    zone_formation_time         TEXT,
    zone_distal                 REAL,
    zone_proximal               REAL,

    -- Score breakdown (from ZoneScore struct)
    score_base_strength         REAL,
    score_elite_bonus           REAL,
    score_swing_position        REAL,
    score_regime_alignment      REAL,
    score_state_freshness       REAL,
    score_rejection_confirmation REAL,
    recommended_rr              REAL,
    score_rationale             TEXT,
    aggressiveness              REAL,
    swing_percentile            REAL,
    at_swing_extreme            INTEGER     DEFAULT 0,         -- 0/1

    -- Risk levels determined at signal time
    entry_price                 REAL        NOT NULL,
    stop_loss                   REAL        NOT NULL,
    target_1                    REAL        NOT NULL,
    target_2                    REAL,
    rr_ratio                    REAL,
    lot_size                    INTEGER     NOT NULL,
    order_tag                   TEXT        NOT NULL UNIQUE,   -- SDT+MMDDHHMMSS+[L/S]+[CC]

    -- Market regime at signal time
    regime                      TEXT,                          -- BULL|BEAR|RANGING

    -- Technical indicators at signal time (from Trade struct fields)
    fast_ema                    REAL,
    slow_ema                    REAL,
    rsi                         REAL,
    bb_upper                    REAL,
    bb_middle                   REAL,
    bb_lower                    REAL,
    bb_bandwidth                REAL,
    adx                         REAL,
    di_plus                     REAL,
    di_minus                    REAL,
    macd_line                   REAL,
    macd_signal                 REAL,
    macd_histogram              REAL,

    -- Volume analysis at signal time (V6.0 volume fields from Trade struct)
    zone_departure_vol_ratio    REAL,
    zone_is_initiative          INTEGER     DEFAULT 0,
    zone_vol_climax             INTEGER     DEFAULT 0,
    zone_vol_score              REAL,
    zone_institutional_index    REAL,
    entry_pullback_vol_ratio    REAL,
    entry_volume_score          INTEGER,
    entry_volume_pattern        TEXT,
    entry_volume                REAL,
    entry_oi                    REAL,
    entry_volume_ratio          REAL,

    -- Metadata
    acted_upon                  INTEGER     NOT NULL DEFAULT 0, -- 0/1: did this become a trade
    created_at                  TEXT        NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_signals_symbol_time
    ON signals (symbol, signal_time DESC);

CREATE INDEX IF NOT EXISTS idx_signals_order_tag
    ON signals (order_tag);

CREATE INDEX IF NOT EXISTS idx_signals_zone_id
    ON signals (zone_id);

CREATE INDEX IF NOT EXISTS idx_signals_regime
    ON signals (regime);

-- ============================================================
-- TRADES
-- One row per trade. Linked to signal via signal_id.
-- Contains execution data + exit behaviour.
-- Maps to all fields in Trade struct from common_types.h.
-- ============================================================

CREATE TABLE IF NOT EXISTS trades (
    id                          INTEGER     PRIMARY KEY AUTOINCREMENT,
    order_tag                   TEXT        NOT NULL UNIQUE,   -- SDT tag links signal→trade
    symbol                      TEXT        NOT NULL REFERENCES symbol_registry(symbol),
    direction                   TEXT        NOT NULL,          -- LONG | SHORT
    signal_id                   INTEGER     REFERENCES signals(id),
    zone_id                     INTEGER     REFERENCES zones(zone_id),
    trade_num                   INTEGER,                       -- Trade::trade_num

    -- Entry
    entry_time                  TEXT        NOT NULL,
    entry_price                 REAL        NOT NULL,
    entry_bar_index             INTEGER,
    entry_position_lots         INTEGER     NOT NULL,
    position_size               INTEGER,                       -- contracts
    position_size_reason        TEXT,

    -- Risk levels at entry
    stop_loss                   REAL        NOT NULL,
    take_profit                 REAL        NOT NULL,
    original_stop_loss          REAL,                          -- before trailing adjustments
    risk_amount                 REAL,
    reward_amount               REAL,
    rr_ratio                    REAL,

    -- Exit
    exit_time                   TEXT,
    exit_price                  REAL,
    exit_reason                 TEXT,       -- TP|SL|TRAIL_SL|MANUAL|EOD|SESSION_END
    exit_volume                 REAL,
    exit_oi                     REAL,
    exit_was_vol_climax         INTEGER     DEFAULT 0,         -- 0/1

    -- Trailing stop state at close
    trailing_activated          INTEGER     DEFAULT 0,         -- 0/1
    highest_price               REAL,                          -- for LONG
    lowest_price                REAL,                          -- for SHORT
    current_trail_stop          REAL,

    -- Outcome
    gross_pnl                   REAL,
    brokerage                   REAL,
    net_pnl                     REAL,
    pnl_pct                     REAL,
    return_pct                  REAL,
    bars_in_trade               INTEGER,
    mae                         REAL,                          -- Max Adverse Excursion
    mfe                         REAL,                          -- Max Favorable Excursion
    actual_rr                   REAL,

    -- Config snapshot at trade time (full resolved config as JSON)
    config_snapshot             TEXT,

    -- Status
    trade_status                TEXT        NOT NULL DEFAULT 'OPEN',  -- OPEN|CLOSED
    created_at                  TEXT        NOT NULL DEFAULT (datetime('now', 'localtime')),
    updated_at                  TEXT        NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_trades_symbol_time
    ON trades (symbol, entry_time DESC);

CREATE INDEX IF NOT EXISTS idx_trades_order_tag
    ON trades (order_tag);

CREATE INDEX IF NOT EXISTS idx_trades_status
    ON trades (trade_status);

CREATE INDEX IF NOT EXISTS idx_trades_exit_reason
    ON trades (exit_reason);

CREATE INDEX IF NOT EXISTS idx_trades_zone_id
    ON trades (zone_id);

-- ============================================================
-- EQUITY CURVE (per symbol)
-- One row per trade close event per symbol.
-- Running equity + drawdown tracking.
-- ============================================================

CREATE TABLE IF NOT EXISTS equity_curve (
    id                      INTEGER     PRIMARY KEY AUTOINCREMENT,
    time                    TEXT        NOT NULL,
    symbol                  TEXT        NOT NULL REFERENCES symbol_registry(symbol),
    equity                  REAL        NOT NULL,
    peak_equity             REAL        NOT NULL,
    drawdown                REAL,                              -- absolute drawdown from peak
    drawdown_pct            REAL,                              -- drawdown as % of peak
    trade_id                INTEGER     REFERENCES trades(id),
    UNIQUE (time, symbol)
);

CREATE INDEX IF NOT EXISTS idx_equity_curve_symbol
    ON equity_curve (symbol, time DESC);

-- ============================================================
-- EQUITY CURVE PORTFOLIO (master / cross-symbol)
-- One row per bar close. Aggregates P&L across all symbols.
-- Rebuilt by aggregator after each session.
-- ============================================================

CREATE TABLE IF NOT EXISTS equity_curve_portfolio (
    time                    TEXT        PRIMARY KEY,
    total_equity            REAL        NOT NULL,
    peak_equity             REAL        NOT NULL,
    drawdown                REAL,
    drawdown_pct            REAL,
    open_positions          INTEGER     DEFAULT 0,
    symbols_active          INTEGER     DEFAULT 0,
    session_date            TEXT                               -- YYYY-MM-DD
);

-- ============================================================
-- METRICS (per symbol, per period)
-- Computed after each session close and on demand.
-- One row per symbol per period type.
-- ============================================================

CREATE TABLE IF NOT EXISTS metrics (
    id                      INTEGER     PRIMARY KEY AUTOINCREMENT,
    symbol                  TEXT        NOT NULL,              -- or 'PORTFOLIO' for aggregate
    period_type             TEXT        NOT NULL,              -- SESSION|WEEKLY|MONTHLY|ALL
    period_start            TEXT        NOT NULL,
    period_end              TEXT        NOT NULL,

    -- Trade statistics
    total_trades            INTEGER     NOT NULL DEFAULT 0,
    winning_trades          INTEGER     NOT NULL DEFAULT 0,
    losing_trades           INTEGER     NOT NULL DEFAULT 0,
    win_rate                REAL,                              -- 0.0 to 1.0
    avg_rr                  REAL,
    profit_factor           REAL,
    expectancy              REAL,

    -- Drawdown
    max_drawdown            REAL,
    max_drawdown_pct        REAL,
    max_consecutive_wins    INTEGER,
    max_consecutive_losses  INTEGER,

    -- P&L
    gross_pnl               REAL,
    brokerage_total         REAL,
    net_pnl                 REAL,
    total_return_pct        REAL,

    -- Risk metrics
    sharpe_ratio            REAL,
    starting_capital        REAL,
    ending_capital          REAL,

    -- Timestamps
    computed_at             TEXT        NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_metrics_symbol_period
    ON metrics (symbol, period_type, period_start DESC);

-- ============================================================
-- SESSION STATE (operational, per symbol)
-- Runtime state snapshot for recovery after crash.
-- Loaded at startup, updated on each bar.
-- ============================================================

CREATE TABLE IF NOT EXISTS session_state (
    symbol                  TEXT        PRIMARY KEY REFERENCES symbol_registry(symbol),
    session_date            TEXT        NOT NULL,              -- YYYY-MM-DD
    engine_state            TEXT        NOT NULL DEFAULT 'READY',  -- READY|PROCESSING|SUSPENDED|STOPPED
    bars_received           INTEGER     NOT NULL DEFAULT 0,
    bars_rejected           INTEGER     NOT NULL DEFAULT 0,
    open_position_tag       TEXT,                              -- order_tag of open trade, NULL if none
    last_bar_time           TEXT,
    last_signal_time        TEXT,
    consecutive_losses      INTEGER     NOT NULL DEFAULT 0,
    pause_counter           INTEGER     NOT NULL DEFAULT 0,
    next_zone_id            INTEGER     NOT NULL DEFAULT 1,    -- zone ID counter (replaces next_zone_id_ in LiveEngine)
    updated_at              TEXT        NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- Seed: NIFTY session state
INSERT OR IGNORE INTO session_state (symbol, session_date, next_zone_id)
VALUES ('NIFTY', date('now', 'localtime'), 1);

-- ============================================================
-- CONFIG AUDIT LOG
-- Every session's fully resolved config snapshot per symbol.
-- Enables exact reproduction of any historical trade.
-- ============================================================

CREATE TABLE IF NOT EXISTS config_audit (
    id                      INTEGER     PRIMARY KEY AUTOINCREMENT,
    symbol                  TEXT        NOT NULL,
    session_date            TEXT        NOT NULL,              -- YYYY-MM-DD
    config_tier             TEXT        NOT NULL,              -- MASTER|CLASS|SYMBOL|RESOLVED
    config_file_path        TEXT,
    effective_config        TEXT        NOT NULL,              -- full JSON of resolved Config
    config_hash             TEXT        NOT NULL,              -- SHA256 for change detection
    recorded_at             TEXT        NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_config_audit_symbol
    ON config_audit (symbol, session_date DESC);

-- ============================================================
-- ZONE NEXT ID SEQUENCE
-- Replaces next_zone_id_ member in LiveEngine.
-- Ensures unique zone IDs across sessions and symbols.
-- ============================================================

CREATE TABLE IF NOT EXISTS zone_id_sequence (
    symbol                  TEXT        PRIMARY KEY REFERENCES symbol_registry(symbol),
    next_id                 INTEGER     NOT NULL DEFAULT 1
);

INSERT OR IGNORE INTO zone_id_sequence (symbol, next_id)
VALUES ('NIFTY', 1);

-- ============================================================
-- VIEWS: Portfolio aggregates (readable via DuckDB or SQLite)
-- ============================================================

-- Cross-symbol trade summary (replaces trades_master.csv)
CREATE VIEW IF NOT EXISTS v_trades_master AS
SELECT
    t.id,
    t.symbol,
    t.order_tag,
    t.direction,
    t.entry_time,
    t.exit_time,
    t.entry_price,
    t.exit_price,
    t.stop_loss,
    t.take_profit,
    t.entry_position_lots,
    t.exit_reason,
    t.net_pnl,
    t.pnl_pct,
    t.bars_in_trade,
    t.actual_rr,
    t.trade_status,
    -- Signal-side indicators (joined)
    s.zone_score,
    s.score_base_strength,
    s.score_elite_bonus,
    s.score_swing_position,
    s.score_regime_alignment,
    s.score_state_freshness,
    s.score_rejection_confirmation,
    s.recommended_rr,
    s.score_rationale,
    s.aggressiveness,
    s.regime,
    s.zone_formation_time,
    s.zone_distal,
    s.zone_proximal,
    s.swing_percentile,
    s.at_swing_extreme,
    s.fast_ema,
    s.slow_ema,
    s.rsi,
    s.bb_upper,
    s.bb_middle,
    s.bb_lower,
    s.bb_bandwidth,
    s.adx,
    s.di_plus,
    s.di_minus,
    s.macd_line,
    s.macd_signal,
    s.macd_histogram,
    s.zone_departure_vol_ratio,
    s.zone_is_initiative,
    s.zone_vol_climax,
    s.zone_vol_score,
    s.zone_institutional_index,
    s.entry_pullback_vol_ratio,
    s.entry_volume_score,
    s.entry_volume_pattern,
    -- Exit behaviour
    t.exit_volume,
    t.exit_was_vol_climax
FROM trades t
LEFT JOIN signals s ON t.signal_id = s.id
ORDER BY t.entry_time DESC;

-- Per-symbol performance summary (replaces metrics_master.csv)
CREATE VIEW IF NOT EXISTS v_metrics_summary AS
SELECT
    m.symbol,
    m.period_type,
    m.period_start,
    m.period_end,
    m.total_trades,
    m.winning_trades,
    m.losing_trades,
    ROUND(m.win_rate * 100, 2)      AS win_rate_pct,
    ROUND(m.avg_rr, 3)              AS avg_rr,
    ROUND(m.profit_factor, 3)       AS profit_factor,
    ROUND(m.expectancy, 2)          AS expectancy,
    ROUND(m.max_drawdown, 2)        AS max_drawdown,
    ROUND(m.max_drawdown_pct, 2)    AS max_drawdown_pct,
    ROUND(m.net_pnl, 2)             AS net_pnl,
    ROUND(m.sharpe_ratio, 3)        AS sharpe_ratio,
    m.computed_at
FROM metrics m
ORDER BY m.symbol, m.period_type, m.period_start DESC;

-- Zone health view (for live monitoring)
CREATE VIEW IF NOT EXISTS v_zone_health AS
SELECT
    z.symbol,
    z.zone_type,
    z.state,
    COUNT(*)                        AS zone_count,
    AVG(z.strength)                 AS avg_strength,
    AVG(z.touch_count)              AS avg_touches,
    AVG(z.institutional_index)      AS avg_inst_index,
    SUM(z.is_elite)                 AS elite_count
FROM zones z
WHERE z.is_active = 1
GROUP BY z.symbol, z.zone_type, z.state
ORDER BY z.symbol, z.zone_type;

-- ============================================================
-- END OF SCHEMA
-- ============================================================
