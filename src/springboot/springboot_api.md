# SD Trading V8 — Spring Boot REST API Contract
## Milestone M5

This document defines all HTTP endpoints that the Spring Boot
microservice must expose for V8 multi-symbol scanner integration.

---

## Existing V7 Endpoints (unchanged)

### POST /trade/multipleOrderSubmit
Submit a new futures order.
```json
{
  "quantity": 1,
  "strategyNumber": 13,
  "weekNum": 0,
  "monthNum": 0,
  "type": 1,
  "orderTag": "SDT0409093000LCC"
}
```
Response: `{"s":"ok","id":"FYERS_ORDER_ID"}`

### POST /trade/squareOffPositionsByOrderTag
Exit a specific open position by order tag.
```json
{ "orderTag": "SDT0409093000LCC" }
```

---

## New V8 Endpoints

### GET /api/scanner/signals/live
Returns the latest pending signal for each active symbol.
Used by dashboards and monitoring tools.
```json
{
  "signals": [
    {
      "symbol": "NIFTY",
      "direction": "LONG",
      "score": 72.5,
      "entry_price": 22985.0,
      "stop_loss": 22850.0,
      "target": 23200.0,
      "rr_ratio": 1.8,
      "order_tag": "SDT0409093000LCC",
      "signal_time": "2026-04-09T09:30:00+05:30"
    }
  ]
}
```

### PUT /api/symbols/{symbol}/active
Activate or deactivate a symbol in the scanner at runtime.
```
PUT /api/symbols/BANKNIFTY/active
Body: { "active": true }
Response: { "symbol": "BANKNIFTY", "active": true }
```

### GET /api/scanner/status
Returns current scanner health: active symbols, open positions,
signals emitted today, last bar timestamp per symbol.
```json
{
  "active_symbols": ["NIFTY", "BANKNIFTY"],
  "open_positions": 1,
  "signals_today": 3,
  "symbols": {
    "NIFTY": { "last_bar": "2026-04-09T10:15:00+05:30", "state": "READY" },
    "BANKNIFTY": { "last_bar": "2026-04-09T10:15:00+05:30", "state": "READY" }
  }
}
```

### POST /api/trade/close  ← Spring Boot calls this when Fyers fills exit
Spring Boot writes the exit data directly to the shared SQLite
trades table. The C++ TradeCloseListener detects the update via
polling. No HTTP call back into C++ is needed.

Spring Boot UPDATE query (pseudo-code):
```sql
UPDATE trades SET
  exit_time    = ?,
  exit_price   = ?,
  exit_reason  = ?,   -- TP | SL | TRAIL_SL | MANUAL | EOD
  gross_pnl    = ?,
  brokerage    = ?,
  net_pnl      = ?,
  bars_in_trade= ?,
  trade_status = 'CLOSED',
  updated_at   = datetime('now','localtime')
WHERE order_tag = ?
```

---

## Data Flow Summary

```
AmiBroker ──pipe──► C++ Engine ──SignalEvent──► SignalConsumer
                                                     │
                                         POST /trade/multipleOrderSubmit
                                                     │
                                               Spring Boot
                                                     │
                                              Fyers executes
                                                     │
                                         Spring Boot writes exit
                                         to trades table (SQLite)
                                                     │
                                         TradeCloseListener polls
                                         (every 5 seconds)
                                                     │
                                         publishes TradeCloseEvent
                                                     │
                             ┌───────────────────────┤
                             │                       │
                          DbWriter              SignalConsumer
                    (updates equity_curve)   (decrements position count)
```
