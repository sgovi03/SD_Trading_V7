# SD Trading V8 — Impact Analysis & Implementation Roadmap
## Based on Codebase Review: V8_codebase.zip

---

# PART 1: CODEBASE ASSESSMENT

## 1.1 Architecture Overview (Current State)

```
SD Trading V8 — Current Architecture
│
├── Modes: backtest | dryrun | live         (ITradingEngine interface)
├── Factory: EngineFactory                  (creates engine by mode)
├── Symbol: Single (NIFTY) via system_config.json
├── Config: Two-tier (system_config.json + phase_6_config_v0_7.txt)
├── Data: CSV file polling (live_data.csv via LiveDataReader)
├── Broker: FyersAdapter (data + execution — both from Fyers)
├── Persistence: JSON files (zones_live.json, zones_live_master.json)
├── Output: CSV files (trades.csv, metrics.csv, equity_curve.csv)
└── Persistence layer: stub only (CMakeLists says "Milestone 4")
```

## 1.2 Source File Inventory

| File | Lines | Role | Change Impact |
|---|---|---|---|
| `include/common_types.h` | 1607 | All shared types: Bar, Zone, Config, Trade | 🟡 Medium — Bar already has OI |
| `src/sd_engine_core.h/cpp` | ~1100 | Legacy core (used by backtest path) | 🟢 Low — minimal changes |
| `src/live/live_engine.h/cpp` | ~5000 | Live trading engine — largest file | 🔴 High — major refactor |
| `src/backtest/backtest_engine.cpp` | 2296 | Backtest loop engine | 🟡 Medium — symbol-agnostic changes |
| `src/zones/zone_detector.h/cpp` | ~1200 | Zone detection algorithm | 🟢 Low — already symbol-agnostic |
| `src/scoring/entry_decision_engine.h/cpp` | ~1000 | Entry scoring, lot sizing | 🟡 Medium — lot_size parameterisation |
| `src/core/config_loader.h/cpp` | ~1050 | Strategy config parser | 🔴 High — 3-tier cascade needed |
| `src/core/system_config_loader.h/cpp` | ~400 | system_config.json parser | 🔴 High — multi-symbol registry |
| `src/backtest/csv_reporter.h/cpp` | ~250 | trades/metrics/equity CSV export | 🔴 High — replace with SQLite |
| `src/backtest/performance_tracker.h/cpp` | ~400 | Metrics calculation | 🟢 Low — reusable as-is |
| `src/ZonePersistenceAdapter.h/cpp` | ~450 | zones JSON save/load | 🔴 High — replace with SQLite |
| `src/live/broker_interface.h` | ~200 | Abstract broker interface | 🟢 Low — add OI validation hook |
| `src/live/fyers_adapter.h/cpp` | ~300 | Fyers broker implementation | 🔴 High — demote to execution only |
| `src/live_data_reader.h` | ~200 | CSV file polling (live data) | 🔴 High — replace with pipe reader |
| `src/dryrun/dryrun_engine.h/cpp` | ~400 | DryRun inherits LiveEngine | 🟡 Medium — update constructor |
| `src/EngineFactory.h/cpp` | ~250 | Engine creation factory | 🔴 High — multi-symbol orchestration |
| `src/main_unified.cpp` | ~200 | Entry point | 🔴 High — scanner mode entry |
| `src/persistence/CMakeLists.txt` | stub | Not implemented | 🔴 High — implement with SQLite |
| `system_config.json` | ~100 | System configuration | 🔴 High — multi-symbol registry |
| `conf/phase_6_config_v0_7.txt` | ~400 | Strategy config | 🔴 High — becomes MASTER.config |

---

## 1.3 What Is Already Well-Designed (Preserve)

These components require minimal or zero changes and should be preserved:

**`ITradingEngine` interface** — The abstract interface `initialize() / run() / stop()` is exactly
the right pattern. The scanner orchestrator will implement this same interface.

**`BrokerInterface`** — Clean abstract interface. FyersAdapter decouples from the interface
correctly. Just needs to be limited to execution only.

**`ZoneDetector`** — Already fully symbol-agnostic. Takes `Config&` and `Bar` vectors.
No NIFTY references. Zero changes needed for multi-symbol support.

**`EntryDecisionEngine`** — Symbol-agnostic. Operates on zone + bars + config.
Minimal changes needed (lot_size parameterisation only).

**`PerformanceTracker`** — Clean metrics computation, no file dependencies. Reusable as-is.

**`ZoneStateManager`** — Handles FRESH/TESTED/VIOLATED lifecycle. Fully generic.

**`DryRunEngine`** — Correct design: inherits LiveEngine 100%, swaps broker only.

**`Bar` struct** — Already has `double oi` field. OI already in the type system.

**Volume/OI scorers** (`VolumeScorer`, `OIScorer`) — Already implemented in V6.0.
Symbol-agnostic, config-driven.

**`common_types.h`** type system — `VolumeProfile`, `OIProfile`, `MarketPhase`,
`EntryVolumeMetrics`, `ZoneScore` are all well-designed and reusable.

---

## 1.4 What Must Change (Impact Areas)

### IMPACT AREA 1 — Data Ingestion Layer
**Current:** `LiveDataReader` polls `live_data.csv` by file modification time.
Data source is Fyers (unreliable, caused ₹9,500 P&L damage on a single trade).

**Required:** AmiBroker AFL → Named Pipe → `PipeListener` → `BarValidator` → Event Bus.
`LiveDataReader` is retired entirely. `FyersAdapter` demoted to execution-only.

**Files affected:**
- `src/live_data_reader.h` → **RETIRE**
- `src/live/fyers_adapter.h/cpp` → **MODIFY** (strip data methods)
- New: `src/data/pipe_listener.h/cpp`
- New: `src/data/bar_validator.h/cpp`
- New: `afl/SD_DataBridge.afl`

---

### IMPACT AREA 2 — Config System
**Current:** Two-tier config. `system_config.json` has one `trading.symbol` string.
`phase_6_config_v0_7.txt` is a single flat strategy config. No inheritance.

**Required:** Three-tier cascade: `MASTER.config → classes/{CLASS}.config → symbols/{SYMBOL}.config`.
Config resolved once per symbol at startup into an immutable `ConfigMap`.

**Files affected:**
- `src/core/config_loader.h/cpp` → **MAJOR REFACTOR** (cascade resolution)
- `src/core/system_config_loader.h/cpp` → **MAJOR REFACTOR** (symbol registry)
- `system_config.json` → **REPLACE** with `symbol_registry.json` + `MASTER.config`
- `include/common_types.h` → **MINOR** (add `SymbolProfile`, `SymbolRegistry`)

---

### IMPACT AREA 3 — Multi-Symbol Orchestration
**Current:** `EngineFactory` creates one engine for one symbol. `main_unified.cpp`
runs one engine instance. No scanner concept.

**Required:** `ScannerOrchestrator` manages N `SymbolEngineContext` instances.
Thread pool (8 workers) processes symbols concurrently. Signal ranking layer.
`ComputeBackend` strategy (CPU/CUDA) selected at startup.

**Files affected:**
- `src/EngineFactory.h/cpp` → **REPLACE** with `ScannerOrchestrator`
- `src/main_unified.cpp` → **MAJOR REFACTOR** (scanner entry point)
- New: `src/scanner/scanner_orchestrator.h/cpp`
- New: `src/scanner/symbol_engine_context.h`
- New: `src/scanner/compute_backend.h`
- New: `src/scanner/cpu_backend.h/cpp`
- New: `src/scanner/cuda_backend.h/cu` (Phase 2)
- New: `src/events/event_bus.h/cpp`
- New: `src/events/event_types.h`

---

### IMPACT AREA 4 — LiveEngine Symbol Coupling
**Current:** `LiveEngine` constructor takes `symbol` and `interval` as strings.
`trading_symbol` is a public member used throughout 4,931 lines.
Zone files are named `zones_live.json` / `zones_live_master.json` (single symbol).

**Required:** `LiveEngine` receives symbol context from `SymbolEngineContext`.
Zone files become `zones_{SYMBOL}.json`. Output dirs become `output/symbols/{SYMBOL}/`.
`LiveEngine` becomes one instance per symbol, not a singleton.

**Files affected:**
- `src/live/live_engine.h/cpp` → **REFACTOR** (output path parameterisation,
  zone file naming, context injection)

**Key finding — threading safety:**
Static variables found in `live_engine.cpp`:
- `regime_log_counter` in backtest_engine → local static, safe per instance
- `curl_initialized` in http_client → process-wide, needs mutex for thread pool
- Function-level statics in `sd_engine_core.cpp` → file-scope functions, safe

`LiveEngine` itself has no process-wide static state — safe for thread pool instantiation
(one instance per worker, no shared mutable state between symbol threads).

---

### IMPACT AREA 5 — Persistence Layer
**Current:** `ZonePersistenceAdapter` writes JSON files.
`CSVReporter` writes trades/metrics/equity CSV files.
Persistence `CMakeLists.txt` is a stub ("Milestone 4 — not implemented").

**Required:** SQLite (WAL mode) replaces all file-based persistence.
DuckDB for analytical queries. Master portfolio aggregation via DB views.
The persistence stub is the natural insertion point for this work.

**Files affected:**
- `src/ZonePersistenceAdapter.h/cpp` → **REPLACE** with `SqliteZoneRepository`
- `src/backtest/csv_reporter.h/cpp` → **REPLACE** with `SqliteTradeRepository`
- `src/persistence/CMakeLists.txt` → **IMPLEMENT** (was stub)
- New: `src/persistence/sqlite_zone_repository.h/cpp`
- New: `src/persistence/sqlite_trade_repository.h/cpp`
- New: `src/persistence/sqlite_bar_repository.h/cpp`
- New: `src/persistence/db_schema.sql`
- New: `src/persistence/duckdb_analytics.h/cpp`

---

### IMPACT AREA 6 — Output File Structure
**Current:** All outputs go to `results/live_trades/` flat directory.
`trades.csv`, `metrics.csv`, `equity_curve.csv` are single-symbol files.
Zone files go to the output_dir specified in system_config.

**Required:** Per-symbol SQLite tables + portfolio aggregation views.
File exports available on-demand. Directory structure per design.

---

# PART 2: IMPACT SUMMARY MATRIX

```
┌────────────────────────────────────────────────────────────────────┐
│  COMPONENT                    │ CHANGE TYPE  │ EFFORT │ RISK       │
├────────────────────────────────────────────────────────────────────┤
│  ITradingEngine interface     │ Preserve     │ None   │ None       │
│  BrokerInterface              │ Minor        │ Low    │ Low        │
│  ZoneDetector                 │ Preserve     │ None   │ None       │
│  EntryDecisionEngine          │ Minor        │ Low    │ Low        │
│  PerformanceTracker           │ Preserve     │ None   │ None       │
│  ZoneStateManager             │ Preserve     │ None   │ None       │
│  Volume/OI scorers            │ Preserve     │ None   │ None       │
│  common_types.h               │ Additive     │ Low    │ Low        │
│  DryRunEngine                 │ Minor        │ Low    │ Low        │
│  Bar struct (OI already in)   │ Preserve     │ None   │ None       │
├────────────────────────────────────────────────────────────────────┤
│  LiveDataReader               │ Retire       │ Low    │ Low        │
│  FyersAdapter (data path)     │ Modify       │ Medium │ Medium     │
│  PipeListener (new)           │ New build    │ Medium │ Medium     │
│  BarValidator (new)           │ New build    │ Medium │ Low        │
│  AFL DataBridge (new)         │ New build    │ Low    │ Low        │
├────────────────────────────────────────────────────────────────────┤
│  ConfigLoader                 │ Major refactor│ High  │ Medium     │
│  SystemConfigLoader           │ Major refactor│ High  │ Medium     │
│  MASTER.config (new)          │ New build    │ Medium │ Low        │
│  Symbol registry              │ New build    │ Medium │ Low        │
├────────────────────────────────────────────────────────────────────┤
│  EngineFactory → Orchestrator │ Replace      │ High   │ High       │
│  ScannerOrchestrator (new)    │ New build    │ High   │ Medium     │
│  SymbolEngineContext (new)    │ New build    │ Medium │ Low        │
│  EventBus (new)               │ New build    │ Medium │ Low        │
│  ComputeBackend CPU (new)     │ New build    │ Medium │ Low        │
│  ComputeBackend CUDA (new)    │ New build    │ High   │ Medium     │
├────────────────────────────────────────────────────────────────────┤
│  LiveEngine (symbol coupling) │ Refactor     │ High   │ High       │
│  main_unified.cpp             │ Major refactor│ Medium│ Medium     │
├────────────────────────────────────────────────────────────────────┤
│  ZonePersistenceAdapter       │ Replace      │ High   │ Medium     │
│  CSVReporter                  │ Replace      │ High   │ Medium     │
│  SQLite repositories (new)    │ New build    │ High   │ Medium     │
│  DuckDB analytics (new)       │ New build    │ Medium │ Low        │
│  DB schema + migrations (new) │ New build    │ Medium │ Low        │
└────────────────────────────────────────────────────────────────────┘
```

---

# PART 3: IMPLEMENTATION ROADMAP

## Guiding Principles
1. **Never break the working NIFTY system during development**
2. **Each milestone delivers a testable, runnable system**
3. **Shadow-write validates DB before retiring file outputs**
4. **Backtest parity must be verified at each milestone**
5. **CUDA backend is additive — CPU path always works**

---

## MILESTONE 0 — Foundation & Preparation
**Duration: 1 week**
**Goal: Set up infrastructure without touching engine logic**

### Deliverables

**M0.1 — Directory restructure**
```
Create new directory layout:
  config/MASTER.config          (copy from phase_6_config_v0_7.txt)
  config/classes/INDEX_FUTURES.config
  config/symbols/NIFTY.config   (symbol-specific only)
  config/symbols/BANKNIFTY.config
  symbol_registry.json          (single symbol: NIFTY, active=true)
  output/symbols/NIFTY/         (existing outputs move here)
  output/master/                (future portfolio aggregates)
  state/zones/NIFTY/            (zone JSON files move here)
```

**M0.2 — SQLite schema creation**
```
Implement src/persistence/ (the existing stub):
  db_schema.sql                 (all tables per design document)
  SqliteConnection              (WAL mode, connection pool)
  Schema migrations support     (versioned DDL)
```

**M0.3 — CMake updates**
```
Add SQLite3 dependency (find_package or FetchContent)
Add DuckDB dependency
Create persistence library CMakeLists.txt (replace stub)
Ensure existing build still works (no engine changes yet)
```

**M0.4 — AFL DataBridge script**
```
afl/SD_DataBridge.afl:
  Bar close detection
  OHLCV + OI capture
  Named pipe write per symbol
  Error handling (pipe unavailable → log, skip)
Test: manually write to pipe, verify format
```

**Verification:** Existing NIFTY backtest produces identical results to pre-M0.

---

## MILESTONE 1 — Three-Tier Config System
**Duration: 1.5 weeks**
**Goal: Config cascade working for NIFTY; foundation for multi-symbol**

### Deliverables

**M1.1 — ConfigLoader refactor**
```
Implement cascade resolution:
  load_master(MASTER.config)
  load_class(classes/INDEX_FUTURES.config) → overrides master
  load_symbol(symbols/NIFTY.config) → overrides class
  Returns: immutable resolved ConfigMap

Backward compatible: existing phase_6_config_v0_7.txt still loads
as master if new structure not present (migration bridge)
```

**M1.2 — SymbolRegistry**
```
symbol_registry.json parser → SymbolRegistry class
SymbolProfile struct:
  symbol, display_name, asset_class, lot_size, tick_size,
  expiry_day, live_feed_ticker, config_file, active flag
SystemConfigLoader updated to load registry instead of
single trading.symbol string
```

**M1.3 — Common types additions**
```
Add to common_types.h:
  struct SymbolProfile
  struct SymbolRegistry
  struct ResolvedConfig (immutable, post-cascade)
```

**M1.4 — Validation**
```
Config validation: all required fields present after cascade
Symbol registry validation: active symbols have valid profiles
Log resolved config per symbol at startup (audit trail)
```

**Verification:**
- NIFTY loads via new cascade, produces same resolved values as old flat config
- Add BANKNIFTY to registry (inactive), verify it loads without error

---

## MILESTONE 2 — Data Ingestion: AmiBroker Pipeline
**Duration: 2 weeks**
**Goal: Replace Fyers data feed with AmiBroker named pipe**

### Deliverables

**M2.1 — PipeListener**
```
src/data/pipe_listener.h/cpp:
  One thread per active symbol pipe
  Block-read on \\.\pipe\SD_{SYMBOL}_{TIMEFRAME}
  Parse wire format: SOURCE|SYMBOL|TF|TIMESTAMP|O|H|L|C|V|OI
  Pass RawBar to BarValidator
  Reconnect policy (10 retries, 500ms wait)
  Disconnect alert after threshold
```

**M2.2 — BarValidator**
```
src/data/bar_validator.h/cpp:
  Tier 1: Structural (H>=L, C in [L,H], O in [L,H], V>0, timestamp monotonic)
  Tier 2: Anomaly (gap%, ATR spike, volume spike, OI spike, zero OI alert)
  Tier 3: Sequence (no duplicates, no session gap, bar count bounds)
  Config-driven thresholds from MASTER.config
  Publishes BarValidatedEvent or BarRejectedEvent to EventBus
```

**M2.3 — EventBus**
```
src/events/event_bus.h/cpp:
  In-process pub/sub
  Subscriber registration by event type
  SYNC dispatch (for engine — zero latency)
  ASYNC dispatch (for DB writer — non-blocking)
  Thread-safe subscriber map
```

**M2.4 — EventTypes**
```
src/events/event_types.h:
  BarValidatedEvent    (full OHLCV + OI + metadata)
  BarRejectedEvent     (raw data + rejection reason + tier)
  SignalEvent          (full signal + order tag)
  TradeOpenEvent
  TradeCloseEvent
  ZoneUpdateEvent
  SystemAlertEvent
```

**M2.5 — FyersAdapter demoted**
```
Strip get_latest_bar(), get_historical_bars(), get_ltp()
  from FyersAdapter data path — these become no-ops
  (AmiBroker is now the data source)
Keep: place_market_order(), place_limit_order(),
  place_stop_order(), cancel_order() — execution only
LiveDataReader → retire (delete, remove from CMakeLists)
```

**M2.6 — Shadow mode**
```
Run PipeListener alongside existing LiveDataReader
  for 1 week in parallel
Compare bar streams: AmiBroker vs Fyers
Log all discrepancies (this will document the bad data problem)
Go/No-go gate: validator rejection rate < 0.1% to proceed
```

**Verification:**
- AmiBroker pipe delivers bars; validator logs show Tier 1/2/3 pass rates
- Bad candle equivalent (manual test) correctly rejected at Tier 2
- NIFTY live session runs on AmiBroker data, Fyers executes orders only

---

## MILESTONE 3 — SQLite Persistence Layer
**Duration: 2 weeks**
**Goal: All data writes go to SQLite; files retired after shadow validation**

### Deliverables

**M3.1 — SqliteBarRepository**
```
src/persistence/sqlite_bar_repository.h/cpp:
  insert_bar(BarValidatedEvent)    → bars table
  insert_rejection(BarRejectedEvent) → bar_rejections table
  get_bars(symbol, from, to)       → for historical queries
  WAL mode, prepared statements, connection pool
```

**M3.2 — SqliteZoneRepository**
```
src/persistence/sqlite_zone_repository.h/cpp:
  Replaces ZonePersistenceAdapter JSON files
  save_zones(symbol, zones)        → zones table (UPSERT)
  load_zones(symbol)               → restore on startup
  update_zone_state(zone_id, state)
  Zone data stored as BLOB (serialized) + indexed columns
ZonePersistenceAdapter → RETIRE after shadow validation
```

**M3.3 — SqliteTradeRepository**
```
src/persistence/sqlite_trade_repository.h/cpp:
  Replaces CSVReporter
  insert_signal(SignalEvent)       → signals table (57 columns per design)
  insert_trade_open(TradeOpenEvent)
  update_trade_close(TradeCloseEvent)
  get_trades(symbol)
  get_signals(symbol)
CSVReporter → RETIRE after shadow validation
```

**M3.4 — SqliteSessionRepository**
```
src/persistence/sqlite_session_repository.h/cpp:
  upsert_session_state(symbol, state)
  get_session_state(symbol)
  insert_config_audit(symbol, resolved_config_json)
```

**M3.5 — DB Writer subscriber**
```
src/persistence/db_writer.h/cpp:
  Subscribes to EventBus (ASYNC)
  Routes events to correct repository
  Batches writes where possible (< 50ms latency)
  Write-ahead queue (bounded, non-blocking to engine)
```

**M3.6 — Shadow write phase**
```
Week 1: Write to both SQLite AND existing JSON/CSV
Automated comparison: SQLite query vs CSV content
Week 2: Switch reads to SQLite, keep CSV writes
Week 3 (start of M4): Retire CSV/JSON writes
```

**M3.7 — DuckDB analytics setup**
```
src/persistence/duckdb_analytics.h/cpp:
  Attach SQLite file as DuckDB data source
  Pre-built analytical queries:
    win_rate_by_regime()
    pnl_by_zone_score_bucket()
    bt_vs_lt_parity_report()
    indicator_correlation(indicator_col)
  Offline only — never called during live session
```

**Verification:**
- NIFTY trades.csv content matches SQLite trades table row-for-row
- Zone load from SQLite on restart produces identical zone state as JSON load
- DuckDB win_rate_by_regime() runs against NIFTY historical data

---

## MILESTONE 4 — Multi-Symbol Scanner Orchestrator
**Duration: 2.5 weeks**
**Goal: Scanner runs N symbols concurrently; NIFTY unchanged in behaviour**

### Deliverables

**M4.1 — SymbolEngineContext**
```
src/scanner/symbol_engine_context.h:
  symbol: string
  profile: SymbolProfile
  resolved_config: ResolvedConfig (immutable)
  zone_detector: ZoneDetector (own instance)
  zone_state_mgr: ZoneStateManager (own instance)
  entry_engine: EntryDecisionEngine (own instance)
  scorer: ZoneScorer (own instance)
  bar_buffer: CircularBuffer<Bar> (last N bars)
  active_zones: vector<Zone>
  session_metrics: MetricsSnapshot
  engine_state: enum { READY, PROCESSING, SUSPENDED }
  last_bar_timestamp: int64
```

**M4.2 — ComputeBackend interface**
```
src/scanner/compute_backend.h:
  virtual vector<SignalResult> score_symbols(
    vector<SymbolState>&) = 0
  virtual string backend_name() = 0

src/scanner/cpu_backend.h/cpp:
  Thread pool (N workers, configurable)
  Task queue: one task per symbol
  Worker: zone load → score → return result
  No file I/O inside worker threads
  Results collected via future.get()
```

**M4.3 — ScannerOrchestrator**
```
src/scanner/scanner_orchestrator.h/cpp:
  load_symbol_registry()
  for each active symbol:
    resolve_config(MASTER → CLASS → SYMBOL)
    load_zone_state_from_db(symbol)
    create SymbolEngineContext
    register PipeListener thread

  subscribe to EventBus:
    on BarValidatedEvent:
      find context for symbol
      append bar to context buffer
      submit scoring task to ComputeBackend
      on result:
        if signal: publish SignalEvent
        if zone changed: publish ZoneUpdateEvent
        update session metrics

  on market close:
    flush all zone states to DB
    compute session + portfolio metrics
    publish end-of-session summary
```

**M4.4 — Signal ranking layer**
```
After all symbols scored per bar:
  collect signals from this bar window
  filter by scanner_signal_min_score
  rank by scanner_rank_by (SCORE | RR | ZONE_STRENGTH)
  write live_signals.json (for Spring Boot consumption)
  update signals table in SQLite
```

**M4.5 — Portfolio position guard**
```
In ScannerOrchestrator:
  max_portfolio_positions check before emitting signal
  symbol already in position → suppress signal
  correlated symbol deduplication (configurable)
```

**M4.6 — LiveEngine refactor for multi-symbol**
```
LiveEngine constructor: add symbol_profile parameter
  output_dir → output/symbols/{SYMBOL}/
  zone files → state/zones/{SYMBOL}/zones_live.json
  trading_symbol already exists, just drive from profile

Remove singleton assumptions in live_engine.cpp:
  No process-wide state shared across instances
  curl_initialized in http_client → mutex-protected
```

**Verification:**
- Run NIFTY alone: results identical to pre-M4 baseline
- Add BANKNIFTY (active=true): both symbols process concurrently
- Verify no zone state leakage between symbol instances
- Portfolio position guard prevents >N concurrent trades

---

## MILESTONE 5 — Spring Boot Integration Update
**Duration: 1 week**
**Goal: Order execution layer handles multi-symbol signals**

### Deliverables

**M5.1 — Signal consumption update**
```
Spring Boot reads live_signals.json (existing mechanism)
Add symbol field routing:
  signal.symbol → correct Fyers ticker from symbol registry
  signal.order_tag → order tracking (existing SDT format)
Multi-symbol position tracking:
  open_positions map: symbol → order_tag
  max_portfolio_positions guard (mirrors C++ guard)
```

**M5.2 — REST API additions**
```
GET  /api/scanner/signals/live     (existing + symbol filter)
GET  /api/symbols                  (symbol registry endpoint)
PUT  /api/symbols/{symbol}/active  (enable/disable symbol)
GET  /api/config/{symbol}          (resolved config per symbol)
GET  /api/metrics/portfolio        (cross-symbol metrics)
```

**M5.3 — TradeEvent bridge**
```
Spring Boot publishes TradeOpenEvent / TradeCloseEvent
  back to C++ EventBus (via HTTP or shared SQLite)
SQLite is the simpler path: Spring Boot writes to same DB
C++ DB writer reads trade status on startup for recovery
```

**Verification:**
- BANKNIFTY signal emitted, Spring Boot routes order to correct Fyers ticker
- Portfolio max position guard fires correctly when limit reached
- Order tag links signal → trade in SQLite end-to-end

---

## MILESTONE 6 — Master Portfolio Aggregation
**Duration: 1 week**
**Goal: Portfolio-level equity curve, metrics, and cross-symbol analysis**

### Deliverables

**M6.1 — Portfolio metrics computation**
```
After market close:
  DuckDB reads all symbol trades from SQLite
  Computes portfolio equity curve (sum P&L by timestamp)
  Computes cross-symbol metrics table (one row per symbol)
  Writes portfolio row to equity_curve_portfolio table
  Writes metrics summary to metrics table (period_type=SESSION)
```

**M6.2 — Master CSV export (on-demand)**
```
REST API endpoint: GET /api/export/trades-master
  DuckDB query → all symbols trades union
  Returns CSV download
Replaces the old trades_master.csv manual aggregation
```

**M6.3 — DuckDB analytical queries**
```
Expose via REST API or CLI:
  /api/analytics/win-rate-by-regime?symbol=ALL
  /api/analytics/pnl-by-score-bucket?symbol=NIFTY
  /api/analytics/bt-vs-lt-parity
  /api/analytics/indicator-correlation?indicator=rsi
```

**Verification:**
- Portfolio equity curve correctly sums NIFTY + BANKNIFTY P&L
- Win rate by regime query returns valid results across symbols
- BT vs LT parity report runs end-to-end

---

## MILESTONE 7 — CUDA Backend (Optional / Phase 2)
**Duration: 2 weeks**
**Goal: RTX 4050 accelerates signal scoring for 50+ symbols**

### Deliverables

**M7.1 — CUDA backend implementation**
```
src/scanner/cuda_backend.h/cu:
  Implements ComputeBackend interface
  Batch SymbolState vectors → GPU memory
  Scoring kernel (one thread per zone per symbol)
  Transfer SignalResult vectors → CPU memory
  Error handling → automatic fallback to CPU
```

**M7.2 — AUTO backend selection**
```
BackendFactory::create(config):
  if compute_backend = CPU → CPUBackend
  if compute_backend = CUDA → CUDABackend (throws if unavailable)
  if compute_backend = AUTO:
    detect CUDA → symbol count >= threshold → CUDABackend
    any failure → CPUBackend (log warning)
```

**M7.3 — CMake CUDA integration**
```
find_package(CUDA OPTIONAL_COMPONENTS)
if CUDA_FOUND: enable_language(CUDA), link cuda_backend.cu
else: CUDA_AVAILABLE=0, factory always returns CPUBackend
Compile with: nvcc -arch=sm_89 (RTX 4050 = Ada Lovelace)
```

**M7.4 — GPU backtest acceleration**
```
Offline backtest mode: submit all symbols as GPU batch
  Zone scoring across full history in parallel
  Parameter sweep: N configs × M symbols on GPU
  Speedup target: 20-50x vs sequential CPU backtest
```

**Verification:**
- 50 symbols scored in single CUDA kernel launch
- Results identical between CPU and CUDA backends
- AUTO mode correctly selects CUDA on development machine
- Graceful CPU fallback when CUDA driver not available

---

# PART 4: EFFORT & DEPENDENCY SUMMARY

## Timeline

```
Week 1      : M0 — Foundation & Preparation
Week 2-3    : M1 — Three-Tier Config System
Week 4-5    : M2 — AmiBroker Data Pipeline
Week 6-7    : M3 — SQLite Persistence Layer
Week 8-10   : M4 — Multi-Symbol Scanner Orchestrator
Week 11     : M5 — Spring Boot Integration Update
Week 12     : M6 — Portfolio Aggregation
Week 13-14  : M7 — CUDA Backend (optional)
```

## Dependency Chain

```
M0 (Foundation)
  └─► M1 (Config)
        └─► M2 (Data Pipeline)    M3 (SQLite)
              └──────────────────────┴──► M4 (Scanner)
                                              └─► M5 (Spring Boot)
                                                    └─► M6 (Portfolio)
                                                          └─► M7 (CUDA)
```

M2 and M3 can proceed in parallel after M1.

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| LiveEngine refactor breaks NIFTY | Medium | High | Shadow run: old + new in parallel for 2 weeks |
| AmiBroker pipe timing mismatch at bar close | Medium | Medium | ±30s timestamp window in validator |
| SQLite WAL contention (C++ + Spring Boot) | Low | Medium | Connection pool, WAL mode, serialized writes via DB writer |
| Config cascade regression in existing parameters | Medium | Medium | Config hash comparison: resolved config must match old flat config for NIFTY |
| Zone state divergence on multi-symbol startup | Low | High | Replay historical bars per symbol (existing bootstrap mechanism) |
| CUDA kernel correctness vs CPU path | Medium | High | CPU/CUDA result comparison test before live use |

## What Stays Identical (Zero Risk)

These components are untouched across all milestones:

- Zone detection algorithm (`ZoneDetector`)
- Entry scoring logic (`EntryDecisionEngine`, `ZoneScorer`)
- Trade management (`TradeManager`, `PerformanceTracker`)
- Volume/OI scorers (`VolumeScorer`, `OIScorer`)
- Zone state lifecycle (`ZoneStateManager`)
- All enums, structs in `common_types.h` (additive only)
- DryRun engine (inherits LiveEngine, picks up changes automatically)
- Backtest engine (zone detection + scoring unchanged)

---

# PART 5: FIRST IMPLEMENTATION SESSION RECOMMENDATION

Given the dependency chain, the highest-value first session is:

**Start with M0.2 (SQLite schema) + M1.1 (Config cascade)**

Reason: Both are pure additions with zero risk to the running system.
The schema defines the target state for all subsequent milestones.
The config cascade is the foundation everything else depends on.
Neither requires touching live_engine.cpp or the trading logic.

Concretely:
1. Implement `db_schema.sql` with all tables
2. Implement `SqliteConnection` (WAL mode, basic CRUD)
3. Refactor `ConfigLoader` to support cascade resolution
4. Add `SymbolRegistry` to `SystemConfigLoader`
5. Verify: NIFTY resolved config is byte-identical to current flat config
```
