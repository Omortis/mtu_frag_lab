# SD-WAN Packet Mangling Lab

## Overview

Two Ubuntu VMs connected over a bridged LAN. VM-A encapsulates raw IP packets from a TUN interface with a 40-byte custom header and forwards them as UDP datagrams to VM-B. VM-B decapsulates and writes the original packet to its own TUN interface, then encapsulates return traffic and sends it back to VM-A. A tiny HTTP server on VM-B's TUN side demonstrates that TCP flows survive fragmentation.

## Network Topology

```
[Host macOS] ---(Bridged)--- [UTM]
                             |
          +------------------+------------------+
          |                                     |
    [VM-A: encapsulator]              [VM-B: decapsulator]
    tun0: 10.200.0.1/24               tun0: 10.200.0.2/24
    enp0s1: DHCP (bridged)            enp0s1: DHCP (bridged)
     UDP dst: <VM-B eth0 IP>:9999      (outer transport)
     Inner: 10.200.0.x  (read from tun0)
```

## Phase 1: VM Provisioning & Verification

### VM-A & VM-B Setup

- UTM Ubuntu Server 26.04 VMs, bridged networking
- Install build tools:
  ```bash
  sudo apt update && sudo apt install build-essential tcpdump
  ```
- Verify mutual reachability: `ping` each other on `enp0s1`

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
./testbench udpgen -target 10.200.0.2:9999 -size 1400 -count 10
```

Options:
- `-target`: Target host:port (default: `10.200.0.2:9999`)
- `-size`: Payload size in bytes (default: `1400`)
- `-count`: Number of datagrams (default: `10`)
- `-interval`: Interval between sends (default: `100ms`)
- `-df`: Set Don't Fragment bit (Linux only)

#### MTU Probe

Binary search for path MTU via UDP with the Don't Fragment bit set:

```bash
./testbench mtu-probe -target 10.200.0.2:9999
```

Options:
- `-target`: Target host:port (default: `10.200.0.2:9999`)
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
   ./testbench mtu-probe -target 10.200.0.2:9999
   ```
4. Verify HTTP through the tunnel:
   ```bash
   ./testbench http -url http://10.200.0.2:8080/
   ```

## Capture & Analysis

### Manual Capture (Recommended)

Open a terminal on **each VM** and run `tcpdump` in the shared project directory. This writes `.pcap` files that are immediately visible from the host if you're using a shared folder.

**VM-A:**
```bash
sudo tcpdump -i any -w ./vm_a.pcap 'udp port 9999 or icmp'
```

**VM-B:**
```bash
sudo tcpdump -i any -w ./vm_b.pcap 'udp port 9999 or icmp or tcp port 8080'
```

While both captures are running, execute your tests from VM-A (e.g., `ping`, `udpgen`, `mtu-probe`, `http`). Then `Ctrl-C` each `tcpdump` to stop the capture.

If the VMs are not using a shared folder, transfer the pcaps manually:
```bash
scp vm-a:~/projects/mtu_frag_lab/vm_a.pcap .
scp vm-b:~/projects/mtu_frag_lab/vm_b.pcap .
```

### Wireshark Analysis

- **Filter**: `udp.port == 9999` to isolate tunnel traffic
- **Observe**: Fragmentation on outer IP layer when inner packet + 40 > interface MTU
- **Verify**: Inner packet (e.g., ICMP or TCP) is intact after reassembly

## Wireshark Lua Dissector

A custom dissector for the 40-byte TWIG header is available in `twig_dissector.lua`.

### Installation (macOS)

```bash
mkdir -p ~/.config/wireshark/plugins
cp twig_dissector.lua ~/.config/wireshark/plugins/
```

The dissector maps bytes 0–39 to named fields in the Wireshark packet detail pane.

## WezTerm Layout

A 6-pane WezTerm layout is provided for managing the lab from a single window.

### Installation

```bash
cp mtu_lab_layout.lua ~/.config/wezterm/
```

### Integration

Add to `~/.wezterm.lua`:

```lua
local lab_path = os.getenv("HOME") .. "/.config/wezterm/mtu_lab_layout.lua"
local lab = dofile(lab_path)

config.keys = config.keys or {}
table.insert(config.keys, {
    key = 'M', mods = 'CTRL|SHIFT', action = wezterm.action_callback(lab.apply),
})
```

### Layout

| Column 1 (macOS) | Column 2 (VM-A) | Column 3 (VM-A) |
|------------------|-----------------|-----------------|
| Local shell      | `ssh 192.168.64.3` | `ssh 192.168.64.3` |
| Local shell      | `ssh 192.168.64.5` | `ssh 192.168.64.5` |

Press **Ctrl+Shift+M** in WezTerm to spawn the layout.
