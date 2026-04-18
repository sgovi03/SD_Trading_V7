# Candle to Trade to Exit Process Flow (from console.txt)

## Scope
This document reconstructs the runtime flow from the latest `console.txt` run, focusing on:
- how candles are processed,
- how entry decisions are made,
- how trades are opened,
- how exits are handled,
- how DB and portfolio state are updated.

All references below are line anchors in `console.txt` from this run.

## 1) Startup and Wiring Flow

### 1.1 System and DB initialization
- System config loaded and environment bootstrapped: `console.txt:6-22`.
- SQLite opened and schema applied: `console.txt:25-29`.
- Event bus + DB writer initialized: `console.txt:39-49`.

### 1.2 Event subscriptions that drive processing
- Signal persistence subscriber (`db_signal`): `console.txt:43`.
- Trade open persistence subscriber (`db_trade_open`): `console.txt:44`.
- Trade close persistence subscriber (`db_trade_close`): `console.txt:45`.
- Signal consumer subscriber (`signal_consumer`): `console.txt:380-381`.

### 1.3 Symbol engines and zone bootstrap
- Per-symbol context created with zone/entry components: `console.txt:99-101`, `console.txt:189-191`.
- Zone cache reuse + publish to DB via EventBus: `console.txt:405-411`.
- Zone rows persisted by DB writer: `console.txt:414-437`.

### 1.4 Close listener polling path
- TradeCloseListener created and poll loop started: `console.txt:377`, `console.txt:382`, `console.txt:398`.
- Bridge confirms signal path + close polling path are active: `console.txt:383`.

---

## 2) Candle Processing Flow

### 2.0 How candles are fed into the system

The candle ingress path in this run is pipe-first (AmiBroker -> named pipe -> listener -> broker -> engine).

Ingress sequence observed in logs:
1. EventBus wires bar event channels (`BarValidatedEvent` and `BarRejectedEvent`) for DB persistence and routing (`console.txt:41-42`, `console.txt:236-237`).
2. Per-symbol `PipeBroker` is created (`console.txt:98`, `console.txt:188`).
3. AmiBroker pipe listeners are started for each symbol (`console.txt:238`, `console.txt:245`, `console.txt:250`).
4. Listener reader loops create Windows named-pipe servers and wait for AFL bridge connection (`console.txt:266-268`, `console.txt:276-278`, `console.txt:286`, `console.txt:288`).
5. System reports no CSV preload for that symbol and switches to live pipe build-up mode (`console.txt:282-283`).
6. Pipe brokers confirm pipe-mode connectivity (`console.txt:301`, `console.txt:334`).
7. On first connection, listener logs the first decoded bar payload from AmiBroker (`console.txt:438-439`).
8. Engine consumes the bar, increments in-memory history (`console.txt:443`, `console.txt:446`), and emits canonical candle print (`console.txt:448`).

At runtime, this feed loop repeats continuously:
- `New bar formed` -> `Total bars in history` -> `[CANDLE]` snapshots, e.g. `console.txt:445-448`, `console.txt:471-474`, `console.txt:298115-298118`, `console.txt:300500-300503`.

Operational note from this run:
- Startup can bootstrap zones from SQLite/cache (`console.txt:141`, `console.txt:330`, `console.txt:405-412`), but incremental candle arrival is driven by the pipe listeners in V8 mode (`console.txt:283`).

### 2.1 New candle ingestion and canonical candle print
For each bar:
1. New bar formed log appears.
2. Candle OHLCV line appears.
3. Zones are updated on touch.
4. Zone metadata is saved.
5. Entry checks run.
6. Periodic zone detection runs and may add zones.

Representative evidence:
- New bar + candle prints: `console.txt:445`, `console.txt:448`, `console.txt:300155`, `console.txt:300158`.
- Zone profile touch updates: `console.txt:449-452`, `console.txt:300159-300162`.
- Zone metadata saved: `console.txt:453`, `console.txt:300223`.
- Periodic zone detection: `console.txt:460`, `console.txt:300173`.
- Detection output (none/new): `console.txt:464`, `console.txt:300208`, `console.txt:300213`.

### 2.2 Entry gate checks inside candle loop
Entry is filtered by multiple guards before a signal is actionable:
- Time-window block: `console.txt:455`, `console.txt:459`.
- EMA filter reject: `console.txt:300165`, `console.txt:300168`.
- Freshness + BB reject: `console.txt:300170-300171`.
- Zone exhaustion reject: `console.txt:300172`.
- Re-entry guard checks zone/session memory: `console.txt:300167`.

### 2.3 Late session behavior
- New entries disabled after configured cutoff while candles/zones still process:
  `console.txt:300488`, `console.txt:300510`.

### 2.4 Source-mapped candle feed pipeline (technical)

This is the concrete code path that produces the candle logs seen in `console.txt`.

1. Process bootstrap wires feed components in `main()`:
   - Creates `EventBus` + `DbWriter` and registers async DB subscribers (`src/run_v8_live.cpp`, Step 3; `src/persistence/db_writer.cpp:29-56`).
   - Creates `BarRouter` and subscribes SYNC to `BarValidatedEvent` (`src/run_v8_live.cpp`, Step 6; `src/scanner/bar_router.cpp:22-29`).
   - Starts one `PipeListener` per active symbol (`src/run_v8_live.cpp`, Step 7).

2. `PipeListener` owns a dedicated reader thread per symbol:
   - Thread start + loop: `src/data/pipe_listener.cpp:88-114`.
   - Named-pipe server + AFL connect: `src/data/pipe_listener.cpp:338-409`.
   - First-bar log emission (`[PipeListener:...] First bar received`): `src/data/pipe_listener.cpp:157`.

3. Wire payload parsing and validation:
   - Parse `SOURCE|SYMBOL|TIMEFRAME|TIMESTAMP|O|H|L|C|V|OI` to `RawBar`: `src/data/pipe_listener.cpp:273-316`.
   - Validate raw bar and convert pass/fail events:
     - `BarValidator::validate(...)` call site: `src/data/pipe_listener.cpp:177`.
     - `to_validated_event(...)`: `src/data/bar_validator.cpp:326-343`.
     - `to_rejected_event(...)`: `src/data/bar_validator.cpp:349-377`.
   - Publish on EventBus:
     - PASS -> `BarValidatedEvent` (`src/data/pipe_listener.cpp:181-186`).
     - FAIL -> `BarRejectedEvent` (`src/data/pipe_listener.cpp:187-201`).

4. EventBus fanout by dispatch mode:
   - SYNC subscribers execute on publisher thread (`src/events/event_bus.h`, `publish()` sync branch).
   - ASYNC subscribers are enqueued and handled by single worker thread (`src/events/event_bus.h`, `publish()` async branch + `run_async_worker()`).

5. Dual consumers of each validated bar:
   - SYNC route to strategy path (`bar_router`):
     - `BarRouter::on_bar_validated(...)` converts event -> `Core::Bar` and pushes to symbol context (`src/scanner/bar_router.cpp:31-55`).
     - `SymbolContext::push_bar(...)` forwards to `PipeBroker` (`src/scanner/symbol_context.cpp:177-185`).
     - `PipeBroker::push_bar(...)` enqueues and notifies condition variable (`src/live/pipe_broker.cpp:40-61`).
   - ASYNC route to persistence path (`db_bar`):
     - `DbWriter::on_bar_validated(...)` inserts into SQLite via `SqliteBarRepository` (`src/persistence/db_writer.cpp:63-67`).

6. Live engine bar consumption and candle logs:
   - Engine blocks on `broker->get_latest_bar(...)` (`src/live/live_engine.cpp:605`).
   - `PipeBroker::get_latest_bar(...)` dequeues from queue (`src/live/pipe_broker.cpp:69-92`).
   - On new bar, engine logs:
     - `📊 New bar formed`: `src/live/live_engine.cpp:659-660`.
     - `Total bars in history`: `src/live/live_engine.cpp:668`.
     - `[CANDLE] ...`: `src/live/live_engine.cpp:673-681`.
   - Periodic detection log source:
     - `🔄 Running periodic zone detection (...)`: `src/live/live_engine.cpp:4504`.

---

## 3) Entry and Trade-Open Flow

## 3.1 Signal generation in strategy layer
- Entry signal generated logs appear in engine output:
  - `console.txt:3597`, `console.txt:3609`.
  - `console.txt:4269`, `console.txt:4281`.

## 3.2 Portfolio guard records local position state
- Position opened logs:
  - `console.txt:3632`.
  - `console.txt:4304`.

## 3.3 SignalConsumer bridge path (to Spring Boot)
For accepted signals:
1. Signal received with tag.
2. Submission attempted to Spring Boot.
3. Spring Boot accepts order.

Evidence (same trade tag `SDT0416125915S01`):
- Received: `console.txt:3660`.
- Submitting: `console.txt:3661`.
- Accepted: `console.txt:3663`.

## 3.4 Trade open persistence
- Trade open row persisted via repository and DB writer:
  - `console.txt:3664-3665`.
  - `console.txt:4336-4337`.
  - `console.txt:7806`, `console.txt:7828`.

Note: In some sequences, duplicate "Trade opened / persisted" logs appear for same tag (`console.txt:3664-3667`, `console.txt:4336-4339`). This reflects duplicate open-event handling in the observed run output.

## 3.5 Capacity gate handling
- Signals can be skipped by bridge due to portfolio full:
  - `console.txt:8401`.
  - `console.txt:10542`.

Observed behavior in logs: even with skip logs, trade open persistence lines may still appear (`console.txt:8402-8403`, `console.txt:10543-10544`), indicating local event/open path still emitted persistence events for those tags.

---

## 4) Exit and Trade-Close Flow

## 4.1 Engine-side exit detection
Typical exit sequence:
1. LIVE position closed at price.
2. Exit reason + raw PnL printed.
3. Final PnL printed.
4. Position closed summary line.

Examples:
- `console.txt:4101-4102`, `console.txt:4114`, `console.txt:4117`.
- `console.txt:6040-6041`, `console.txt:6056`, `console.txt:6059`.

## 4.2 Close persistence + equity update
- Repository closes trade row: `console.txt:4120`, `console.txt:6062`, `console.txt:8128`.
- DB writer updates equity: `console.txt:4121`, `console.txt:6063`, `console.txt:8129`.
- Equity curve exported: `console.txt:4128-4129`, `console.txt:6070-6071`.
- Trade journal updated: `console.txt:4130`, `console.txt:6072`, `console.txt:8140`.

## 4.3 Portfolio/risk update on close
- PortfolioGuard close accounting: `console.txt:4122`, `console.txt:6064`, `console.txt:8131`.
- Main confirms updated portfolio state: `console.txt:4123`, `console.txt:6065`, `console.txt:8132`.
- Daily loss limit trigger: `console.txt:8130`, `console.txt:8583`.

## 4.4 Re-entry guard after close
- Zone can be blocked or left tradeable based on exit profile:
  - Not blocked after small trail exit: `console.txt:4131`.
  - Blocked for session after stop loss: `console.txt:6073`.
  - Not blocked after profitable trail exit: `console.txt:8593`.

---

## 5) TradeCloseListener Reconciliation Path (Async Poll)

Close listener independently polls SQLite and emits close-detected events:
- Listener close-detected logs: `console.txt:6042`, `console.txt:289089-289090`, `console.txt:293331-293333`, `console.txt:300522`.

With current idempotent close handling, repeated closes are skipped at DB close update layer:
- `update_trade_close: skipped (already closed or missing)`:
  - `console.txt:6043`.
  - `console.txt:289091`, `console.txt:289095`.
  - `console.txt:293334`, `console.txt:293337`, `console.txt:293340`.
  - `console.txt:300523`.

This is the intended duplicate-close suppression behavior in the current build.

### 5.1 Source-mapped close reconciliation and dedupe

1. `TradeCloseListener` thread model:
  - Poll thread starts in `start()` (`src/springboot/trade_close_listener.cpp:33-37`).
  - Poll loop cadence log (`Polling SQLite every ...`): `src/springboot/trade_close_listener.cpp:50-52`.
  - Close scan query + event publish lives in `check_for_closes()` (`src/springboot/trade_close_listener.cpp:73-146`).

2. Close event emission:
  - `Trade closed detected` log is emitted before publish (`src/springboot/trade_close_listener.cpp:128-132`).
  - Listener publishes `TradeCloseEvent` to EventBus (`src/springboot/trade_close_listener.cpp:134`).

3. Idempotent DB close handling:
  - `DbWriter::on_trade_close(...)` wraps close update and equity update in DB transaction (`src/persistence/db_writer.cpp:91-106`).
  - `update_trade_close(...)` returns `rows > 0` only for first close (`src/persistence/sqlite_trade_repository.cpp:203-236`).
  - Duplicate replay path emits:
    - `update_trade_close: skipped (already closed or missing)` (`src/persistence/sqlite_trade_repository.cpp:229-231`).
  - If skipped, `DbWriter` exits early and avoids duplicate equity append (`src/persistence/db_writer.cpp:93-99`).

4. Dedicated listener DB connection (thread-safety):
  - `SpringBootBridge` creates a dedicated read connection for `TradeCloseListener` (`src/springboot/springboot_bridge.cpp:22-36`).
  - This avoids sharing the write connection across threads in the listener poll path.

---

## 5.2) Code-Level Architecture Notes

### 5.2.1 Primary source files for this run
- Feed ingress: `src/data/pipe_listener.h`, `src/data/pipe_listener.cpp`
- Validation/event conversion: `src/data/bar_validator.h`, `src/data/bar_validator.cpp`
- Event dispatch: `src/events/event_bus.h`
- Bar routing: `src/scanner/bar_router.h`, `src/scanner/bar_router.cpp`
- Symbol boundary + callbacks: `src/scanner/symbol_context.cpp`
- Queue bridge to engine: `src/live/pipe_broker.cpp`
- Candle processing + entry logic: `src/live/live_engine.cpp`
- Trade persistence: `src/persistence/db_writer.cpp`, `src/persistence/sqlite_trade_repository.cpp`
- Spring bridge: `src/springboot/signal_consumer.cpp`, `src/springboot/trade_close_listener.cpp`, `src/springboot/springboot_bridge.cpp`
- Orchestration/wiring: `src/run_v8_live.cpp`

### 5.2.2 Thread boundaries (important)
- PipeListener reader thread (per symbol): parse + validate + publish.
- EventBus publisher thread executes SYNC subscribers (`bar_router`) inline.
- EventBus async worker thread executes `DbWriter` handlers.
- LiveEngine thread (per symbol) blocks on `PipeBroker::get_latest_bar()`.
- TradeCloseListener polling thread queries SQLite for closes and publishes close events.

### 5.2.3 Why candle logs appear where they do
- `PipeListener` logs reflect transport-level ingress (pipe connect/wire parsing).
- `LiveEngine` logs (`New bar formed`, `[CANDLE]`) reflect strategy-level consumption after queue handoff, not raw transport arrival.
- `DbWriter` logs reflect async persistence completion, which can lag behind engine logs under load.

---

## 6) End-to-End Sequence (Condensed)

1. Startup initializes config, SQLite, EventBus, DbWriter, engines, and bridge listeners.
2. Bars stream in; each candle updates in-memory history and emits candle diagnostics.
3. Zone touches are rescored and persisted to zone metadata.
4. Entry gating applies time/EMA/freshness/re-entry/risk controls.
5. If eligible, entry signal is generated.
6. SignalConsumer submits to Spring Boot and receives accept/skip outcomes.
7. Trade open event persists to SQLite via DbWriter/SqliteTradeRepo.
8. While trade is live, running PnL updates are logged.
9. On exit, strategy emits close reason and PnL.
10. Close event persists, equity curve/journal update, and PortfolioGuard capital/daily PnL update.
11. TradeCloseListener may detect same close later; idempotent close-update logic skips duplicate DB close updates.

---

## 7) Practical Observations from This Run

- Candle and zone loop stayed active through late-session bars (up to `console.txt:300525`).
- Entry/exit loop is high-frequency with asynchronous side paths (engine close + listener reconciliation).
- Duplicate close detections are visible, but DB close updates are now idempotently skipped.
- Daily risk controls are active and repeatedly enforced after loss limit breach.

---

## 8) Suggested Next Validation Query (optional)

To cross-check a specific trade tag end-to-end in one grep pass:
- Search for one tag (example `SDT0416125915S01`) across signal receive, trade open, trade close, listener detect, and skipped update lines.
- This gives a single-trade timeline proving open->close->reconcile behavior.
