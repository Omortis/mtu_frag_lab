#!/bin/bash
# VM-A TUN interface setup script
# Run after every VM boot

set -e

TUN_DEV="tun0"
TUN_ADDR="10.200.0.1/24"
REMOTE_TUN="10.200.0.2"

echo "Setting up ${TUN_DEV} on VM-A..."

# Create TUN device if it doesn't exist
if ! ip link show "${TUN_DEV}" &>/dev/null; then
    sudo ip tuntap add mode tun user "$(whoami)" name "${TUN_DEV}"
fi

# Assign address
sudo ip addr add "${TUN_ADDR}" dev "${TUN_DEV}"

# Bring up
sudo ip link set "${TUN_DEV}" up

# Route traffic for the remote TUN address into our TUN device
sudo ip route add "${REMOTE_TUN}" dev "${TUN_DEV}"

echo "VM-A TUN setup complete:"
ip addr show "${TUN_DEV}"

# Uncomment to auto-start the encapsulator after setup:
# sudo ./encapsulator "${REMOTE_TUN}" 9999
