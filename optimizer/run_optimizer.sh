#!/usr/bin/env bash
# ============================================================
# SD Engine V6 — Optimizer Launcher (Linux / macOS)
# ============================================================
# Edit the variables below, then:  chmod +x run_optimizer.sh && ./run_optimizer.sh

# ── CONFIG ──────────────────────────────────────────────────
CONFIG="./phase_6_config_v0_7_NEW.txt"
BACKTEST_CMD="./backtest {config}"    # ← Your backtest binary + {config} placeholder
OUTPUT_DIR="./optimization_runs"
STRATEGY="genetic"                    # random | grid | genetic | bayesian
OBJECTIVE="composite"                 # composite | net_pnl | sharpe | profit_factor | risk_adjusted
TRIALS=100
PARAMS_GROUPS="risk trailing entry"  # all | risk trailing entry zone volume adx exhaustion timing scoring
POP_SIZE=20                           # For genetic strategy
TIMEOUT=300                           # Seconds per backtest run
SEED=42

# ── DRY RUN (set to --dry-run to test framework without running backtest) ──
# DRY="--dry-run"
DRY=""

# ── RUN ──────────────────────────────────────────────────────
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  SD Engine V6 Parameter Optimizer"
echo "  Strategy: $STRATEGY | Objective: $OBJECTIVE | Trials: $TRIALS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

python3 optimizer.py \
    --config "$CONFIG" \
    --backtest-cmd "$BACKTEST_CMD" \
    --output-dir "$OUTPUT_DIR" \
    --strategy "$STRATEGY" \
    --objective "$OBJECTIVE" \
    --trials "$TRIALS" \
    --params-groups $PARAMS_GROUPS \
    --pop-size "$POP_SIZE" \
    --timeout "$TIMEOUT" \
    --seed "$SEED" \
    $DRY

echo ""
echo "Results → $OUTPUT_DIR/all_results.csv"
echo "Best    → $OUTPUT_DIR/BEST/best_config.txt"
echo "Report  → $OUTPUT_DIR/BEST/best_report.txt"
