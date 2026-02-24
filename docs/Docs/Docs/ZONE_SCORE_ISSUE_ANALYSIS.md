# Zone Score Issue Analysis

## Issue Summary
The zone scoring data is **not being persisted correctly** in the zone files:

### Master File (zones_live_master.json)
- **Missing fields**: `zone_score` and `state_history` completely absent
- **File size**: ~443KB (latest from 2/9/2026 11:47 PM)
- **Zones count**: 605 zones
- **Current fields**: Only basic zone geometry and state, no scoring data

### Active File (zones_live_active.json)  
- **Has `zone_score` field**: ✅ Present in structure
- **Has `state_history` field**: ✅ Present in structure
- **Issue**: All zone_score values are **DEFAULT/ZERO values**:
  - `base_strength_score`: 0
  - `elite_bonus_score`: 0
  - `swing_position_score`: 0
  - `regime_alignment_score`: 0
  - `state_freshness_score`: 0
  - `rejection_confirmation_score`: 0
  - `total_score`: 0
  - `entry_aggressiveness`: 0
  - `recommended_rr`: 2 (default)
  - `score_breakdown`: "" (empty)
  - `entry_rationale`: "" (empty)
- **File size**: ~756KB (larger than master, contains scoring structure but with default values)

## Root Cause Analysis

### ✅ IDENTIFIED ISSUE

The master file is created using **`save_zones_with_metadata()`** (line 683 in ZonePersistenceAdapter.cpp):
- This function intentionally omits the `zone_score` and `state_history` fields
- It only saves basic zone geometry and properties, stopping at line 743-744
- This is **intentional** but incomplete - the scoring data is lost

The active file is created using regular **`save_zones()`** (line 107):
- This function DOES include `zone_score` and `state_history`
- However, the scores are all zeros because:
  1. The scoring happens in `bootstrap_zones_on_startup()` 
  2. But subsequent zone creation in `process_zones()` doesn't re-score them
  3. Or the newly detected zones aren't being scored before they're saved

### Code Flow Issue

In `live_engine.cpp` process_zones() (line 1094):
1. New zones are created and scored: `zone.zone_score = scorer.evaluate_zone(...)`
2. Zones are added to active_zones
3. Then `save_zones()` is called → **Active file gets scored zones ✓**
4. Then `save_zones_with_metadata()` is called → **Master file loses score data ✗**

### Why `save_zones_with_metadata()` is incomplete

**Lines 730-744** in ZonePersistenceAdapter.cpp:
```cpp
<< "      \"structure_rr\": " << zone.structure_rr << ",\n"
<< "      \"structure_rr_advantage\": " << zone.structure_rr_advantage << ",\n"
<< "      \"fixed_rr_at_entry\": " << zone.fixed_rr_at_entry << "\n";  // ← Missing comma + no zone_score!

// Zone object closes here without zone_score
if (i < zones.size() - 1) {
    ss << "    },\n";
} else {
    ss << "    }\n";
}
```

The regular `save_zones()` function (lines 156-161) DOES include zone_score:
```cpp
ss << "      \"zone_score\": {\n"
   << "        \"base_strength_score\": " << zone.zone_score.base_strength_score << ",\n"
   ... all score fields ...
   << "      },\n";
```

## The Solution

Add the `zone_score` and `state_history` fields to `save_zones_with_metadata()` before the zone object closes, matching the structure in `save_zones()`.

## Files Referenced
- `d:\SD_Engine\SD_Engine_v5.0\SD_Trading_V4\src\ZonePersistenceAdapter.cpp`
- Master: `d:\SD_Engine\SD_Engine_v5.0\SD_Trading_V4\results\live_trades\zones_live_master.json`
- Active: `d:\SD_Engine\SD_Engine_v5.0\SD_Trading_V4\results\live_trades\zones_live_active.json`
