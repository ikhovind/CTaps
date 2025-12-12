#!/bin/bash
# Network emulation setup script for benchmark testing
# This script uses tc (traffic control) and netem to simulate realistic network conditions

set -e

INTERFACE="lo"  # loopback interface for local testing
RTT_MS=50       # Round-trip time in milliseconds (25ms delay each way)
BANDWIDTH_MBIT=100  # Bandwidth in Mbit/s

print_usage() {
    echo "Usage: $0 {setup|teardown|status} [interface] [rtt_ms] [bandwidth_mbit]"
    echo ""
    echo "Default values:"
    echo "  interface: lo (loopback)"
    echo "  rtt_ms: 50ms"
    echo "  bandwidth_mbit: 100 Mbit/s"
    echo ""
    echo "Examples:"
    echo "  $0 setup                    # Use defaults (50ms RTT, 100 Mbps)"
    echo "  $0 setup lo 100 50 1        # 100ms RTT, 50 Mbps"
    echo "  $0 teardown                 # Remove network emulation"
    echo "  $0 status                   # Show current settings"
    echo ""
    echo "BDP Calculation:"
    echo "  BDP = Bandwidth × RTT"
    echo "  For cwnd=200: BDP = 200 packets × 1500 bytes = 300 KB"
    echo "  Required bandwidth = 300 KB / 0.05s = 48 Mbps (minimum)"
}

setup_network() {
    local iface=$1
    local rtt=$2
    local bw=$3

    echo "Setting up network emulation on $iface:"
    echo "  RTT: ${rtt}ms"
    echo "  Bandwidth: ${bw} Mbit/s"

    # Calculate delay (half of RTT for each direction)
    local delay=$((rtt / 2))

    # Calculate BDP
    local bdp_bits=$((bw * 1000000 * rtt / 1000))
    local bdp_bytes=$((bdp_bits / 8))
    local bdp_kb=$((bdp_bytes / 1024))
    local bdp_packets=$((bdp_bytes / 1500))

    echo "  Calculated BDP: ${bdp_kb} KB (≈${bdp_packets} packets)"

    if [ $bdp_packets -lt 200 ]; then
        echo "  WARNING: BDP is less than 200 packets. cwnd may not reach target!"
    fi

    # Disable GSO and TSO for accurate packet sizing
    echo "  Disabling GSO/TSO on $iface for accurate MSS-sized packets..."
    sudo ethtool -K "$iface" gso off tso off 2>/dev/null || echo "  (ethtool not available or not applicable)"

    # Remove existing qdisc if present
    sudo tc qdisc del dev "$iface" root 2>/dev/null || true

    sudo tc qdisc add dev "$iface" parent root handle 1: netem delay "${delay}ms" rate "${bw}mbit"

    echo "Network emulation configured successfully!"
}

teardown_network() {
    local iface=$1

    echo "Removing network emulation from $iface..."
    sudo tc qdisc del dev "$iface" root 2>/dev/null || echo "No qdisc found (already clean)"

    # Re-enable GSO and TSO
    echo "Re-enabling GSO/TSO on $iface..."
    sudo ethtool -K "$iface" gso on tso on 2>/dev/null || echo "(ethtool not available or not applicable)"

    echo "Network emulation removed!"
}

show_status() {
    local iface=$1

    echo "Current network emulation status for $iface:"
    echo ""
    sudo tc qdisc show dev "$iface" || echo "No emulation configured"
    echo ""
    sudo tc -s qdisc show dev "$iface" 2>/dev/null || true
}

# Parse command line arguments
COMMAND=${1:-}
if [ -z "$COMMAND" ]; then
    print_usage
    exit 1
fi

# Override defaults with command line args if provided
[ -n "$2" ] && INTERFACE=$2
[ -n "$3" ] && RTT_MS=$3
[ -n "$4" ] && BANDWIDTH_MBIT=$4

case "$COMMAND" in
    setup)
        setup_network "$INTERFACE" "$RTT_MS" "$BANDWIDTH_MBIT"
        ;;
    teardown|clean)
        teardown_network "$INTERFACE"
        ;;
    status|show)
        show_status "$INTERFACE"
        ;;
    help|--help|-h)
        print_usage
        ;;
    *)
        echo "Error: Unknown command '$COMMAND'"
        echo ""
        print_usage
        exit 1
        ;;
esac
