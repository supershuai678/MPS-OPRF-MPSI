#!/bin/bash
# Run a single benchmark: launch n processes for the given protocol
#
# Usage: bash scripts/run_benchmark.sh <proto> <n> <t> <nn> [net] [port_base]
#   proto: oprf, bc, ring
#   n: number of parties
#   t: malicious threshold
#   nn: log2 of set size
#   net: lan (default), wan
#   port_base: base port (default 20000, only used by Our protocols)
#
# 路径由 scripts/config.sh 自动配置，无需手动设置

set -e

PROTO=${1:?Usage: $0 <proto> <n> <t> <nn> [net] [port_base]}
N=${2:?}
T=${3:?}
NN=${4:?}
NET=${5:-lan}
PORT_BASE=${6:-20000}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/config.sh"

# Network flag for O-Ring: 0=LAN, 1=WAN
NET_FLAG=0
[ "$NET" == "wan" ] && NET_FLAG=1

echo "=== Protocol: $PROTO | n=$N, t=$T | m=2^$NN | net=$NET ==="

PIDS=()

cleanup() {
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null
}
trap cleanup EXIT

case "$PROTO" in
    oprf|bc|ring)
        # Our protocols — GF128 mode
        INNER_PROTO="$PROTO"
        BIN="$BIN_GF128"
        if [ ! -x "$BIN" ]; then
            echo "[ERROR] Binary not found: $BIN"
            exit 1
        fi
        for ((id=0; id<N-1; id++)); do
            "$BIN" -nu "$N" -t "$T" -id "$id" -nn "$NN" -proto "$INNER_PROTO" -net "$NET" -port "$PORT_BASE" &
            PIDS+=($!)
        done
        "$BIN" -nu "$N" -t "$T" -id $((N-1)) -nn "$NN" -proto "$INNER_PROTO" -net "$NET" -port "$PORT_BASE"
        ;;

    oprf-hh|bc-hh|ring-hh)
        # Our protocols — HH mode
        INNER_PROTO="${PROTO%-hh}"
        BIN="$BIN_HH"
        if [ ! -x "$BIN" ]; then
            echo "[ERROR] Binary not found: $BIN"
            exit 1
        fi
        for ((id=0; id<N-1; id++)); do
            "$BIN" -nu "$N" -t "$T" -id "$id" -nn "$NN" -proto "$INNER_PROTO" -net "$NET" -port "$PORT_BASE" &
            PIDS+=($!)
        done
        "$BIN" -nu "$N" -t "$T" -id $((N-1)) -nn "$NN" -proto "$INNER_PROTO" -net "$NET" -port "$PORT_BASE"
        ;;

    bzs)
        # BZS-MPSI — uses fixed ports (10000/10100/10500 series)
        if [ ! -x "$BIN_BZS" ]; then
            echo "[ERROR] Binary not found: $BIN_BZS"
            exit 1
        fi
        for ((id=0; id<N-1; id++)); do
            "$BIN_BZS" -perf -mpsi -nu "$N" -id "$id" -nn "$NN" &
            PIDS+=($!)
        done
        "$BIN_BZS" -perf -mpsi -nu "$N" -id $((N-1)) -nn "$NN"
        ;;

    oring-ring)
        # O-Ring (ring topology = 0)
        if [ "$T" -eq 1 ]; then
            BIN_OR="$BIN_ORING_T1"
        else
            BIN_OR="$BIN_ORING"
        fi
        if [ ! -x "$BIN_OR" ]; then
            echo "[ERROR] Binary not found: $BIN_OR"
            exit 1
        fi
        for ((id=1; id<N; id++)); do
            "$BIN_OR" "$N" "$T" "$NN" "$id" 0 "$NET_FLAG" &
            PIDS+=($!)
        done
        "$BIN_OR" "$N" "$T" "$NN" 0 0 "$NET_FLAG"
        ;;

    oring-star)
        # O-Ring (star topology = 1)
        if [ "$T" -eq 1 ]; then
            BIN_OR="$BIN_ORING_T1"
        else
            BIN_OR="$BIN_ORING"
        fi
        if [ ! -x "$BIN_OR" ]; then
            echo "[ERROR] Binary not found: $BIN_OR"
            exit 1
        fi
        for ((id=1; id<N; id++)); do
            "$BIN_OR" "$N" "$T" "$NN" "$id" 1 "$NET_FLAG" &
            PIDS+=($!)
        done
        "$BIN_OR" "$N" "$T" "$NN" 0 1 "$NET_FLAG"
        ;;

    mpsi-paxos)
        # mPSI-paxos (CCS 2021) — uses fixed ports (1200 + i*100 + j)
        # CLI: ./frontend.exe -m <nn> -n <n> -t <t> -p <pIdx>
        if [ ! -x "$BIN_MPSI_PAXOS" ]; then
            echo "[ERROR] Binary not found: $BIN_MPSI_PAXOS"
            exit 1
        fi
        for ((id=0; id<N-1; id++)); do
            "$BIN_MPSI_PAXOS" -m "$NN" -n "$N" -t "$T" -p "$id" &
            PIDS+=($!)
        done
        "$BIN_MPSI_PAXOS" -m "$NN" -n "$N" -t "$T" -p $((N-1))
        ;;

    multipartypsi)
        # MultipartyPSI (CCS 2017) — uses fixed ports (1200 + i*100 + j)
        # CLI: ./frontend.exe -n <n> -t <t> -m <nn> -p <pIdx>
        if [ ! -x "$BIN_MULTIPARTYPSI" ]; then
            echo "[ERROR] Binary not found: $BIN_MULTIPARTYPSI"
            exit 1
        fi
        for ((id=0; id<N-1; id++)); do
            "$BIN_MULTIPARTYPSI" -n "$N" -t "$T" -m "$NN" -p "$id" &
            PIDS+=($!)
        done
        "$BIN_MULTIPARTYPSI" -n "$N" -t "$T" -m "$NN" -p $((N-1))
        ;;

    *)
        echo "[ERROR] Unknown protocol: $PROTO"
        echo "  Supported: oprf bc ring oprf-hh bc-hh ring-hh bzs oring-ring oring-star mpsi-paxos multipartypsi"
        exit 1
        ;;
esac

# Wait for all background processes
for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
done

trap - EXIT
echo "--- Done ---"
echo ""
