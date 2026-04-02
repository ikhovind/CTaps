#!/bin/bash
# Network emulation setup script for benchmark testing
# This script uses tc (traffic control) and netem to simulate realistic network conditions

set -e

print_usage() {
    echo "Usage: $0 {setup|teardown|status} [interface] [bandwidth_mbit] ip:rtt_ms:[jitter] ... "
    echo ""
    echo "Examples:"
    echo "  $0 setup lo 100 127.0.0.1:50         # 50ms RTT, 0ms jitter"
    echo "  $0 setup lo 100 127.0.0.1:50:7       # 50ms RTT, 7ms jitter"
    echo "  $0 teardown                      # Remove network emulation"
    echo "  $0 status                        # Show current settings"
    echo ""
    echo "BDP Calculation:"
    echo "  BDP = Bandwidth × RTT"
    echo "  For cwnd=200: BDP = 200 packets × 1500 bytes = 300 KB"
    echo "  Required bandwidth = 300 KB / 0.05s = 48 Mbps (minimum)"
    echo ""
}

# Usage: setup_network_full lo 100 127.0.0.1:25 127.0.0.2:300
setup_network_full() {
    local iface=$1
    local bw=$2
    shift 2
    local pairs=("$@")

    echo "Setting up network on $iface (bw=${bw}mbit):"

    echo "Disabling offloading on $iface to ensure jitter accuracy..."

    sudo sysctl -w net.ipv4.conf.all.accept_local=1 2>/dev/null || true
    sudo tc qdisc del dev "$iface" root 2>/dev/null || true

    sudo ifconfig lo mtu 1500

    # HTB root with aggregate bandwidth cap; class 1:99 is the default
    sudo tc qdisc add dev "$iface" root handle 1: htb default 99

    # Default class: bandwidth cap, no delay
    sudo tc class add dev "$iface" parent 1: classid 1:99 htb rate "${bw}mbit" ceil "${bw}mbit"
    sudo tc qdisc add dev "$iface" parent 1:99 handle 99: pfifo

    local band=10
    for pair in "${pairs[@]}"; do
        local ip="${pair%%:*}"
        local rtt="${pair##*:}"
        local delay=$(( rtt / 2 ))
        local jitter=$(echo "$pair" | cut -s -d: -f3) # Empty if not provided

        # Build netem command string
        local netem_cmd="delay ${delay}ms"
        if [ -n "$jitter" ] && [ "$jitter" != "0" ]; then
            # 25% correlation is standard; increase to 50% for smoother tests
            netem_cmd="$netem_cmd ${jitter}ms 25% distribution normal"
        fi

        sudo tc class add dev "$iface" parent 1: classid "1:$band" htb rate "${bw}mbit" ceil "${bw}mbit"
        sudo tc qdisc add dev "$iface" parent "1:$band" handle "${band}:" netem $netem_cmd

        # sudo tc filter add dev "$iface" parent 1: protocol ip prio $band \
        #     u32 match ip dst "${ip}/32" match ip dport 8080 0xffff flowid "1:$band"
        # sudo tc filter add dev "$iface" parent 1: protocol ip prio $((band + 1)) \
        #     u32 match ip src "${ip}/32" match ip sport 8080 0xffff flowid "1:$band"
        sudo tc filter add dev "$iface" parent 1: protocol ip prio 1 \
            u32 match ip dport 8080 0xffff flowid "1:$band"

        sudo tc filter add dev "$iface" parent 1: protocol ip prio 1 \
            u32 match ip sport 8080 0xffff flowid "1:$band"

        band=$(( band + 10 ))
    done

    echo "Network configured successfully!"
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

case "$COMMAND" in
    setup)
        if [ $# -lt 4 ]; then
            echo "Error: setup requires interface, bandwidth, and at least one ip:rtt pair"
            echo "  Usage: $0 setup <interface> <bandwidth_mbit> <ip:rtt_ms> [ip:rtt_ms ...]"
            exit 1
        fi
        setup_network_full "$2" "$3" "${@:4}"
        ;;
    teardown|clean)
        if [ $# -lt 2 ]; then
            echo "Error: teardown requires interface"
            echo "  Usage: $0 teardown <interface>"
            exit 1
        fi
        teardown_network "$2"
        ;;
    status|show)
        if [ $# -lt 2 ]; then
            echo "Error: status requires interface"
            echo "  Usage: $0 status <interface>"
            exit 1
        fi
        show_status "$2"
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
