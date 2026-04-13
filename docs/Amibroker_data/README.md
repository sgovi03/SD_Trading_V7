# SD Trading System — AmiBroker Live Feed Bridge
## Migration from fyers_bridge.py → SD_LiveFeed_Reader.py

---

## Architecture

```
AmiBroker (AFL Scan — every N minutes, full watchlist)
  └─ SD_LiveFeed_Export.afl
       │  Writes confirmed closed bars only (skips forming bar)
       │  Appends to one CSV per symbol
       │  Tracks last-exported timestamp per symbol in _SD_State\
       ▼
D:\SD_System\LiveFeed\Data\         ← WATCH_DIR
  NIFTY-FUT.csv
  BANKNIFTY-FUT.csv
  FINNIFTY-FUT.csv
  MIDCPNIFTY-FUT.csv
  _SD_State\
    NIFTY-FUT.last                  ← last exported Unix timestamp (AFL state)
    BANKNIFTY-FUT.last
    ...

SD_LiveFeed_Reader.py  (always-running Python process)
  └─ Polls WATCH_DIR every 2s
  └─ Validates rows (9-column, numeric guards)
  └─ Deduplicates via last_ts (same as fyers_bridge.last_written_ts)
  └─ Appends to ENGINE_DIR per symbol

D:\SD_System\EngineData\            ← ENGINE_DIR
  NIFTY-FUT.csv                     ← SD Trading Engine (C++) reads these
  BANKNIFTY-FUT.csv
  ...

D:\SD_System\Logs\bridge_status.json  ← health/status (same JSON as before)
```

---

## Output CSV Format

```
Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OpenInterest
1774237500,2026-03-23 09:15:00,NIFTY-FUT,22821.00,22821.00,22645.80,22700.00,838435,17302870
1774237800,2026-03-23 09:20:00,NIFTY-FUT,22699.10,22747.10,22690.00,22714.60,377260,17334460
```

**Exactly 9 columns** — engine config unchanged.

> Note: The old `fyers_bridge.py` wrote 11 columns (`OI_Fresh`, `OI_Age_Seconds`).
> Those columns are removed. The engine reads only the first 9. If your C++
> engine has a hard column-count check, this is now clean.

---

## Setup

### Step 1 — AmiBroker AFL

1. Copy `SD_LiveFeed_Export.afl` → AmiBroker `Formulas\` directory.
2. **Analysis → New Analysis** → load the AFL.
3. **Apply to** = `Current Watchlist` (add all symbols you want fed).
4. **Periodicity** = your chosen timeframe (e.g., 5 minutes).
5. **Parameters** (Ctrl+R):
   - `Output Directory` = `D:\SD_System\LiveFeed\Data`
   - `Timeframe (minutes)` = `5`
6. **Automatic Analysis → Schedule** → every 5 minutes (match periodicity).
7. **Scan once manually** to initialise state files for all current symbols.

**Why scan once manually?**
The AFL writes a `.last` state file per symbol. Without it, on first
scheduled run the AFL would attempt to export the entire history in the
AmiBroker database, which is slow and unnecessary.

### Step 2 — Python Bridge

1. Edit `config.ini` — set `watch_dir`, `engine_dir`, `timeframe`.
2. Start the bridge:
   ```
   python SD_LiveFeed_Reader.py --config config.ini
   ```
3. On first launch it skips history already in source files
   and tails only new bars going forward.

### Step 3 — SD Engine Config

Point your C++ engine's `data_feed_dir` to `engine_dir`:
```ini
data_feed_dir = D:\SD_System\EngineData
```
File naming: `SYMBOL.csv` — same as old Fyers feed.

---

## Key Behavioural Differences vs fyers_bridge.py

| Aspect                      | fyers_bridge.py                             | SD_LiveFeed_Reader.py                         |
|-----------------------------|---------------------------------------------|-----------------------------------------------|
| Data source                 | Fyers history REST API (polling)            | AmiBroker AFL (file polling)                  |
| Symbol scope                | Single symbol from system_config            | All symbols in watchlist (unlimited)          |
| Timeframe                   | Fixed from system_config                    | Parameterized in AFL + config.ini             |
| OI source                   | Fyers depth API (polled every 60s)          | AmiBroker OpenInterest() field                |
| Output columns              | 11 (incl. OI_Fresh, OI_Age_Seconds)         | 9 (exact engine format, no extra columns)     |
| State tracking              | last_written_ts in memory; seed from CSV    | Same pattern per symbol; seed from engine CSV |
| Token management            | fyers_helper auto-refresh (hourly)          | Not required — no broker API dependency       |
| Deduplication               | Filter candles where ts > last_written_ts   | Same: skip rows where bar_ts <= last_ts       |
| Partial (forming) bar guard | Fyers history API excludes current bar      | AFL skips i == bars-1 (current bar)           |
| Status file                 | bridge_status.json (single symbol)          | bridge_status.json (all symbols, same schema) |
| Log rotation                | 5 MB x 5 files                              | Same                                          |

---

## Status File (bridge_status.json)

```json
{
  "status": "running",
  "timeframe": 5,
  "symbols": {
    "NIFTY-FUT": {
      "last_ts": 1774237800,
      "last_dt": "2026-03-23 09:20:00",
      "rows_written": 42
    },
    "BANKNIFTY-FUT": {
      "last_ts": 1774237800,
      "last_dt": "2026-03-23 09:20:00",
      "rows_written": 42
    }
  },
  "timestamp": "2026-03-23T09:20:05.123456"
}
```

---

## Running as a Windows Service (NSSM)

```bat
nssm install SDLiveFeed "C:\Python312\python.exe"
nssm set SDLiveFeed AppParameters "D:\SD_System\SD_LiveFeed_AB\SD_LiveFeed_Reader.py --config D:\SD_System\SD_LiveFeed_AB\config.ini"
nssm set SDLiveFeed AppDirectory  "D:\SD_System\SD_LiveFeed_AB"
nssm set SDLiveFeed Start SERVICE_AUTO_START
nssm start SDLiveFeed
```

---

## Troubleshooting

| Symptom                          | Likely cause                               | Fix                                                    |
|----------------------------------|--------------------------------------------|--------------------------------------------------------|
| No symbol CSVs in watch_dir      | AFL not run / wrong Output Directory param | Check AFL Parameter dialog; run Scan manually          |
| OI column is all zeros           | AmiBroker data plugin does not supply OI   | Expected for some plugins; engine handles 0 gracefully |
| Duplicate bars in engine CSV     | Bridge restarted + last_ts seed failed     | Check engine CSV has valid data rows (not just header) |
| Bridge logs "Watch dir missing"  | watch_dir does not exist yet               | Run AFL Scan once; it creates the directory            |
| Bridge logs "Read error"         | AFL writing while Python reads             | Harmless; AFL write lock; next poll cycle succeeds     |
