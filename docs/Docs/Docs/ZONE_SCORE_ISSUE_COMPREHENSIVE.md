# Zone Score Persistence - Complete Issue Analysis

## Executive Summary

**CONFIRMED ISSUE**: Zone scores are NOT being persisted correctly in either zone file:
- **Master File (zones_live_master.json)**: Missing `zone_score` and `state_history` fields entirely
- **Active File (zones_live_active.json)**: Fields present but ALL 605 zones have `total_score = 0` (default value)

This results in **COMPLETE LOSS of scoring data** after each live trading run.

## Detailed Analysis

### File Comparison

| Aspect | Master | Active |
|--------|--------|--------|
| **File Path** | zones_live_master.json | zones_live_active.json |
| **Size** | 443 KB | 756 KB |
| **Zone Count** | 605 | 605 |
| **zone_score Field** | ❌ MISSING | ✓ Present |
| **state_history Field** | ❌ MISSING | ✓ Present |
| **Score Values** | N/A | 🔴 ALL ZEROS (605/605) |
| **Average total_score** | N/A | 0.0 |
| **Min/Max total_score** | N/A | 0/0 |
| **Unique score values** | N/A | 1 (only value: 0) |

### Root Cause #1: Master File Missing Scores

**Location**: `src/ZonePersistenceAdapter.cpp`, lines 683-760  
**Function**: `ZonePersistenceAdapter::save_zones_with_metadata()`

**Issue**: The function saves basic zone data but stops before including scoring data:

```cpp
// Line 730-744 - Zone object closes WITHOUT zone_score
<< "      \"structure_rr_advantage\": " << zone.structure_rr_advantage << ",\n"
<< "      \"fixed_rr_at_entry\": " << zone.fixed_rr_at_entry << "\n";  // ← No comma, no score

if (i < zones.size() - 1) {
    ss << "    },\n";  // Zone object closes here
} else {
    ss << "    }\n";   // Zone object closes here
}
```

**Expected**: Should include the zone_score block that exists in `save_zones()` (lines 156-161)

### Root Cause #2: Active File Has Zero Scores

**Two Possible Issues**:

#### Issue 2a: Zones saved before scoring
When `process_zones()` adds new zones (live_engine.cpp, line 1094):
1. New zone is created with default `zone_score` (all zeros)
2. Zone is scored: `zone.zone_score = scorer.evaluate_zone(...)`
3. Zone added to `active_zones`
4. **But**: The `zone.zone_score` assignment happens in the scope of zone creation, possibly getting lost

#### Issue 2b: Initialization of Zone struct
The Zone struct's `zone_score` member may not be properly initialized:
- If Zone constructor doesn't initialize ZoneScore, it defaults to zeros
- If ZoneScore is a POD type with all numeric fields, it defaults to 0.0
- New zones created might not get properly scored before persistence

### Call Chain for Zone Persistence

```
LiveEngine::run()
  └─ LiveEngine::process_zones()  (line 1094)
     ├─ Zone zone; (created, score not set)
     ├─ zone.zone_score = scorer.evaluate_zone(...) [SHOULD happen]
     ├─ active_zones.push_back(zone)
     ├─ zone_persistence_.save_zones()  → zones_live_active.json  ❌ SCORE = 0
     └─ zone_persistence_.save_zones_with_metadata()  → zones_live_master.json  ❌ NO SCORE FIELD
```

### Evidence Summary

**Active File Sample** (first 3 zones):
```json
{
  "zone_id": 2277,
  ...
  "zone_score": {
    "base_strength_score": 0,
    "elite_bonus_score": 0,
    "swing_position_score": 0,
    "regime_alignment_score": 0,
    "state_freshness_score": 0,
    "rejection_confirmation_score": 0,
    "total_score": 0,  ← ALL ZEROS
    "entry_aggressiveness": 0,
    "recommended_rr": 2,
    "score_breakdown": "",
    "entry_rationale": ""
  }
}
```

**Master File Sample** (first zone):
```json
{
  "zone_id": 2277,
  ...
  "fixed_rr_at_entry": 0
  // ← File ends here, no zone_score or state_history
}
```

## Impact Assessment

### Severity: HIGH 🔴

- **Data Loss**: All scoring history is lost on restart
- **Zones Unranked**: No way to know which zones are best opportunities
- **Training Data Lost**: Can't analyze what scoring worked well
- **Live Trading**: Zones loaded with zero scores, affecting entry decisions

### Affected Components

1. **Zone Loading on Startup**: `bootstrap_zones_on_startup()` loads zones with zero scores
2. **Zone Selection**: Can't prioritize zones by score
3. **Trading Decisions**: Entry logic depends on scores
4. **Backtesting**: Any backtest analyzing zone scores will show all zeros
5. **Performance Analysis**: Score metrics are meaningless

## Recommended Fix Priority

| Priority | Issue | Effort | Impact |
|----------|-------|--------|--------|
| **P1 - CRITICAL** | Add zone_score to master file save | 30 min | Fixes data preservation |
| **P1 - CRITICAL** | Investigate why active file has zero scores | 1-2 hours | Fixes current scoring |
| **P2 - HIGH** | Add state_history to master file save | 20 min | Preserves audit trail |
| **P3 - MEDIUM** | Verify Zone struct initialization | 1 hour | Prevents future issues |

## Next Steps

1. **Immediate**: Fix `save_zones_with_metadata()` to include `zone_score` and `state_history`
2. **Urgent**: Debug why new zones have zero scores in active file
3. **Testing**: Verify both files after fixes with actual trading run
4. **Validation**: Check that scores are non-zero and realistic
