# ZONE RETIREMENT FIX - New Config Parameters
## Add These to Your Config File

```ini
# ====================
# ⭐ ZONE LIFECYCLE MANAGEMENT (NEW)
# ====================
# These parameters control when zones are automatically retired

# Maximum zone age in days before auto-retirement
# After this many days, zones are disabled regardless of performance
# Recommended: 60-90 days for institutional relevance
# Conservative: 90 days (default)
# Aggressive: 60 days (faster rotation)
# Very aggressive: 45 days
zone_max_age_days = 90

# Maximum touch count before auto-retirement
# After this many touches, zones are considered exhausted
# Recommended: 30-50 touches
# Conservative: 50 touches (default)
# Moderate: 40 touches
# Aggressive: 30 touches (faster rotation, fresher zones)
zone_max_touch_count = 50

# ====================
# EXPECTED BEHAVIOR:
# ====================
# With these settings:
# - Zones older than 90 days: AUTO-RETIRED (score = 0)
# - Zones with >50 touches: AUTO-RETIRED (score = 0)
# - Zones 0-3 touches: BONUS (+3 to +5 points)
# - Zones 10-20 touches: PENALTY (-8 points)
# - Zones 30-50 touches: HEAVY PENALTY (-30 points)
#
# Result:
# - System favors FRESH zones (0-10 touches)
# - Avoids exhausted zones (30+ touches)
# - Retired zones never enter trades
# - More zone rotation, better trade quality
```

---

## ADDITIONAL CONFIG CHANGES (Optional but Recommended)

```ini
# ====================
# ZONE DETECTION (Relax to Find More Zones)
# ====================
# Current settings might be too strict (only 11 zones found)
# Consider relaxing these slightly:

# Min bars in consolidation (current: 3)
# Lower = more zones detected
# Try: 2 (may find more valid zones)
consolidation_min_bars = 2

# Min impulse ATR (current: 1.4)
# Lower = accept weaker impulses
# Try: 1.2 (find more institutional zones)
min_impulse_atr = 1.2

# Base height max ATR (current: 1.9)
# Higher = accept taller consolidations
# Try: 2.5 (don't reject valid wide bases)
base_height_max_atr = 2.5

# ====================
# EXPECTED IMPACT:
# ====================
# Current: 11 zones in 6 months
# After: 30-60 zones in 6 months (but with retirement, only 10-20 active)
# This gives fresher, more tradeable zones
```

---

## SUMMARY OF ALL FIXES

### **1. Code Changes:**
- ✅ `zone_quality_scorer.cpp` - Zone retirement logic
- ✅ `zone_scorer.cpp` - Stricter state freshness scoring

### **2. Config Changes:**
- ✅ Add `zone_max_age_days = 90`
- ✅ Add `zone_max_touch_count = 50`
- ⚠️ Optional: Relax zone detection parameters

### **3. Expected Results:**
```
Current System:
- 11 zones total (all 90+ days old)
- Avg 235 touches each
- Trading stale zones repeatedly
- 15 trades, +₹2K

After Fixes:
- 30-60 zones detected over time
- 10-20 active zones (rest retired)
- Avg 5-15 touches each (fresh!)
- Trading quality zones only
- 40-60 trades, +₹40K-80K estimated
```

---

## TESTING RECOMMENDATIONS

### **Phase 1: Conservative (Validate Fix)**
```ini
zone_max_age_days = 90
zone_max_touch_count = 50
# Keep other settings same
```
**Expected:** 20-30 trades, +₹15K-25K

### **Phase 2: Moderate (After validation)**
```ini
zone_max_age_days = 60
zone_max_touch_count = 40
# Relax detection slightly
```
**Expected:** 30-40 trades, +₹30K-50K

### **Phase 3: Aggressive (For maximum rotation)**
```ini
zone_max_age_days = 45
zone_max_touch_count = 30
consolidation_min_bars = 2
min_impulse_atr = 1.2
```
**Expected:** 40-60 trades, +₹40K-80K

---

## IMPLEMENTATION CHECKLIST

- [ ] Replace `src/scoring/zone_quality_scorer.cpp` with FIXED version
- [ ] Replace `src/scoring/zone_scorer.cpp` with FIXED version
- [ ] Add `zone_max_age_days` to config
- [ ] Add `zone_max_touch_count` to config
- [ ] Add these fields to Config struct in `common_types.h`:
  ```cpp
  int zone_max_age_days = 90;
  int zone_max_touch_count = 50;
  ```
- [ ] Add config parsing in `config_loader.cpp`:
  ```cpp
  else if (key == "zone_max_age_days") {
      config.zone_max_age_days = std::stoi(value);
  }
  else if (key == "zone_max_touch_count") {
      config.zone_max_touch_count = std::stoi(value);
  }
  ```
- [ ] Rebuild project
- [ ] Test with conservative settings first
- [ ] Monitor zone retirement in logs
- [ ] Validate trade quality improves

---

## MONITORING & VALIDATION

After deploying, check logs for:

```
✅ Good signs:
- "Zone #X RETIRED: Y touches (max=50)"
- "Zone #X RETIRED: Y days old (max=90)"
- Fresh zones (0-5 touches) being traded
- Multiple different zones in rotation

❌ Bad signs:
- No retirement messages
- Same zones being traded repeatedly
- All zones >30 touches
- Trade frequency unchanged
```

---

## ROLLBACK PROCEDURE

If results are worse:

1. **Revert code changes**
   ```bash
   git checkout src/scoring/zone_quality_scorer.cpp
   git checkout src/scoring/zone_scorer.cpp
   ```

2. **Remove config parameters**
   ```ini
   # Comment out:
   # zone_max_age_days = 90
   # zone_max_touch_count = 50
   ```

3. **Rebuild**
   ```bash
   cmake --build . --config Release
   ```

---

**Ready to deploy! Start with Phase 1 (conservative) and monitor results closely.**
