# Supply & Demand Trading System Skill

Comprehensive Claude skill for developing, debugging, and optimizing supply & demand zone-based algorithmic trading systems.

## What This Skill Covers

- **Supply & Demand Methodology:** Sam Seiden institutional order flow concepts
- **Zone Detection:** Rally-Base-Rally (RBR) / Drop-Base-Drop (DBD) patterns
- **Zone Scoring:** Multi-factor analysis (base, volume, OI, state, rejection)
- **Entry/Exit Logic:** Systematic decision frameworks
- **Volume/OI Analysis:** Institutional footprint detection
- **Multi-Timeframe Analysis:** HTF alignment and confluence
- **Backtesting:** Performance metrics and optimization
- **C++ Trading Engines:** Implementation patterns and debugging
- **Bug Diagnosis:** Common issues and solutions
- **Live Trading:** Broker integration (Fyers API, Spring Boot microservices)

## Skill Structure

```
supply-demand-trading/
├── SKILL.md                    # Main skill (comprehensive guide)
├── README.md                   # This file
├── references/
│   ├── version_history.md     # V3.0 → V6.0 evolution
│   ├── bug_tracker.md         # Complete bug database
│   └── [additional references]
├── scripts/
│   └── health_check.py        # Diagnostic tool
└── examples/
    └── [code examples]
```

## Quick Start

### For Claude

When user mentions:
- Supply/demand zones, S&D methodology
- Zone detection bugs or scoring issues
- Backtest analysis or performance problems
- Volume/OI profiling, institutional order flow
- C++ trading engine development
- Multi-timeframe analysis
- AmiBroker AFL integration
- Fyers broker API integration

**→ Read `/mnt/skills/supply-demand-trading/SKILL.md`**

### For Users

**Common Use Cases:**

1. **"My backtest shows -13% return, help me fix it"**
   - Claude will read SKILL.md
   - Use bug_tracker.md to identify likely issues
   - Provide specific diagnostic steps
   - Suggest targeted fixes with code

2. **"Zone detection is too strict, only 4 zones"**
   - Claude refers to detection parameters section
   - Provides data-driven threshold recommendations
   - Shows before/after examples

3. **"Winners have 2.47× volume, losers 1.31×, but filter is disabled"**
   - Claude identifies Bug #172 from bug_tracker.md
   - Explains root cause and impact
   - Provides 2-minute fix with ROI calculation

4. **"How do I implement multi-timeframe support?"**
   - Claude uses MTF section in SKILL.md
   - Provides bar aggregation code
   - Shows confluence scoring implementation
   - Estimates effort and impact

## Key Sections in SKILL.md

### 1. Core Methodology (Lines 1-150)
- Zone fundamentals
- State machine logic
- Institutional signatures
- Volume/OI analysis

### 2. System Architecture (Lines 151-400)
- Zone detection algorithm
- Scoring system (multi-factor)
- Entry decision filters
- Risk management

### 3. Common Issues & Solutions (Lines 401-600)
- Too many zones
- Profiles not updating
- Filters too strict
- Backtest-live gap
- Zone exhaustion

### 4. Multi-Timeframe Framework (Lines 601-700)
- Bar aggregation
- MTF zone detection
- Confluence scoring
- Implementation examples

### 5. Performance Optimization (Lines 701-800)
- Backtesting metrics
- 80/20 rule for trading
- Data-driven optimization
- Parameter tuning

### 6. Integration Guides (Lines 801-900)
- Fyers broker API
- Spring Boot microservices
- AmiBroker AFL patterns
- Order submission

### 7. Debugging Decision Trees (Lines 901-1000)
- Systematic diagnostics
- Issue classification
- Root cause analysis
- Fix verification

## Reference Files

### version_history.md
Complete evolution from V3.0 (monolithic) through V6.0 (volume/OI integration):
- Architectural changes
- Bug discoveries and fixes
- Performance progression
- Lessons learned

**Key sections:**
- V3.0: Baseline (simple, working)
- V4.0: Modernization (complexity issues)
- V5.0: Optimization (parameter tuning)
- V6.0: Volume/OI (current, with bugs)

### bug_tracker.md
Comprehensive bug database with:
- Active critical bugs (P0)
- Resolved bugs
- Feature gaps
- Historical bugs
- Reporting guidelines

**Priority bugs:**
- Bug #172: Pullback volume filter disabled (HIGHEST)
- Bug #VOL_EXIT: Volume exhaustion on entry bar (CRITICAL)
- Bug #143-155: Category 1 fixes (RESOLVED)
- Gaps #173-186: Missing features

## Diagnostic Tools

### health_check.py

Quick system diagnostic:
```bash
python scripts/health_check.py zones_live_master.json phase_6_config_v0_6_NEW.txt
```

**Checks:**
- Zone count (too many/few?)
- Volume score distribution vs thresholds
- Institutional index vs thresholds
- Departure ratios vs thresholds
- Touch count (exhausted zones?)
- Config issues (disabled filters, strict params)

**Output:**
- Data analysis with min/max/avg
- Threshold recommendations
- Critical issues flagged
- Priority action items

## Best Practices

**When using this skill:**

1. **Start with SKILL.md overview**
   - Understand methodology first
   - Know the state machine
   - Grasp zone scoring logic

2. **Refer to specific sections**
   - Don't read entire 1000-line file
   - Jump to relevant section
   - Use quick reference card (end of SKILL.md)

3. **Check bug_tracker.md for known issues**
   - Don't reinvent solutions
   - Learn from past bugs
   - Apply proven fixes

4. **Use version_history.md for context**
   - Understand why things changed
   - Learn from mistakes
   - See evolution patterns

5. **Run health_check.py for diagnostics**
   - Objective data analysis
   - Quick problem identification
   - Data-driven thresholds

## Example Workflows

### Workflow 1: Debugging Zero Trades

```
User: "My system generates 0 trades"

Claude process:
1. Read SKILL.md Common Issues section
2. Request zones_live_master.json
3. Run mental health_check analysis
4. Identify: Entry filters too strict
5. Check bug_tracker: Bug #172 (volume filter disabled)
6. Provide fix: min_volume_entry_score = 10
7. Explain impact: +10-15% win rate
```

### Workflow 2: Implementing New Feature

```
User: "Add approach velocity filter"

Claude process:
1. Read SKILL.md entry decision section
2. Check bug_tracker: Gap #173 (not implemented)
3. Provide implementation code from SKILL.md
4. Show config parameters needed
5. Estimate effort: 4 hours
6. Estimate impact: +5-8% win rate
7. Provide verification steps
```

### Workflow 3: Performance Analysis

```
User: "Backtest shows -13% return, 58 trades"

Claude process:
1. Read SKILL.md debugging decision tree
2. Request exit reason breakdown
3. Identify: 48/58 vol_exhaustion exits at Bars=0
4. Check bug_tracker: Bug #VOL_EXIT
5. Explain: Exit triggers on entry bar
6. Provide quick fix: Disable in config
7. Provide proper fix: Add bars_in_trade check
8. Estimate recovery: -13% → +25%
```

## Maintenance

**Keeping the skill current:**

- Update bug_tracker.md when bugs are discovered/fixed
- Add to version_history.md when versions change
- Expand SKILL.md with new patterns as discovered
- Add scripts/ utilities as needed
- Update examples/ with working implementations

**Version control:**

This skill evolves with the SD Trading System:
- Current system: V6.0
- Skill coverage: V3.0 through V6.0
- Future: Will track V6.5, V7.0 as developed

## Credits

**Methodology:**
- Sam Seiden (Supply & Demand concepts)
- Mark Douglas (Trading psychology)
- Adam Grimes (Technical analysis)

**System Development:**
- Shan (25+ years trading experience, system architect)
- Claude (Implementation assistance, debugging, optimization)

**Technology Stack:**
- C++ (Trading engine core)
- Java Spring Boot (Microservices, Fyers API)
- AmiBroker AFL (Zone detection)
- Python (Analysis, utilities)

## Contact & Feedback

This skill is maintained as part of an ongoing trading system development project. Feedback and improvements welcome.

**Common issues? Check:**
1. bug_tracker.md (known bugs)
2. SKILL.md Common Issues section
3. health_check.py output
4. version_history.md (similar past issues)

---

*Comprehensive skill for supply & demand algorithmic trading system development*  
*Version: 1.0*  
*Last Updated: March 6, 2026*
