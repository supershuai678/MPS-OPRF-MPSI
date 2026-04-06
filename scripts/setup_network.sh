#!/bin/bash
# Network environment simulation using tc (traffic control)
#
# Usage:
#   sudo bash scripts/setup_network.sh lan    # 20Gbps, 0.01ms delay
#   sudo bash scripts/setup_network.sh wan    # 200Mbps, 48ms delay
#   sudo bash scripts/setup_network.sh reset  # restore default

set -e

IFACE=${IFACE:-lo}

case "$1" in
    lan)
        sudo tc qdisc del dev $IFACE root 2>/dev/null || true
        sudo tc qdisc add dev $IFACE root netem delay 0.01ms rate 20gbit
        echo "LAN mode: 20Gbps, 0.01ms delay on $IFACE"
        ;;
    wan)
        sudo tc qdisc del dev $IFACE root 2>/dev/null || true
        sudo tc qdisc add dev $IFACE root netem delay 48ms rate 200mbit
        echo "WAN mode: 200Mbps, 48ms delay on $IFACE"
        ;;
    reset)
        sudo tc qdisc del dev $IFACE root 2>/dev/null || true
        echo "Network reset to default on $IFACE"
        ;;
    *)
        echo "Usage: $0 {lan|wan|reset}"
        exit 1
        ;;
esac
