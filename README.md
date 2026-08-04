# SD-WAN Packet Mangling Lab

## Overview

Two Ubuntu VMs connected over a bridged LAN. VM-A encapsulates raw IP packets from a TUN interface with a 40-byte custom header and forwards them as UDP datagrams to VM-B. VM-B decapsulates and writes the original packet to its own TUN interface. A tiny HTTP server on VM-B's TUN side demonstrates that TCP flows survive fragmentation.

## Network Topology

```
[Host macOS] ---(Bridged)--- [UTM]
                             |
          +------------------+------------------+
          |                                     |
    [VM-A: encapsulator]              [VM-B: decapsulator]
    tun0: 10.200.0.1/24               tun0: 10.200.0.2/24
    eth0: DHCP (bridged)              eth0: DHCP (bridged)
     UDP dst: <VM-B eth0 IP>:9999      (outer transport)
     Inner: 10.200.0.x  (read from tun0)
```

## Phase 1: VM Provisioning & Verification

### VM-A & VM-B Setup

- UTM Ubuntu Server 22.04/24.04 VMs, bridged networking
- Install build tools:
  ```bash
  sudo apt update && sudo apt install build-essential tcpdump
  ```
- Verify mutual reachability: `ping` each other on `eth0`

### TUN Interface Setup

Run the following on **each VM**. The interface must be created, assigned an address, **and brought up** before any application can bind to it.

**VM-A:**
```bash
sudo ip tuntap add mode tun user $(whoami) name tun0
sudo ip addr add 10.200.0.1/24 dev tun0
sudo ip link set tun0 up
```

**VM-B:**
```bash
sudo ip tuntap add mode tun user $(whoami) name tun0
sudo ip addr add 10.200.0.2/24 dev tun0
sudo ip link set tun0 up
```

**Verify the interface is up and has the expected address:**
```bash
ip addr show tun0
```

You should see `state UP` (or `UNKNOWN`) and the correct `inet` line. If the interface shows `state DOWN`, the `ip link set tun0 up` step was missed.

**Important: TUN interfaces are not persistent.** They are lost on reboot or VM shutdown. You must re-run the setup commands above every time the VM starts. Convenience scripts `vma-setup.sh` and `vmb-setup.sh` are provided in the repo to automate this.

### Routing (VM-A)

```bash
sudo ip route add 10.200.0.2 dev tun0
```

This ensures ICMP/TCP to `10.200.0.2` is injected into `tun0` rather than dropped.

## Building the C Components

```bash
make clean && make
```

This produces three binaries:
- `encapsulator` — VM-A tunnel endpoint
- `decapsulator` — VM-B tunnel endpoint
- `http_server` — VM-B HTTP service

### Running the Tunnel

**VM-A** (must be run with `sudo` for TUN device access):
```bash
sudo ./encapsulator [VM-B_ETH0_IP] [REMOTE_PORT]
# Example: sudo ./encapsulator 192.168.64.5 9999
```

> **Important:** The `[VM-B_ETH0_IP]` argument is VM-B's **real bridged LAN address** (e.g., `192.168.64.5`), **not** `10.200.0.2`. The inner packet destination (`10.200.0.2`) is read from `tun0`; the outer UDP envelope is addressed to VM-B's `eth0` so it can actually leave the machine.

**VM-B** (must be run with `sudo` for TUN device access):
```bash
sudo ./decapsulator [LISTEN_PORT]
# Default: sudo ./decapsulator 9999
```

**VM-B** (HTTP server on TUN interface):
```bash
sudo ./http_server [BIND_ADDR] [BIND_PORT]
# Default: sudo ./http_server 10.200.0.2 8080
```

### Troubleshooting

**Stale processes competing for TUN device:**
If you see packets in `tcpdump` but the tunnel binary never logs them, you may have a stale process still holding `/dev/net/tun`. Check with:
```bash
sudo fuser /dev/net/tun
```
If it shows more than one PID, kill all stale instances:
```bash
sudo killall -9 decapsulator
sudo killall -9 encapsulator
```

**TUN interface shows `state DOWN` or bind fails with `Cannot assign requested address`:**
Re-run the setup script for that VM (`vma-setup.sh` or `vmb-setup.sh`). The TUN interface is not persistent across reboots.

**UDP packets leave VM-A but never arrive at VM-B:**
Check the firewall on both VMs:
```bash
sudo ufw status
sudo ufw allow 9999/udp
```
Also verify you started the encapsulator with VM-B's **real bridged LAN IP** (e.g., `192.168.64.5`), not `10.200.0.2`.

## Testbench

The Go testbench provides tools for verifying the tunnel and probing path MTU.

### Building the Testbench

```bash
cd testbench
go build -o testbench .
```

### Testbench Commands

#### Ping

Send ICMP echo requests through the tunnel:

```bash
./testbench ping -target 10.200.0.2 -count 4
```

Options:
- `-target`: Target IP address (default: `10.200.0.2`)
- `-count`: Number of echo requests (default: `4`)
- `-interval`: Interval between pings (default: `1s`)

#### UDP Generator

Generate UDP datagrams of configurable size:

```bash
./testbench udpgen -target 10.200.0.2:9000 -size 1400 -count 10
```

Options:
- `-target`: Target host:port (default: `10.200.0.2:9000`)
- `-size`: Payload size in bytes (default: `1400`)
- `-count`: Number of datagrams (default: `10`)
- `-interval`: Interval between sends (default: `100ms`)
- `-df`: Set Don't Fragment bit (Linux only)

#### MTU Probe

Binary search for path MTU via UDP with the Don't Fragment bit set:

```bash
./testbench mtu-probe -target 10.200.0.2:9000
```

Options:
- `-target`: Target host:port (default: `10.200.0.2:9000`)
- `-timeout`: Reply timeout (default: `2s`)

#### HTTP Client

Send an HTTP GET request and print the response:

```bash
./testbench http -url http://10.200.0.2:8080/
```

Options:
- `-url`: URL to fetch (default: `http://10.200.0.2:8080/`)

### Example Test Workflow

1. Start the tunnel components on both VMs
2. From VM-A, verify ICMP:
   ```bash
   ./testbench ping -target 10.200.0.2
   ```
3. Probe the MTU:
   ```bash
   ./testbench mtu-probe -target 10.200.0.2:9000
   ```
4. Verify HTTP through the tunnel:
   ```bash
   ./testbench http -url http://10.200.0.2:8080/
   ```

## Capture & Analysis

### VM-A Capture

```bash
sudo tcpdump -i any -w vm_a.pcap 'udp port 9999 or icmp'
```

### VM-B Capture

```bash
sudo tcpdump -i any -w vm_b.pcap 'udp port 9999 or icmp or tcp port 8080'
```

### Transfer to Host

```bash
scp vm_a.pcap user@host-mac:~/
scp vm_b.pcap user@host-mac:~/
```

### Wireshark Analysis

- **Filter**: `udp.port == 9999` to isolate tunnel traffic
- **Observe**: Fragmentation on outer IP layer when inner packet + 40 > interface MTU
- **Verify**: Inner packet (e.g., ICMP or TCP) is intact after reassembly

## Wireshark Lua Dissector

A custom dissector for the 40-byte TWIG header is available in `twingate_lab.lua`.

### Installation (macOS)

```bash
mkdir -p ~/.config/wireshark/plugins
cp twingate_lab.lua ~/.config/wireshark/plugins/
```

The dissector maps bytes 0–39 to named fields in the Wireshark packet detail pane.
