#!/bin/bash
# Reproduce Table 5 — full parameter sweep across all 9 protocols
#
# Protocols: oprf, oprf-hh, bc, bc-hh, ring, ring-hh, bzs, oring-ring, oring-star
# Set sizes: 2^12, 2^16, 2^20
# (n,t) combos: (4,1) (4,3) (10,1) (10,4) (10,9) (15,1) (15,4) (15,7) (15,14)
# Networks: LAN
#
# Usage:
#   bash scripts/run_table5.sh
#
# NOTE: WAN mode requires sudo for tc commands
# NOTE: BZS-MPSI and O-Ring use fixed ports — protocols run serially

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/config.sh"
LOG_DIR="$PROJECT_DIR/logs"

mkdir -p "$LOG_DIR"

PROTOCOLS="oprf oprf-hh bc bc-hh ring ring-hh bzs oring-ring oring-star mpsi-paxos multipartypsi"
SET_SIZES="12 16 20"
NT_COMBOS="4,1 4,3 10,1 10,4 10,9 15,1 15,4 15,7 15,14"
SETTINGS="lan wan"

TOTAL=0
PASSED=0
FAILED=0
FAILED_LIST=""

echo "========================================"
echo "Table 5 Full Benchmark"
echo "Protocols: $PROTOCOLS"
echo "Log dir:   $LOG_DIR"
echo "Start:     $(date)"
echo "========================================"
echo ""

for net in $SETTINGS; do
    echo "==== Network: $net ===="

    # Set up network environment
    if [ "$net" == "wan" ]; then
        echo "[INFO] Setting WAN mode (200Mbps, 48ms delay)..."
        sudo bash "$SCRIPT_DIR/setup_network.sh" wan || echo "[WARN] Network setup failed, continuing..."
    else
        echo "[INFO] Setting LAN mode (20Gbps, 0.01ms delay)..."
        sudo bash "$SCRIPT_DIR/setup_network.sh" lan || echo "[WARN] Network setup failed, continuing..."
    fi

    for proto in $PROTOCOLS; do
        for nn in $SET_SIZES; do
            for nt in $NT_COMBOS; do
                n=$(echo "$nt" | cut -d, -f1)
                t=$(echo "$nt" | cut -d, -f2)

                TOTAL=$((TOTAL + 1))

                # Use unique port base for Our protocols (avoid conflicts)
                PORT_BASE=$((20000 + RANDOM % 10000))

                LOG_FILE="$LOG_DIR/${net}_${proto}_n${n}_t${t}_m${nn}.log"

                echo ""
                echo ">>> [$TOTAL] $proto | n=$n, t=$t | m=2^$nn | $net"

                if bash "$SCRIPT_DIR/run_benchmark.sh" "$proto" "$n" "$t" "$nn" "$net" "$PORT_BASE" \
                    > "$LOG_FILE" 2>&1; then
                    PASSED=$((PASSED + 1))
                    echo "    [OK] -> $LOG_FILE"
                else
                    FAILED=$((FAILED + 1))
                    FAILED_LIST="$FAILED_LIST  $proto n=$n,t=$t m=2^$nn $net\n"
                    echo "    [FAIL] -> $LOG_FILE"
                fi

                # Small delay between tests to release ports
                sleep 2
            done
        done
    done

    # Reset network
    sudo bash "$SCRIPT_DIR/setup_network.sh" reset || true
    echo ""
done

echo "========================================"
echo "Table 5 Benchmark Complete"
echo "End:    $(date)"
echo "Total:  $TOTAL"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
if [ -n "$FAILED_LIST" ]; then
    echo ""
    echo "Failed tests:"
    echo -e "$FAILED_LIST"
fi
echo "========================================"
echo ""
echo "Run 'bash scripts/parse_results.sh' to generate CSV."
