# SD Trading Engine V6 — Parameter Optimizer
## Complete Integration Guide

---

## Files in This Package

| File | Purpose |
|------|---------|
| `optimizer.py` | Main optimizer engine — all strategies + result tracking |
| `result_parser_adapter.py` | Customize to parse your engine's output format |
| `dashboard.html` | Visual dashboard — open in browser to monitor/analyse |
| `run_optimizer.sh` | Linux/macOS launcher |
| `run_optimizer.bat` | Windows launcher |

---

## Quick Start (3 steps)

### Step 1 — Test framework (no backtest needed)
```bash
python optimizer.py \
  --config phase_6_config_v0_7_NEW.txt \
  --backtest-cmd "echo test" \
  --strategy random \
  --trials 20 \
  --dry-run
```

### Step 2 — Configure your backtest command
Edit `run_optimizer.sh` (or `.bat`):
```bash
BACKTEST_CMD="./backtest {config}"   # ← replace with your actual command
```
The `{config}` placeholder is replaced with the path to each trial's config file.

### Step 3 — Run
```bash
chmod +x run_optimizer.sh && ./run_optimizer.sh
```

---

## Optimization Strategies

| Strategy | When to Use | Trials Recommended |
|----------|-------------|-------------------|
| **random** | Quick exploration, large spaces | 50–200 |
| **grid** | Exhaustive, small parameter sets | Auto (caps at --trials) |
| **genetic** | Best for trading systems, complex interactions | 80–200 |
| **bayesian** | Fewest backtests needed, expensive runs | 30–80 |

**Recommended for SD Engine V6:** `genetic` with `--trials 100`

---

## Objective Functions

| Objective | Optimizes For | Best When |
|-----------|--------------|-----------|
| `composite` | Balanced P&L + consistency + risk | Default — use this |
| `net_pnl` | Raw rupee profit | High-frequency, many trades |
| `sharpe` | Risk-adjusted returns | Capital preservation focus |
| `profit_factor` | Gross profit / gross loss | Win/loss ratio focus |
| `risk_adjusted` | Return on max drawdown × win rate | Conservative accounts |

---

## Parameter Groups

Optimize specific groups to reduce search space:

```bash
# Only risk + trailing parameters (fastest)
python optimizer.py ... --params-groups risk trailing

# Entry quality tuning
python optimizer.py ... --params-groups entry scoring

# Zone detection tuning
python optimizer.py ... --params-groups zone

# Full optimization (all 40 parameters)
python optimizer.py ... --params-groups all
```

---

## Customizing the Result Parser

**CRITICAL STEP:** Edit `result_parser_adapter.py` to match your engine's output.

Your engine's output likely looks like:
```
Total Trades    : 87
Win Rate        : 54.02%
Net P&L         : ₹1,23,456
Profit Factor   : 1.834
Max Drawdown    : 12.4%
Sharpe Ratio    : 1.23
```

Test the parser:
```bash
python result_parser_adapter.py backtest_output.txt
```

Then integrate it into `optimizer.py` by replacing the `ResultParser` class.

---

## Output Directory Structure

```
optimization_runs/
├── all_results.csv          ← All trials with metrics
├── optimizer.log            ← Full run log
├── BEST/
│   ├── best_config.txt      ← ⭐ BEST configuration file
│   ├── best_report.txt      ← Human-readable best result report
│   └── best_summary.json    ← Machine-readable summary
└── runs/
    ├── run_0001_143022/
    │   ├── config.txt       ← Exact config used for this trial
    │   └── backtest_output.txt
    ├── run_0002_143156/
    │   └── ...
    └── ...
```

---

## Recommended Workflow for SD Engine V6

### Phase 1: Core Risk Parameters (20 trials, fast)
```bash
python optimizer.py --config config.txt --backtest-cmd "./backtest {config}" \
  --strategy random --objective composite --trials 20 --params-groups risk trailing
```

### Phase 2: Genetic Optimization on Best Groups (100 trials)
```bash
python optimizer.py --config optimization_runs/BEST/best_config.txt \
  --backtest-cmd "./backtest {config}" \
  --strategy genetic --objective composite --trials 100 --params-groups all
```

### Phase 3: Bayesian Fine-tuning (50 trials)
```bash
python optimizer.py --config optimization_runs/BEST/best_config.txt \
  --backtest-cmd "./backtest {config}" \
  --strategy bayesian --objective risk_adjusted --trials 50 \
  --params-groups risk entry scoring
```

---

## Dashboard

Open `dashboard.html` in a browser to see:
- Score convergence chart
- P&L vs Win Rate scatter plot
- Top 50 results table (sortable/filterable)
- Optimized vs base parameter diff view
- Parameter sensitivity analysis
- Score distribution histogram

**Integration for live monitoring:**
When running the optimizer, have the dashboard fetch `all_results.csv` from your
output directory by replacing the `generateDemoData()` call in the dashboard with:
```javascript
const resp = await fetch('./optimization_runs/all_results.csv');
// ... parse CSV
```

---

## Adding Custom Parameters

In `optimizer.py`, add entries to `SEARCH_SPACE`:

```python
"my_new_param": {
    "type": "float",     # or "int"
    "min": 1.0,
    "max": 5.0,
    "step": 0.5,
    "group": "risk",     # group for --params-groups filtering
    "desc": "Description of what this controls"
},
```

---

## Locked Parameters (never modified)

These are locked in `LOCKED_PARAMS` and never touched by the optimizer:
- `starting_capital`, `lot_size`, `commission_per_trade`
- `session_end_time`, `volume_baseline_file`
- `skip_violated_zones`, `v6_fully_enabled`
- All boolean feature flags, indicator periods

Edit `LOCKED_PARAMS` in `optimizer.py` to add/remove.
