# Zone Score Persistence Fix Plan

## Issue Summary

### Current State
```
zones_live_master.json (Master):
╔════════════════════════════════════╗
║ ✓ metadata                         ║
║ ✓ zones array                      ║
║   ├─ zone_id                       ║
║   ├─ type, geometry, state         ║
║   ├─ target fields                 ║
║   ├─ structure fields              ║
║   └─ ✗ zone_score (MISSING)        ║  ← BUG: Not saved
║   └─ ✗ state_history (MISSING)     ║
╚════════════════════════════════════╝

zones_live_active.json (Active):
╔════════════════════════════════════╗
║ ✓ zones array                      ║
║   ├─ zone_id                       ║
║   ├─ type, geometry, state         ║
║   ├─ target fields                 ║
║   ├─ structure fields              ║
║   ├─ ✓ zone_score (PRESENT)        ║
║   │  ├─ base_strength_score: 0     ║  ← All ZEROS!
║   │  ├─ elite_bonus_score: 0       ║
║   │  ├─ total_score: 0             ║
║   │  └─ ... (all zeros)            ║
║   └─ ✓ state_history (PRESENT)     ║
║      └─ Empty array                ║
╚════════════════════════════════════╝
```

## Problem Breakdown

### Problem 1: Master File Missing Zone Scores
**Location**: `ZonePersistenceAdapter::save_zones_with_metadata()` (Line 683-760)
**Why**: The function incomplete - it doesn't include zone_score or state_history fields
**Impact**: Any restoration from master file loses all scoring history

### Problem 2: Active File Has Zero Scores  
**Possible Causes**:
1. **Timing Issue**: Zones saved before scoring engine runs
2. **Zone Creation Path**: New zones created but not scored before save
3. **Initialization Issue**: Zone struct zone_score not initialized properly

**Location to Check**: 
- When `process_zones()` saves zones (live_engine.cpp line 1094)
- What happens between zone creation and persistence

## Fix Implementation

### Fix 1: Complete `save_zones_with_metadata()` 

**File**: `src/ZonePersistenceAdapter.cpp` (Line 743)

**Current Code**:
```cpp
<< "      \"structure_rr_advantage\": " << zone.structure_rr_advantage << ",\n"
<< "      \"fixed_rr_at_entry\": " << zone.fixed_rr_at_entry << "\n";

if (i < zones.size() - 1) {
    ss << "    },\n";
} else {
    ss << "    }\n";
}
```

**Fixed Code**:
```cpp
<< "      \"structure_rr_advantage\": " << zone.structure_rr_advantage << ",\n"
<< "      \"fixed_rr_at_entry\": " << zone.fixed_rr_at_entry << ",\n"
<< "      \"zone_score\": {\n"
<< "        \"base_strength_score\": " << zone.zone_score.base_strength_score << ",\n"
<< "        \"elite_bonus_score\": " << zone.zone_score.elite_bonus_score << ",\n"
<< "        \"swing_position_score\": " << zone.zone_score.swing_position_score << ",\n"
<< "        \"regime_alignment_score\": " << zone.zone_score.regime_alignment_score << ",\n"
<< "        \"state_freshness_score\": " << zone.zone_score.state_freshness_score << ",\n"
<< "        \"rejection_confirmation_score\": " << zone.zone_score.rejection_confirmation_score << ",\n"
<< "        \"total_score\": " << zone.zone_score.total_score << ",\n"
<< "        \"entry_aggressiveness\": " << zone.zone_score.entry_aggressiveness << ",\n"
<< "        \"recommended_rr\": " << zone.zone_score.recommended_rr << ",\n"
<< "        \"score_breakdown\": \"" << zone.zone_score.score_breakdown << "\",\n"
<< "        \"entry_rationale\": \"" << zone.zone_score.entry_rationale << "\"\n"
<< "      },\n"
<< "      \"state_history\": [\n";

for (size_t j = 0; j < zone.state_history.size(); j++) {
    const ZoneStateEvent& event = zone.state_history[j];
    ss << "        {\n"
       << "          \"timestamp\": \"" << event.timestamp << "\",\n"
       << "          \"bar_index\": " << event.bar_index << ",\n"
       << "          \"event\": \"" << event.event << "\",\n"
       << "          \"old_state\": \"" << event.old_state << "\",\n"
       << "          \"new_state\": \"" << event.new_state << "\",\n"
       << "          \"price\": " << event.price << ",\n"
       << "          \"bar_high\": " << event.bar_high << ",\n"
       << "          \"bar_low\": " << event.bar_low << ",\n"
       << "          \"touch_number\": " << event.touch_number << "\n";
    
    if (j < zone.state_history.size() - 1) {
        ss << "        },\n";
    } else {
        ss << "        }\n";
    }
}

ss << "      ]\n";

if (i < zones.size() - 1) {
    ss << "    },\n";
} else {
    ss << "    }\n";
}
```

### Fix 2: Investigate Zero Scores Issue

**Next Step**: After fixing the master file, check why active file has zero scores:
1. Add debug logging around zone scoring in `process_zones()`
2. Verify that newly detected zones are scored BEFORE `save_zones()` is called
3. Check if zone.zone_score struct is being initialized with defaults properly

## Testing Strategy

After applying fixes:
1. Run live trading for 1 hour
2. Check both zone files:
   - Master file should now have `zone_score` data
   - Active file should have non-zero scores (if scoring engine is running)
3. Verify scores match between files for common zones
4. Check that score fields are populated with realistic values, not all zeros

## Files to Modify
- [ ] `src/ZonePersistenceAdapter.cpp` - Add missing zone_score and state_history to `save_zones_with_metadata()`
- [ ] `src/live/live_engine.cpp` - Add debug logging for zone scoring verification

## Success Criteria
- ✅ Master file contains zone_score and state_history fields
- ✅ Active file contains non-zero zone_score values (not defaults)
- ✅ Both files can be loaded and zones properly restored
- ✅ Score values are realistic for the zones present
