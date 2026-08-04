#!/bin/bash
# VM-B TUN interface setup script
# Run after every VM boot

set -e

TUN_DEV="tun0"
TUN_ADDR="10.200.0.2/24"

echo "Setting up ${TUN_DEV} on VM-B..."

# Create TUN device if it doesn't exist
if ! ip link show "${TUN_DEV}" &>/dev/null; then
    sudo ip tuntap add mode tun user "$(whoami)" name "${TUN_DEV}"
fi

# Assign address
sudo ip addr add "${TUN_ADDR}" dev "${TUN_DEV}"

# Bring up
sudo ip link set "${TUN_DEV}" up

echo "VM-B TUN setup complete:"
ip addr show "${TUN_DEV}"

# Uncomment to auto-start the decapsulator after setup:
# sudo ./decapsulator 9999

# Uncomment to auto-start the HTTP server after setup:
# sudo ./http_server 10.200.0.2 8080
