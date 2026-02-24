# ZONE OVERLAP CRISIS - ANALYSIS & SOLUTION
## SD Trading Platform V4.0

**Date:** February 9, 2026  
**Severity:** 🚨 **CRITICAL**  
**Files Analyzed:** zones_live_active.json, zones_live_master.json

---

## 🚨 **CRITICAL FINDINGS**

### **The Problem**

Your system has **MASSIVE zone overlap**:

```
SUPPLY ZONES:
  Total zones: 565
  Zones with overlaps: 564 (99.8%)
  Average overlaps per zone: 12.9
  Maximum overlaps: 26 zones at same price!

DEMAND ZONES:
  Total zones: 670
  Zones with overlaps: 669 (99.9%)
  Average overlaps per zone: 15.5
  Maximum overlaps: 27 zones at same price!
```

**Translation:** Nearly EVERY zone overlaps with 12-15 other zones!

---

## 📊 **EXAMPLES OF EXTREME OVERLAP**

### **Worst Case: Zone #1805 (SUPPLY)**

```
Zone #1805:
  Range: 24303.50 - 24335.00 (31.5 points)
  Formation: 2025-05-08 15:05:00
  Strength: 54.0
  State: FRESH
  
  OVERLAPS WITH 26 OTHER ZONES!
  
  Including:
  - Zone #1247: 24303.80 - 24320.00
  - Zone #653:  24310.00 - 24321.20
  - Zone #1203: 24313.00 - 24330.00
  - Zone #1246: 24328.00 - 24338.60
  - ... 22 more zones!
```

### **Worst Case: Zone #768 (DEMAND)**

```
Zone #768:
  Range: 24689.50 - 24698.00 (8.5 points)
  Formation: 2024-07-26 11:42:00
  Strength: 60.2
  State: TESTED
  
  OVERLAPS WITH 27 OTHER ZONES!
  
  Including:
  - Zone #725:  24685.10 - 24703.00
  - Zone #890:  24691.20 - 24703.10
  - Zone #891:  24700.00 - 24708.00
  - Zone #892:  24708.00 - 24714.20
  - ... 23 more zones!
```

---

## ⚠️ **WHY THIS IS A PROBLEM**

### **1. Redundant Signals**

```
Price touches 24700:
  ✗ System thinks: "We have a signal!"
  ✗ Reality: You have 27 overlapping zones all signaling!
  ✗ Result: Same trade opportunity counted 27 times
```

### **2. Confusing Entry Decisions**

```
Which zone do you trade?
  - Zone #768 (Strength: 60.2, Tested)
  - Zone #890 (Strength: 51.8, Tested)  
  - Zone #891 (Strength: 65.0, Tested)
  - Zone #892 (Strength: 71.2, Tested)
  - ... 23 more options!

How do you choose? They all say "trade now"!
```

### **3. Diluted Quality Assessment**

```
With 27 zones at same price:
  ✗ Can't identify THE strongest level
  ✗ Can't prioritize fresh vs tested zones
  ✗ Can't distinguish institutional from noise
  ✗ Scoring becomes meaningless
```

### **4. Performance Impact**

```
1,235 total zones × average 14 overlaps = 17,290 comparisons!
  ✗ Slow zone evaluation
  ✗ Memory waste
  ✗ Computation overhead
```

---

## 🔍 **ROOT CAUSE ANALYSIS**

### **Why Are There So Many Overlaps?**

**Theory 1: Zone Detection Running Too Frequently**
```cpp
// Current: Detecting zones on EVERY bar?
for (int i = 0; i < bars.size(); i++) {
    detect_zones_at_bar(i);  // Creates new zone every bar!
}

// Result: Hundreds of slightly different zones at same price
```

**Theory 2: No Consolidation Merge Logic**
```cpp
// Missing: Zone merger
// When two zones overlap, should merge them into ONE zone
// Currently: Both zones kept separately
```

**Theory 3: Too Many Historical Zones**
```cpp
// Zones from May 2024 to November 2025 (18 months!)
// Old zones not being removed/expired
// Result: Accumulation of stale zones
```

**Theory 4: Detection Parameters Too Loose**
```cpp
// Current parameters might be:
max_consolidation_range = 0.5 ATR  // Too loose?
min_consolidation_bars = 3         // Too few?

// Result: Everything qualifies as a zone
```

---

## 💡 **SOLUTION: 4-STAGE CLEANUP**

### **STAGE 1: Immediate - Zone Deduplication (1 day)**

**Objective:** Merge overlapping zones into single representative zones

```cpp
// File: src/zones/zone_deduplicator.h

#ifndef ZONE_DEDUPLICATOR_H
#define ZONE_DEDUPLICATOR_H

#include "common_types.h"
#include <vector>
#include <map>

namespace SDTrading {
namespace Zones {

/**
 * @struct DeduplicationConfig
 * @brief Configuration for zone deduplication
 */
struct DeduplicationConfig {
    double overlap_threshold_pct;    // Minimum overlap to merge (default: 70%)
    double proximity_threshold_atr;  // Max distance to consider same zone (default: 0.3)
    
    enum class MergeStrategy {
        KEEP_STRONGEST,     // Keep zone with highest strength
        KEEP_FRESHEST,      // Keep zone with fewest touches
        KEEP_OLDEST,        // Keep first detected zone
        MERGE_PROPERTIES    // Create composite zone
    };
    MergeStrategy strategy;
    
    DeduplicationConfig()
        : overlap_threshold_pct(70.0),
          proximity_threshold_atr(0.3),
          strategy(MergeStrategy::KEEP_STRONGEST) {}
};

/**
 * @class ZoneDeduplicator
 * @brief Identifies and merges overlapping zones
 */
class ZoneDeduplicator {
private:
    DeduplicationConfig config_;
    
    /**
     * Calculate overlap percentage between two zones
     */
    double calculate_overlap_percentage(
        const Core::Zone& zone1,
        const Core::Zone& zone2
    ) const;
    
    /**
     * Check if zones should be merged
     */
    bool should_merge(
        const Core::Zone& zone1,
        const Core::Zone& zone2,
        double atr
    ) const;
    
    /**
     * Merge two zones into one
     */
    Core::Zone merge_zones(
        const Core::Zone& zone1,
        const Core::Zone& zone2
    ) const;

public:
    /**
     * Constructor
     */
    explicit ZoneDeduplicator(
        const DeduplicationConfig& config = DeduplicationConfig()
    ) : config_(config) {}
    
    /**
     * Deduplicate zones
     * @param zones Input zones (may have overlaps)
     * @param atr Current ATR (for proximity check)
     * @return Deduplicated zones (no overlaps)
     */
    std::vector<Core::Zone> deduplicate(
        const std::vector<Core::Zone>& zones,
        double atr
    ) const;
    
    /**
     * Get deduplication statistics
     */
    struct DeduplicationStats {
        int original_count;
        int final_count;
        int merged_count;
        int kept_count;
    };
    
    DeduplicationStats get_last_stats() const { return last_stats_; }

private:
    mutable DeduplicationStats last_stats_;
};

} // namespace Zones
} // namespace SDTrading

#endif
```

**Implementation:**

```cpp
// File: src/zones/zone_deduplicator.cpp

#include "zone_deduplicator.h"
#include "../utils/logger.h"
#include <algorithm>
#include <cmath>

namespace SDTrading {
namespace Zones {

double ZoneDeduplicator::calculate_overlap_percentage(
    const Core::Zone& zone1,
    const Core::Zone& zone2
) const {
    // Only check same type zones
    if (zone1.type != zone2.type) return 0.0;
    
    // Calculate overlap range
    double overlap_start = std::max(zone1.distal_line, zone2.distal_line);
    double overlap_end = std::min(zone1.proximal_line, zone2.proximal_line);
    
    if (overlap_start >= overlap_end) return 0.0;  // No overlap
    
    double overlap_range = overlap_end - overlap_start;
    
    // Calculate as percentage of smaller zone
    double zone1_range = zone1.proximal_line - zone1.distal_line;
    double zone2_range = zone2.proximal_line - zone2.distal_line;
    double min_range = std::min(zone1_range, zone2_range);
    
    if (min_range < 0.0001) return 0.0;
    
    return (overlap_range / min_range) * 100.0;
}

bool ZoneDeduplicator::should_merge(
    const Core::Zone& zone1,
    const Core::Zone& zone2,
    double atr
) const {
    // Must be same type
    if (zone1.type != zone2.type) return false;
    
    // Check overlap percentage
    double overlap_pct = calculate_overlap_percentage(zone1, zone2);
    if (overlap_pct >= config_.overlap_threshold_pct) {
        return true;
    }
    
    // Check proximity (even if not overlapping)
    double distance = std::min(
        std::abs(zone1.proximal_line - zone2.proximal_line),
        std::abs(zone1.distal_line - zone2.distal_line)
    );
    
    if (distance <= config_.proximity_threshold_atr * atr) {
        return true;
    }
    
    return false;
}

Core::Zone ZoneDeduplicator::merge_zones(
    const Core::Zone& zone1,
    const Core::Zone& zone2
) const {
    Core::Zone merged;
    
    // Determine which zone to keep based on strategy
    const Core::Zone* primary = nullptr;
    const Core::Zone* secondary = nullptr;
    
    switch (config_.strategy) {
        case DeduplicationConfig::MergeStrategy::KEEP_STRONGEST:
            primary = (zone1.strength >= zone2.strength) ? &zone1 : &zone2;
            secondary = (zone1.strength >= zone2.strength) ? &zone2 : &zone1;
            break;
            
        case DeduplicationConfig::MergeStrategy::KEEP_FRESHEST:
            primary = (zone1.touch_count <= zone2.touch_count) ? &zone1 : &zone2;
            secondary = (zone1.touch_count <= zone2.touch_count) ? &zone2 : &zone1;
            break;
            
        case DeduplicationConfig::MergeStrategy::KEEP_OLDEST:
            primary = (zone1.formation_bar <= zone2.formation_bar) ? &zone1 : &zone2;
            secondary = (zone1.formation_bar <= zone2.formation_bar) ? &zone2 : &zone1;
            break;
            
        case DeduplicationConfig::MergeStrategy::MERGE_PROPERTIES:
            // Create composite zone
            merged = zone1;  // Start with zone1
            
            // Expand boundaries to encompass both zones
            merged.distal_line = std::min(zone1.distal_line, zone2.distal_line);
            merged.proximal_line = std::max(zone1.proximal_line, zone2.proximal_line);
            merged.base_low = std::min(zone1.base_low, zone2.base_low);
            merged.base_high = std::max(zone1.base_high, zone2.base_high);
            
            // Average strength
            merged.strength = (zone1.strength + zone2.strength) / 2.0;
            
            // Sum touch counts
            merged.touch_count = zone1.touch_count + zone2.touch_count;
            
            // Keep oldest formation time
            if (zone2.formation_bar < zone1.formation_bar) {
                merged.formation_bar = zone2.formation_bar;
                merged.formation_datetime = zone2.formation_datetime;
            }
            
            // Combine elite status
            merged.is_elite = zone1.is_elite || zone2.is_elite;
            
            return merged;
    }
    
    // For non-merge strategies, keep primary zone
    merged = *primary;
    
    // Update touch count to include secondary's touches
    merged.touch_count += secondary->touch_count;
    
    return merged;
}

std::vector<Core::Zone> ZoneDeduplicator::deduplicate(
    const std::vector<Core::Zone>& zones,
    double atr
) const {
    if (zones.empty()) return {};
    
    last_stats_.original_count = zones.size();
    
    // Separate by type
    std::vector<Core::Zone> supply_zones;
    std::vector<Core::Zone> demand_zones;
    
    for (const auto& zone : zones) {
        if (zone.type == Core::ZoneType::SUPPLY) {
            supply_zones.push_back(zone);
        } else {
            demand_zones.push_back(zone);
        }
    }
    
    // Deduplicate each type separately
    auto deduplicated_supply = deduplicate_by_type(supply_zones, atr);
    auto deduplicated_demand = deduplicate_by_type(demand_zones, atr);
    
    // Combine results
    std::vector<Core::Zone> result;
    result.insert(result.end(), deduplicated_supply.begin(), deduplicated_supply.end());
    result.insert(result.end(), deduplicated_demand.begin(), deduplicated_demand.end());
    
    last_stats_.final_count = result.size();
    last_stats_.merged_count = last_stats_.original_count - last_stats_.final_count;
    last_stats_.kept_count = last_stats_.final_count;
    
    LOG_INFO("Zone deduplication: " + std::to_string(last_stats_.original_count) + 
            " → " + std::to_string(last_stats_.final_count) +
            " (" + std::to_string(last_stats_.merged_count) + " merged)");
    
    return result;
}

std::vector<Core::Zone> ZoneDeduplicator::deduplicate_by_type(
    const std::vector<Core::Zone>& zones,
    double atr
) const {
    if (zones.empty()) return {};
    
    // Sort by proximal line for efficient overlap detection
    std::vector<Core::Zone> sorted_zones = zones;
    std::sort(sorted_zones.begin(), sorted_zones.end(),
        [](const Core::Zone& a, const Core::Zone& b) {
            return a.proximal_line < b.proximal_line;
        });
    
    std::vector<Core::Zone> result;
    std::vector<bool> merged(sorted_zones.size(), false);
    
    for (size_t i = 0; i < sorted_zones.size(); i++) {
        if (merged[i]) continue;
        
        Core::Zone current = sorted_zones[i];
        
        // Find all zones that should be merged with this one
        for (size_t j = i + 1; j < sorted_zones.size(); j++) {
            if (merged[j]) continue;
            
            if (should_merge(current, sorted_zones[j], atr)) {
                current = merge_zones(current, sorted_zones[j]);
                merged[j] = true;
            }
        }
        
        result.push_back(current);
        merged[i] = true;
    }
    
    return result;
}

} // namespace Zones
} // namespace SDTrading
```

**Usage:**

```cpp
// In your zone loading code
std::vector<Zone> zones = load_zones("zones_live_active.json");

// Deduplicate
Zones::DeduplicationConfig config;
config.overlap_threshold_pct = 70.0;  // 70% overlap = merge
config.proximity_threshold_atr = 0.3; // Within 0.3 ATR = merge
config.strategy = Zones::DeduplicationConfig::MergeStrategy::KEEP_STRONGEST;

Zones::ZoneDeduplicator deduplicator(config);
double atr = 50.0;  // Current ATR

auto deduplicated = deduplicator.deduplicate(zones, atr);

auto stats = deduplicator.get_last_stats();
std::cout << "Zones: " << stats.original_count 
          << " → " << stats.final_count 
          << " (" << stats.merged_count << " merged)" << std::endl;
```

**Expected Result:**
```
Before: 1,235 zones (99% overlapping, avg 14 overlaps each)
After:  ~150-200 zones (minimal overlap, unique price levels)

Reduction: 83-87% fewer zones!
```

---

### **STAGE 2: Short-term - Zone Expiry (3 days)**

**Objective:** Remove stale zones older than threshold

```cpp
// File: src/zones/zone_expiry_manager.h

struct ExpiryConfig {
    int max_age_days;               // Maximum days to keep zone (default: 30)
    int max_age_bars;               // Maximum bars to keep zone (default: 500)
    double max_distance_atr;        // Maximum distance from price (default: 10.0)
    bool expire_violated_zones;     // Remove violated zones (default: true)
    bool expire_weak_zones;         // Remove zones with strength < threshold
    double min_strength_threshold;  // Minimum strength to keep (default: 50.0)
};

class ZoneExpiryManager {
public:
    /**
     * Remove expired zones
     * @param zones Input zones
     * @param current_datetime Current date/time
     * @param current_price Current market price
     * @param atr Current ATR
     * @return Active zones (non-expired)
     */
    std::vector<Core::Zone> remove_expired(
        const std::vector<Core::Zone>& zones,
        const std::string& current_datetime,
        double current_price,
        double atr
    ) const;
};
```

**Usage:**

```cpp
ExpiryConfig config;
config.max_age_days = 30;           // Keep zones < 30 days old
config.max_distance_atr = 10.0;     // Keep zones within 10 ATR
config.expire_violated_zones = true; // Remove broken zones
config.min_strength_threshold = 50.0; // Keep zones with strength >= 50

ZoneExpiryManager expiry(config);
auto active_zones = expiry.remove_expired(all_zones, "2026-02-09", 25000, 50);
```

---

### **STAGE 3: Medium-term - Prevent Detection (1 week)**

**Objective:** Fix zone detection to avoid creating duplicates

```cpp
// File: src/zones/zone_detector.cpp

std::vector<Zone> ZoneDetector::detect_zones_incremental(int start_bar) {
    std::vector<Zone> new_zones;
    
    // Only detect zones at significant intervals
    int detection_interval = config.zone_detection_interval;  // e.g., 20 bars
    
    for (int i = start_bar; i < bars.size(); i += detection_interval) {
        // Check if zone already exists at this price
        if (zone_already_exists(i)) {
            continue;  // Skip detection
        }
        
        // Detect zone
        auto zone = detect_zone_at_bar(i);
        if (zone.is_valid) {
            new_zones.push_back(zone);
        }
    }
    
    return new_zones;
}

bool ZoneDetector::zone_already_exists(int bar_index) {
    double price = bars[bar_index].close;
    double tolerance = config.zone_existence_tolerance_atr * current_atr;
    
    for (const auto& existing_zone : existing_zones) {
        if (std::abs(existing_zone.proximal_line - price) < tolerance) {
            return true;  // Zone exists nearby
        }
    }
    
    return false;
}
```

---

### **STAGE 4: Long-term - Multi-Timeframe Consolidation (Part of V5.0)**

**Objective:** Use MTF to identify truly institutional zones

```cpp
// With MTF, zones are validated across timeframes
MTF::MultiTimeframeManager mtf_manager;

// Only keep zones that align across timeframes
for (auto& zone : zones_1h) {
    bool has_htf_parent = (mtf_manager.find_parent_zone(zone, Timeframe::H4) != nullptr);
    bool htf_aligned = mtf_manager.is_htf_trend_aligned(zone, Timeframe::D1);
    
    if (!has_htf_parent && !htf_aligned) {
        // This is a weak 1H zone with no HTF support
        // Remove it or score it very low
        zone.quality_score *= 0.3;  // Heavy penalty
    }
}
```

---

## 📋 **IMMEDIATE ACTION PLAN**

### **TODAY (2 hours)**

1. **Run Deduplication Script**
```bash
# Quick Python script to deduplicate zones
python3 deduplicate_zones.py zones_live_active.json --output zones_deduplicated.json
```

2. **Validate Results**
```bash
# Compare before/after
echo "Before: $(jq '.zones | length' zones_live_active.json) zones"
echo "After: $(jq '.zones | length' zones_deduplicated.json) zones"
```

3. **Update Live System**
```bash
# Backup old zones
cp zones_live_active.json zones_live_active_backup.json

# Replace with deduplicated
cp zones_deduplicated.json zones_live_active.json
```

### **THIS WEEK (1 day)**

1. **Implement ZoneDeduplicator class** (C++)
   - Copy code from above
   - Add to project
   - Write unit tests

2. **Implement ZoneExpiryManager** (C++)
   - Remove zones > 30 days old
   - Remove violated zones
   - Remove zones too far from price

3. **Update Zone Loading**
```cpp
// In zone loader
auto zones = load_zones_from_file();
zones = deduplicator.deduplicate(zones, atr);
zones = expiry_manager.remove_expired(zones, current_datetime, current_price, atr);
```

### **NEXT WEEK (2-3 days)**

1. **Fix Zone Detection**
   - Add detection interval (every 20 bars, not every bar)
   - Add zone existence check before creating new zone
   - Merge logic during detection

2. **Add Configuration**
```json
{
  "zone_deduplication": {
    "enabled": true,
    "overlap_threshold_pct": 70.0,
    "proximity_threshold_atr": 0.3,
    "merge_strategy": "keep_strongest"
  },
  
  "zone_expiry": {
    "enabled": true,
    "max_age_days": 30,
    "max_distance_atr": 10.0,
    "expire_violated": true,
    "min_strength": 50.0
  },
  
  "zone_detection": {
    "detection_interval_bars": 20,
    "check_existing_before_create": true,
    "auto_merge_on_detection": true
  }
}
```

---

## 📊 **EXPECTED OUTCOMES**

### **Before Deduplication:**
```
Total Zones: 1,235
  Supply: 565 (99.8% overlapping, avg 12.9 overlaps)
  Demand: 670 (99.9% overlapping, avg 15.5 overlaps)

Problems:
  ✗ Redundant signals
  ✗ Confusing entry decisions
  ✗ Slow performance
  ✗ Diluted quality
```

### **After Immediate Fix (Stage 1):**
```
Total Zones: ~150-200 (83-87% reduction)
  Supply: ~75-100
  Demand: ~75-100
  
Improvements:
  ✓ ONE zone per price level
  ✓ Clear entry decisions
  ✓ Fast performance
  ✓ Meaningful quality scores
```

### **After Full Implementation (Stage 1-4):**
```
Total Zones: ~50-75 (93-96% reduction)
  Supply: ~25-40
  Demand: ~25-40
  
Improvements:
  ✓ Only recent zones (< 30 days)
  ✓ Only relevant zones (within 10 ATR)
  ✓ Only strong zones (strength >= 50)
  ✓ No duplicates
  ✓ MTF validated (when V5.0 ready)
```

---

## 🎯 **CONFIGURATION RECOMMENDATIONS**

### **Deduplication Parameters**

```json
{
  "overlap_threshold_pct": 70.0,     // 70%+ overlap = merge
  "proximity_threshold_atr": 0.3,    // Within 0.3 ATR = merge
  "merge_strategy": "keep_strongest" // Keep highest strength zone
}
```

**Why these values:**
- 70% overlap: Zones are essentially the same level
- 0.3 ATR: Within noise range (NIFTY ATR ~50, so 15 points)
- Keep strongest: Highest quality zone is most reliable

### **Expiry Parameters**

```json
{
  "max_age_days": 30,           // Keep zones < 1 month
  "max_distance_atr": 10.0,     // Keep zones within 10 ATR (500 pts)
  "expire_violated": true,      // Remove broken zones
  "min_strength": 50.0          // Keep zones with strength >= 50
}
```

**Why these values:**
- 30 days: Market structure changes monthly
- 10 ATR: Beyond this, zone is irrelevant (NIFTY ~500 points)
- Violated zones: No longer valid support/resistance
- Strength 50: Below this is weak consolidation

### **Detection Parameters**

```json
{
  "detection_interval_bars": 20,           // Check every 20 bars (not every bar!)
  "check_existing_before_create": true,    // Prevent duplicates
  "auto_merge_on_detection": true,         // Merge immediately
  "zone_existence_tolerance_atr": 0.5      // Within 0.5 ATR = same zone
}
```

**Why these values:**
- 20 bars: Balance between freshness and duplication
- Check existing: Prevent creating zone if one exists
- Auto merge: Combine similar zones immediately
- 0.5 ATR tolerance: Reasonable proximity check

---

## 🚨 **CRITICAL: DO THIS BEFORE V5.0**

**Priority: P0 (BLOCKING)**

You MUST fix this overlap problem BEFORE implementing:
- Multi-Timeframe (will multiply the overlaps!)
- Structure-Based Targeting (needs clear zone levels)
- Pattern Classification (confused by overlaps)

**Timeline:**
- TODAY: Run deduplication script on JSON files
- THIS WEEK: Implement ZoneDeduplicator + ZoneExpiryManager
- NEXT WEEK: Fix zone detection to prevent future duplicates

**After this fix, THEN proceed with V5.0 implementation.**

---

## 📝 **PYTHON QUICK FIX SCRIPT**

For immediate relief, here's a Python script:

```python
#!/usr/bin/env python3
import json
import sys

def deduplicate_zones(zones, overlap_threshold=0.7, proximity_atr=15.0):
    """Deduplicate overlapping zones"""
    
    supply = [z for z in zones if z['type'] == 'SUPPLY']
    demand = [z for z in zones if z['type'] == 'DEMAND']
    
    supply_dedup = deduplicate_by_type(supply, overlap_threshold, proximity_atr)
    demand_dedup = deduplicate_by_type(demand, overlap_threshold, proximity_atr)
    
    return supply_dedup + demand_dedup

def deduplicate_by_type(zones, overlap_threshold, proximity_atr):
    if not zones:
        return []
    
    # Sort by proximal line
    zones.sort(key=lambda z: z['proximal_line'])
    
    result = []
    merged = [False] * len(zones)
    
    for i in range(len(zones)):
        if merged[i]:
            continue
        
        current = zones[i].copy()
        
        for j in range(i + 1, len(zones)):
            if merged[j]:
                continue
            
            if should_merge(current, zones[j], overlap_threshold, proximity_atr):
                current = merge_zones(current, zones[j])
                merged[j] = True
        
        result.append(current)
        merged[i] = True
    
    return result

def should_merge(z1, z2, overlap_threshold, proximity_atr):
    # Calculate overlap
    overlap_start = max(z1['distal_line'], z2['distal_line'])
    overlap_end = min(z1['proximal_line'], z2['proximal_line'])
    
    if overlap_start >= overlap_end:
        # Check proximity
        distance = min(
            abs(z1['proximal_line'] - z2['proximal_line']),
            abs(z1['distal_line'] - z2['distal_line'])
        )
        return distance <= proximity_atr
    
    overlap_range = overlap_end - overlap_start
    min_range = min(
        z1['proximal_line'] - z1['distal_line'],
        z2['proximal_line'] - z2['distal_line']
    )
    
    overlap_pct = overlap_range / min_range if min_range > 0 else 0
    return overlap_pct >= overlap_threshold

def merge_zones(z1, z2):
    # Keep zone with higher strength
    if z1['strength'] >= z2['strength']:
        primary, secondary = z1, z2
    else:
        primary, secondary = z2, z1
    
    merged = primary.copy()
    merged['touch_count'] = z1['touch_count'] + z2['touch_count']
    merged['is_elite'] = z1['is_elite'] or z2['is_elite']
    
    return merged

if __name__ == '__main__':
    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else 'zones_deduplicated.json'
    
    with open(input_file, 'r') as f:
        data = json.load(f)
    
    print(f"Original zones: {len(data['zones'])}")
    
    deduplicated = deduplicate_zones(data['zones'])
    
    print(f"Deduplicated zones: {len(deduplicated)}")
    print(f"Removed: {len(data['zones']) - len(deduplicated)} ({(1 - len(deduplicated)/len(data['zones']))*100:.1f}%)")
    
    data['zones'] = deduplicated
    
    with open(output_file, 'w') as f:
        json.dump(data, f, indent=2)
    
    print(f"Saved to: {output_file}")
```

**Run it:**
```bash
python3 deduplicate_zones.py zones_live_active.json zones_clean.json
```

---

## ✅ **CONCLUSION**

**Current Situation:** 🚨 CRITICAL  
- 99% of zones overlap
- Average 14 overlaps per zone  
- Maximum 27 zones at same price!

**Root Cause:**  
- Detection runs too frequently
- No merge logic
- No expiry logic
- Too many historical zones

**Solution:**  
- Stage 1 (TODAY): Deduplicate existing zones
- Stage 2 (THIS WEEK): Add expiry logic
- Stage 3 (NEXT WEEK): Fix detection
- Stage 4 (V5.0): MTF validation

**Expected Result:**  
- 1,235 zones → 50-75 zones (96% reduction)
- Clear entry decisions
- Fast performance
- Meaningful quality scores

**This fix is MANDATORY before V5.0!** 🎯
