# SD Trading V8 — End-to-End Process Flow

---

## Big Picture

```
AmiBroker ──pipe──► C++ Scanner Engine ──signal──► Spring Boot ──order──► Fyers NSE
                         │                              │
                         └──────── SQLite DB ───────────┘
                                      │
                                 MetricsAggregator
                                 (session summary)
```

---

## Phase 1 — Startup (once per session)

### 1. `run_v8_live.exe` starts

- Reads `system_config.json` — loads all paths, Spring Boot URL, SQLite path, symbol registry path
- Opens SQLite DB — creates all 13 tables on first run (bars, signals, trades, zones, equity_curve, metrics etc.)
- Reads `config/symbol_registry.json` — finds NIFTY active, BANKNIFTY inactive
- Resolves config cascade for NIFTY:
  - Loads `config/MASTER.config` (all 100+ params)
  - Overlays `config/classes/INDEX_FUTURES.config` (class-level overrides)
  - Overlays `config/symbols/NIFTY.config` (symbol-specific overrides)
  - Result: one fully-merged `ResolvedConfig` for NIFTY
- Bootstraps zones for NIFTY from SQLite history (last 500 bars)
- Creates `CpuBackend` with 8 worker threads
- Creates named pipe server `\\.\pipe\SD_NIFTY_5min` and waits
- Starts `SignalConsumer` — subscribed to EventBus for signals
- Starts `TradeCloseListener` — polls SQLite every 5s for closed trades
- Starts `DbWriter` — subscribed to EventBus for all data events
- Starts `EventBus` async worker thread

### 2. AmiBroker AFL loads

- AFL detects pipe `\\.\pipe\SD_NIFTY_5min` already exists (C++ created it)
- On every 5-minute bar close, AFL connects, writes one line, disconnects:

```
AMIBROKER|NIFTY|5min|2026-04-09T09:30:00+05:30|22950|22985|22910|22975|142500|18650000
```

### 3. Spring Boot starts

- `java -jar sd-trading-service.jar`
- Fyers API token loaded
- REST endpoints ready on `localhost:8080`

### Startup Order

```
1. run_v8_live.exe       ← creates pipe server, waits for AFL
2. AmiBroker (AFL)       ← connects to pipe on first bar close
3. Spring Boot           ← ready for order submission
```

---

## Phase 2 — Live Bar Processing (every 5 minutes)

```
Bar close in AmiBroker
        │
        ▼
AFL writes wire to named pipe
SOURCE|SYMBOL|TIMEFRAME|TIMESTAMP|O|H|L|C|V|OI
        │
        ▼
PipeListener (reader thread)
  └─ ReadFile() returns one line
        │
        ▼
BarValidator (3-tier)
  Tier 1 — Structural:  H≥L, C in [L,H], V>0, timestamp not empty
  Tier 2 — Anomaly:     price spike <5%, volume within 10× baseline
  Tier 3 — Sequence:    timestamp monotonic, no duplicate bars
        │
        ├── REJECTED ──► BarRejectedEvent → EventBus → DbWriter
        │                (written to bar_rejections table with reason)
        │
        └── PASSED ───► BarValidatedEvent → EventBus
                              │
                    ┌─────────┴──────────┐
                    ▼                    ▼
              DbWriter (ASYNC)    ScannerOrchestrator (SYNC)
              writes bar to       routes bar to SymbolEngineContext
              bars table          for NIFTY
```

---

## Phase 3 — Signal Generation (on each validated bar)

```
SymbolEngineContext::on_bar(bar)
        │
        ▼
detector_.add_bar(bar)           ← feeds ZoneDetector history

Every N bars (live_zone_detection_interval_bars):
  detector_.detect_zones_no_duplicates()
  new zones added to active_zones_

zone_state_mgr_.update_zone_states()
  ← marks zones FRESH / TESTED / VIOLATED / EXHAUSTED

determine_regime()
  ← fast EMA(9) vs slow EMA(21) → BULL / BEAR / RANGING

For each active zone:
  Is price inside zone? (close between proximal and distal)
      │
      NO ──► skip
      │
      YES ▼
  scorer_.evaluate_zone(zone, regime, bar)
  ← scores on: base_strength, elite_bonus, swing_position,
               regime_alignment, state_freshness, rejection_confirmation
  Total score out of 100
      │
  score < min_zone_strength (65)? ──► skip
      │
      ▼
  entry_engine_.calculate_entry(zone, score, regime, bar)
  ← computes: entry_price, stop_loss, take_profit, lot_size, RR ratio
      │
  should_enter = false? ──► skip
      │
      ▼
  Keep highest scoring zone only (best_zone)
        │
        ▼
SignalResult emitted:
  symbol, direction, score, entry_price, stop_loss,
  target_1, rr_ratio, lot_size, order_tag (SDT0409093000LCC)
```

---

## Phase 4 — Signal Ranking and Portfolio Guard

```
CpuBackend::score_symbols()
  ← runs all active symbols in parallel (one thread each)
  ← returns vector<SignalResult>

ScannerOrchestrator::on_bar_validated()
  Filter: score >= signal_min_score (65)
  Rank:   by SCORE (or RR or ZONE_STRENGTH)
  Guard:  open_positions < max_portfolio_positions (3)
        │
        ▼
  EventBus::publish(SignalEvent)   ← ASYNC
```

---

## Phase 5 — Order Execution

```
SignalConsumer::on_signal(SignalEvent)
        │
  Duplicate tag check (submitted_tags_ set)
  Portfolio position guard (open_positions_ < 3)
        │
        ▼
  OrderSubmitter::submit_order()
  POST http://localhost:8080/trade/multipleOrderSubmit
  {
    "quantity": 1,
    "strategyNumber": 13,
    "orderTag": "SDT0409093000LCC"
  }
        │
        ├── HTTP 200 OK ──► TradeOpenEvent → EventBus
        │                   DbWriter writes to signals + trades tables
        │                   open_positions_++
        │
        └── HTTP error  ──► SystemAlertEvent WARN → EventBus
                            logged, skipped
```

---

## Phase 6 — Trade Management (Spring Boot side)

```
Spring Boot monitors open position via Fyers API
        │
  TP hit / SL hit / EOD square-off
        │
        ▼
Spring Boot executes exit order via Fyers
        │
        ▼
Spring Boot writes exit to SQLite trades table:
  UPDATE trades SET
    exit_time     = '2026-04-09T10:45:00+05:30',
    exit_price    = 23200.0,
    exit_reason   = 'TP',
    gross_pnl     = 14300.0,
    brokerage     = 120.0,
    net_pnl       = 14180.0,
    trade_status  = 'CLOSED'
  WHERE order_tag = 'SDT0409093000LCC'
```

---

## Phase 7 — Trade Close Detection

```
TradeCloseListener (polling thread, every 5s)
  SELECT * FROM trades
  WHERE trade_status = 'CLOSED'
    AND updated_at > last_processed_ts
        │
        ▼
  Found closed trade ──► TradeCloseEvent → EventBus
                              │
                    ┌─────────┴──────────┐
                    ▼                    ▼
              DbWriter (ASYNC)    SignalConsumer
              updates:            open_positions_--
              equity_curve        (ready for next signal)
              equity_curve_portfolio
```

---

## Phase 8 — Session End (Ctrl+C)

```
Ctrl+C
  │
  ▼
PipeListeners stopped       ← no more bar data
Spring Boot bridge stopped  ← no more order submissions
EventBus drained            ← all async DB writes flushed
  │
  ▼
MetricsAggregator::compute_portfolio(300000)
  Reads from SQLite trades table:
  ← win_rate, profit_factor, sharpe_ratio
  ← max_drawdown, expectancy
  ← per-symbol breakdown
  Writes to metrics table
  Exports trades CSV
  │
  ▼
Session summary printed:
╔═══════════════════════════════════════════════╗
║        SD TRADING V8 — SESSION SUMMARY        ║
║  Trades:         12                           ║
║  Win Rate:       67%                          ║
║  Profit Factor:  2.1                          ║
║  Net P&L:        ₹18,460                      ║
║  Max Drawdown:   4.2%                         ║
║  NIFTY  trades=12  wr=67%  pnl=₹18,460        ║
╚═══════════════════════════════════════════════╝
```

---

## Data Flow Summary

| Event | Producer | Consumer | Storage |
|---|---|---|---|
| Bar close | AmiBroker AFL | PipeListener | — |
| BarValidatedEvent | BarValidator | ScannerOrchestrator, DbWriter | bars table |
| BarRejectedEvent | BarValidator | DbWriter | bar_rejections table |
| SignalEvent | ScannerOrchestrator | SignalConsumer, DbWriter | signals table |
| TradeOpenEvent | SignalConsumer | DbWriter | trades table (OPEN) |
| TradeCloseEvent | TradeCloseListener | DbWriter, SignalConsumer | trades (CLOSED), equity_curve |
| SystemAlertEvent | Any component | DbWriter, Logger | alerts table |

---

## Component Responsibilities

| Component | Language | Role |
|---|---|---|
| SD_DataBridge.afl | AmiBroker AFL | Bar data source — writes OHLCV+OI to named pipe on each bar close |
| PipeListener | C++ | Named pipe server — receives bars from AFL, one thread per symbol |
| BarValidator | C++ | 3-tier validation — structural, anomaly, sequence checks |
| EventBus | C++ | In-process pub/sub — decouples producers from consumers |
| ScannerOrchestrator | C++ | Routes bars to per-symbol engines, ranks signals, guards portfolio |
| SymbolEngineContext | C++ | Per-symbol state — zones, scorer, entry engine, regime detector |
| CpuBackend | C++ | Thread pool (8 workers) — parallel scoring across symbols |
| DbWriter | C++ | Async SQLite writer — persists all events to DB |
| SignalConsumer | C++ | Submits orders to Spring Boot via HTTP POST |
| TradeCloseListener | C++ | Polls SQLite every 5s — detects Spring Boot exit writes |
| SpringBootBridge | C++ | Owns SignalConsumer + TradeCloseListener |
| Spring Boot Service | Java | Order execution — routes to Fyers, writes exits to SQLite |
| Fyers API | External | NSE futures execution |
| MetricsAggregator | C++ | Session-end P&L metrics from SQLite |
| SQLite DB | DB | Shared state between C++ and Spring Boot |

---

## Key Design Decisions

### Why AmiBroker for data, Fyers only for execution?
V7 used Fyers for both data and execution. Fyers candle data caused ₹9,500 in P&L losses from bad data. AmiBroker data is reliable — it is what the backtest was built and validated on. Fyers API is now execution-only.

### Why SQLite as the shared DB between C++ and Spring Boot?
Both processes run on the same machine. SQLite in WAL mode supports one writer and multiple readers simultaneously. Spring Boot writes trade exits directly to the shared DB — no HTTP callback needed into C++. The TradeCloseListener polls for changes every 5 seconds, which is acceptable latency since the trade is already closed by the time detection happens.

### Why named pipe instead of CSV file?
V7 used a CSV file that Fyers updated. The C++ engine polled it. Named pipes are push-based — AmiBroker writes exactly when the bar closes, C++ receives it immediately. No polling delay, no stale data risk.

### Why C++ is the pipe server, not AmiBroker?
AmiBroker's AFL uses `fopen(pipeName, "w")` which makes AFL the pipe client. The C++ engine must create the pipe server first using `CreateNamedPipe`, then AFL connects as client on each bar close.

### Why CPU thread pool instead of CUDA?
With 2 active symbols (NIFTY + BANKNIFTY), 8 CPU threads are faster than the overhead of moving data to GPU and back. The CUDA backend (M7) will activate automatically when the symbol count reaches 20+.

### OrderTag format
Every trade is tagged `SDT + MMDDHHMMSS + [L/S] + CC` (16 chars, e.g. `SDT0409093000LCC`). This tag flows from signal generation through order submission through Spring Boot through Fyers and back into the SQLite trade close record. It is the single join key that links every event in the trade lifecycle.

---

## File and Directory Layout

```
SD_Trading_V7/
├── system_config.json              ← system-level config (paths, Spring Boot URL, v8 section)
├── config/
│   ├── MASTER.config               ← all 100+ strategy params (base layer)
│   ├── classes/
│   │   └── INDEX_FUTURES.config   ← class-level overrides
│   └── symbols/
│       ├── NIFTY.config            ← NIFTY-specific overrides
│       └── BANKNIFTY.config        ← BANKNIFTY-specific overrides
├── config/symbol_registry.json     ← active symbols, lot sizes, pipe names
├── data/
│   └── sd_trading_v8.db            ← SQLite DB (shared with Spring Boot)
├── src/
│   ├── run_v8_live.cpp             ← main entry point
│   ├── core/                       ← config loader, config cascade, zone state
│   ├── data/                       ← PipeListener, BarValidator, ShadowMode
│   ├── events/                     ← EventBus, all event structs
│   ├── persistence/                ← SQLite connection, bar/trade repositories, DbWriter
│   ├── scanner/                    ← ScannerOrchestrator, SymbolEngineContext, CpuBackend
│   ├── springboot/                 ← SignalConsumer, TradeCloseListener, SpringBootBridge
│   ├── analytics/                  ← MetricsAggregator, DuckDbAnalytics
│   └── live/                       ← AmiBrokerAdapter, FyersAdapter (execution only)
├── afl/
│   └── SD_DataBridge.afl           ← AmiBroker AFL script (bar data → named pipe)
└── build/
    └── Release/bin/
        ├── run_v8_live.exe         ← main executable
        ├── verify_m0_m1.exe        ← schema + config gate test
        ├── verify_m2.exe           ← bar pipeline gate test
        ├── verify_m3.exe           ← SQLite repository gate test
        └── verify_m4.exe           ← scanner orchestrator gate test
```
