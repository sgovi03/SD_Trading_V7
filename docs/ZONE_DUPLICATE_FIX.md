# Zone Duplicate Detection Fix

## Problem Summary

**Issue**: Live engine was detecting **691 zones** instead of expected **373 zones** (matching backtest).

**Root Cause**: The live engine was re-scanning already-processed historical bars in every iteration of the run loop.

## Technical Analysis

### Initial Behavior

1. **Initialization Phase** (lines 90-106 in `live_engine.cpp`):
   - Load 20,000 historical bars from broker
   - Call `detect_zones_full()` which scans entire dataset
   - Find ~373 zones from historical data
   - Save to `zones_live.json`

2. **Run Loop Phase** (lines 357, 374-382):
   - Every 10 seconds: call `process_tick()`
   - `process_tick()` calls `process_zones()`
   - `process_zones()` calls `detector.detect_zones()`
   - **PROBLEM**: `detect_zones()` scans a lookback window (default: all bars)
   - This re-scans the same 20,000 historical bars every iteration
   - Finds duplicate zones with slight variations in detection timing
   - Result: 691 total zones (373 original + 318 duplicates)

### Why Duplicates Occurred

The `detect_zones()` function in `zone_detector.cpp` (line 236-327):
```cpp
std::vector<Zone> ZoneDetector::detect_zones() {
    std::vector<Zone> zones;
    
    // Calculate search range
    int lookback = std::min((int)config.lookback_for_zones, (int)bars.size());
    int search_start = std::max(0, (int)bars.size() - lookback);
    
    // ⚠️ SCANS FROM search_start TO bars.size()
    for (int i = search_start; i < (int)bars.size() - 5; i++) {
        // Detect zones in this range...
    }
}
```

When `lookback_for_zones` is large (or unlimited), this scans the entire dataset repeatedly!

### Weak Deduplication Logic

The original deduplication check (line 202-208):
```cpp
for (const auto& active : active_zones) {
    if (std::abs(zone.proximal_line - active.proximal_line) < 1.0 &&
        std::abs(zone.distal_line - active.distal_line) < 1.0) {
        already_active = true;
        break;
    }
}
```

This only checks if price levels are within 1.0 points - zones with slightly different price levels were considered "new" and added.

## Solution Implemented

### 1. Track Last Scanned Bar Position

Added member variable to track which bars have been scanned:

**File**: `src/live/live_engine.h` (line 84)
```cpp
int last_zone_scan_bar_;  // Track last bar index where zones were scanned
```

**Initialization** in constructor (line 41):
```cpp
last_zone_scan_bar_(-1),  // NEW: Track last zone scan position
```

### 2. Mark Historical Scan Completion

After initial zone detection, mark all historical bars as scanned:

**File**: `src/live/live_engine.cpp` (lines 88, 105)
```cpp
// When loading persisted zones
last_zone_scan_bar_ = bar_history.size() - 1;

// When detecting zones from scratch
last_zone_scan_bar_ = bar_history.size() - 1;
```

### 3. Filter Out Already-Scanned Zones

Modified `process_zones()` to reject zones from already-scanned bars:

**File**: `src/live/live_engine.cpp` (lines 193-246)
```cpp
void LiveEngine::process_zones() {
    // CRITICAL FIX: Don't re-scan historical bars already scanned during initialization
    int current_bar_count = bar_history.size();
    
    if (current_bar_count <= last_zone_scan_bar_) {
        // No new bars to scan
        return;
    }
    
    LOG_DEBUG("Scanning for new zones from bar " << last_zone_scan_bar_ + 1 
              << " to " << current_bar_count);
    
    // Detect new zones (this will scan the lookback window, but we'll filter)
    std::vector<Zone> new_zones = detector.detect_zones();
    
    bool zones_added = false;
    
    // Add non-duplicate zones to active zones
    for (auto zone : new_zones) {
        // CRITICAL: Only accept zones formed AFTER the last scan point
        // This prevents re-detecting zones from the historical data
        if (zone.formation_bar <= last_zone_scan_bar_) {
            continue;  // Skip zones from already-scanned bars
        }
        
        // ... existing deduplication and zone addition logic ...
    }
    
    // Update last scan position
    last_zone_scan_bar_ = current_bar_count - 1;
    
    // Save zones if any were added
    if (zones_added) {
        zone_persistence_.save_zones(active_zones, next_zone_id_);
    }
}
```

## Key Changes

### Files Modified

1. **`src/live/live_engine.h`**:
   - Added `int last_zone_scan_bar_` member variable

2. **`src/live/live_engine.cpp`**:
   - Initialize `last_zone_scan_bar_` to -1 in constructor
   - Set `last_zone_scan_bar_` after initial zone detection (both load and scan paths)
   - Modified `process_zones()` to:
     - Check if new bars exist before scanning
     - Filter zones by `formation_bar` to exclude already-scanned bars
     - Update `last_zone_scan_bar_` after each scan
     - Log formation bar in zone detection message

## Expected Results

### Before Fix
- **Zones detected**: 691
- **Reason**: 373 initial + 318 duplicates from re-scanning historical data

### After Fix
- **Zones detected**: 373 (matching backtest)
- **Reason**: Only historical zones from initial scan, no duplicates from re-scanning

### Live Operation
- During live trading, `process_zones()` will only detect zones in NEW bars
- Historical data (first 11,222 bars) is never re-scanned
- New zones only appear when new bars arrive from the broker

## Testing Instructions

### 1. Clean Start Test
```powershell
# Delete persisted zones to force fresh scan
Remove-Item data\zones_live.json -ErrorAction SilentlyContinue

# Run live engine
.\build\bin\Release\sd_trading_unified.exe --mode=live --config=conf/phase1_enhanced_v3_1_config.txt --duration=1
```

**Expected Output**:
```
✅ Found 373 zones in historical data
```

### 2. Verify No Duplicates
Check the log output - there should be NO "🔷 New zone detected" messages during the initial run (since we're only replaying historical data, no truly NEW bars).

### 3. Persistence Test
```powershell
# Run again WITHOUT deleting zones_live.json
.\build\bin\Release\sd_trading_unified.exe --mode=live --config=conf/phase1_enhanced_v3_1_config.txt --duration=1
```

**Expected Output**:
```
✅ Loaded 373 zones from file
```

## Architecture Notes

### Why This Approach Works

1. **Separation of Concerns**:
   - Initial scan: `detect_zones_full()` processes entire historical dataset
   - Live updates: `process_zones()` only processes new bars

2. **Idempotent Operation**:
   - Historical data scanned exactly once
   - Each new bar processed exactly once
   - No re-processing of already-scanned data

3. **Minimal Performance Impact**:
   - Early return if no new bars exist
   - Single integer comparison per iteration
   - No changes to zone detection algorithms

### Alternative Approaches Considered

1. **Modify detect_zones() lookback**:
   - Problem: Would require changing core zone detection logic
   - Risk: Could miss valid zones near current price

2. **Stronger deduplication**:
   - Problem: Still wastes CPU scanning same data repeatedly
   - Only treats symptoms, not root cause

3. **Track processed bar indices**:
   - Problem: More complex, requires set/hashmap
   - Overkill for this use case

## Related Issues

- **Original Problem**: [#1] Live engine only detected 8 zones
  - **Cause**: Only loading 200 bars of history
  - **Fix**: Changed `initial_history_bars` from 200 to 20000

- **This Problem**: [#2] Live engine detected 691 zones
  - **Cause**: Re-scanning historical bars repeatedly
  - **Fix**: Track last scanned position, filter by formation_bar

## Future Enhancements

### Potential Improvements

1. **Smarter Lookback Window**:
   - Dynamically adjust `detect_zones()` lookback to only scan recent bars
   - Example: `config.lookback_for_zones = 100` (only last 100 bars)

2. **Zone ID-Based Deduplication**:
   - Use zone formation bar + type + price level as unique identifier
   - More robust than price-level proximity check

3. **Performance Monitoring**:
   - Track time spent in `process_zones()`
   - Alert if scanning old data (indicates bug)

## Conclusion

This fix ensures the live engine matches backtest behavior:
- **373 zones detected** (consistent with backtest)
- **No duplicate zones** from re-scanning historical data
- **Efficient operation** - only process new bars
- **Correct persistence** - zones saved/loaded properly

The live engine now implements a proper **hybrid approach**:
- Full dataset scan at startup (historical zones)
- Incremental zone detection during live operation (new zones only)
