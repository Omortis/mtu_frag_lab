# SD-WAN Packet Mangling Lab — Implementation Plan

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
    eth0: DHCP (bridged)              eth0: DHCP (bridged)
    UDP dst: <VM-B eth0 IP>:9999      (outer transport)
    Inner: 10.200.0.x  (read from tun0)
```

## Phase 1: VM Provisioning & Verification

### VM-A & VM-B Setup
- UTM Ubuntu Server 22.04/24.04 VMs, bridged networking
- Install build tools: `sudo apt update && sudo apt install build-essential tcpdump`
- Verify mutual reachability: `ping` each other on `eth0`

### TUN Interface Setup (both VMs)
```bash
sudo ip tuntap add mode tun user $(whoami) name tun0
sudo ip addr add 10.200.0.1/24 dev tun0  # VM-A
sudo ip addr add 10.200.0.2/24 dev tun0  # VM-B
sudo ip link set tun0 up
```

### Routing (VM-A)
```bash
sudo ip route add 10.200.0.2 dev tun0
```
This ensures ICMP/TCP to `10.200.0.2` is injected into `tun0` rather than dropped.

---

## Phase 2: C Code — Encapsulator (VM-A)

### Responsibilities
1. Open TUN device (`/dev/net/tun`)
2. Open UDP socket to VM-B:9999
3. `select()` on TUN fd and UDP fd
4. When TUN readable: read raw IP packet, prepend 40-byte header, send UDP

### 40-Byte Custom Header (`common.h`)
```c
#define HEADER_LEN 40
#define MAGIC 0x54574947  // 'TWIG'

struct custom_hdr {
    uint32_t magic;        // 0x54574947
    uint8_t  version;      // 0x01
    uint8_t  flags;        // reserved
    uint16_t payload_len;  // original inner packet length (network byte order)
    uint32_t session_id;   // tunnel session
    uint32_t seq_num;      // per-packet sequence
    uint64_t timestamp;    // nanoseconds since epoch
    uint32_t src_tun_addr; // inner source IP (network byte order)
    uint32_t dst_tun_addr; // inner dest IP (network byte order)
    uint8_t  reserved[8];  // padding / future use
} __attribute__((packed));
```

### Encapsulator Loop
```c
// Pseudocode
while (1) {
    select(tun_fd, udp_fd);
    if (tun_fd readable) {
        n = read(tun_fd, buf, MTU);
        hdr = build_header(n, buf);
        memcpy(packet, &hdr, 40);
        memcpy(packet + 40, buf, n);
        sendto(udp_fd, packet, 40 + n, dst_addr);
    }
    if (udp_fd readable) {
        n = recvfrom(udp_fd, buf, ...);
        hdr = (struct custom_hdr *)buf;
        if (validate(hdr)) {
            write(tun_fd, buf + 40, hdr->payload_len);
        }
    }
}
```

---

## Phase 3: C Code — Decapsulator (VM-B)

### Responsibilities
1. Open TUN device
2. Bind UDP socket to 0.0.0.0:9999
3. `select()` on TUN fd and UDP fd
4. When UDP readable: validate magic, strip 40 bytes, write remainder to TUN. Remember the peer's source address on the first valid packet.
5. When TUN readable: read the packet, prepend a 40-byte header, and send UDP back to the remembered peer address.

### Decapsulator Loop
```c
while (1) {
    select(tun_fd, udp_fd);
    if (udp_fd readable) {
        n = recvfrom(udp_fd, buf, sizeof(buf), ...);
        if (n < 40) continue;
        hdr = (struct custom_hdr *)buf;
        if (ntohl(hdr->magic) != MAGIC) continue;
        payload_len = ntohs(hdr->payload_len);
        write(tun_fd, buf + 40, payload_len);
        if (!have_peer) { remember_peer(src_addr); }
    }
    if (tun_fd readable && have_peer) {
        n = read(tun_fd, buf, MTU);
        hdr = build_header(n, buf);
        memcpy(packet, &hdr, 40);
        memcpy(packet + 40, buf, n);
        sendto(udp_fd, packet, 40 + n, peer_addr);
    }
}
```

---

## Phase 4: C Code — Tiny HTTP Server (VM-B, TUN side)

### Responsibilities
- Bind to `10.200.0.2:8080` on VM-B's TUN interface
- Single-threaded `select()` server
- Responds to `GET /` with a static HTML page
- Purpose: demonstrate that a TCP 3-way handshake + HTTP request/response survives IP fragmentation in the tunnel

### Minimal Implementation
```c
// Binds to 10.200.0.2:8080, accepts one connection at a time
// Reads until \r\n\r\n, writes HTTP/1.1 200 OK + body
// No threading — just accept, read, write, close
```

---

## Phase 5: Go Testbench

### Directory: `testbench/`

#### `main.go`
- Orchestrates subcommands via `flag`

#### `ping.go`
- Sends ICMP echo requests to `10.200.0.2` by opening a raw socket or using OS `ping`
- Verifies that ICMP replies return through the tunnel

#### `udpgen.go`
- Generates UDP datagrams of configurable size to `10.200.0.2:9999`
- Used to find the exact fragmentation threshold

#### `mtu_probe.go`
- Binary search approach: send increasingly large UDP packets until fragmentation is observed (or replies stop)
- Reports the maximum unfragmented payload size
- This is the "MTU / fragmentation" angle from interview prep

#### `http_client.go`
- Sends `GET http://10.200.0.2:8080/` and prints response
- Confirms TCP + HTTP survive the tunnel

---

## Phase 6: Capture & Analysis

### VM-A Capture
```bash
sudo tcpdump -i any -w ./vm_a.pcap 'udp port 9999 or icmp'
```

### VM-B Capture
```bash
sudo tcpdump -i any -w ./vm_b.pcap 'udp port 9999 or icmp or tcp port 8080'
```

If the VMs are not using a shared folder, transfer the pcaps manually:
```bash
scp vm-a:~/projects/mtu_frag_lab/vm_a.pcap .
scp vm-b:~/projects/mtu_frag_lab/vm_b.pcap .
```

### Wireshark Analysis
- **Filter**: `udp.port == 9999` to isolate tunnel traffic
- **Observe**: Fragmentation on outer IP layer when inner packet + 40 > interface MTU
- **Verify**: Inner packet (e.g., ICMP or TCP) is intact after reassembly

---

## Phase 7: Wireshark Lua Dissector

### File: `twingate_lab.lua`

```lua
local twig_proto = Proto("TWIG", "Twingate Lab Custom Header")

local f_magic        = ProtoField.uint32("twig.magic",        "Magic",        base.HEX)
local f_version      = ProtoField.uint8 ("twig.version",      "Version",      base.DEC)
local f_flags        = ProtoField.uint8 ("twig.flags",        "Flags",        base.HEX)
local f_payload_len  = ProtoField.uint16("twig.payload_len",  "Payload Len",  base.DEC)
local f_session_id   = ProtoField.uint32("twig.session_id",   "Session ID",   base.DEC)
local f_seq_num      = ProtoField.uint32("twig.seq_num",      "Sequence Num", base.DEC)
local f_timestamp    = ProtoField.uint64("twig.timestamp",    "Timestamp",    base.DEC)
local f_src_tun      = ProtoField.ipv4 ("twig.src_tun",      "Src TUN Addr")
local f_dst_tun      = ProtoField.ipv4 ("twig.dst_tun",      "Dst TUN Addr")
local f_reserved     = ProtoField.bytes("twig.reserved",     "Reserved")

twig_proto.fields = {
    f_magic, f_version, f_flags, f_payload_len,
    f_session_id, f_seq_num, f_timestamp,
    f_src_tun, f_dst_tun, f_reserved
}

function twig_proto.dissector(buffer, pinfo, tree)
    if buffer:len() < 40 then return end
    local subtree = tree:add(twig_proto, buffer(), "Twingate Lab Header")
    subtree:add(f_magic,       buffer(0,4))
    subtree:add(f_version,     buffer(4,1))
    subtree:add(f_flags,       buffer(5,1))
    subtree:add(f_payload_len, buffer(6,2))
    subtree:add(f_session_id,  buffer(8,4))
    subtree:add(f_seq_num,     buffer(12,4))
    subtree:add(f_timestamp,   buffer(16,8))
    subtree:add(f_src_tun,     buffer(24,4))
    subtree:add(f_dst_tun,     buffer(28,4))
    subtree:add(f_reserved,    buffer(32,8))
end

-- Register for UDP port 9999
local udp_table = DissectorTable.get("udp.port")
udp_table:add(9999, twig_proto)
```

### Installation (macOS)
```bash
mkdir -p ~/.config/wireshark/plugins
cp twingate_lab.lua ~/.config/wireshark/plugins/
```

### Hex Visibility Note
Yes — hex values are visible either way. The dissector simply maps bytes 0–39 to named fields in the Wireshark packet detail pane. Without it, you can still expand the UDP payload and read the hex directly. The Lua script just makes inspection faster and prettier.

---

## Phase 8: The MTU / Fragmentation "Aha"

### Test Procedure
1. Start encapsulator, decapsulator, HTTP server
2. From VM-A, run: `./testbench mtu-probe -target 10.200.0.2:9999`
3. Observe in Wireshark:
   - Unfragmented packets up to a threshold
   - Beyond threshold: outer IP layer fragments into 2+ pieces
   - Inner packet never fragments — it's the payload of the UDP datagram

### Expected Math
- Standard Ethernet MTU: 1500
- UDP datagram carrying 1500-byte inner packet + 40-byte header = 1540 payload
- Outer IPv4 header: 20 bytes
- Total on wire: 1560 bytes → exceeds 1500, triggers fragmentation

**Fragment 1**: 1500 bytes total (20 IP + 8 UDP + 40 header + 1432 inner)  
**Fragment 2**: 68 bytes total (20 IP + 48 remaining inner)  
More Fragments flag = 0 on fragment 2.

---

## File Inventory (Final Repo Structure)

```
mtu_frag_lab/
├── README.md                 # Build/run instructions + troubleshooting
├── mtu_frag_lab.md           # This implementation plan
├── Makefile
├── .gitignore
├── common.h                  # Shared header struct
├── encapsulator.c            # VM-A (bidirectional)
├── decapsulator.c            # VM-B (bidirectional)
├── http_server.c             # VM-B (TUN side)
├── vma-setup.sh              # VM-A TUN interface bootstrap
├── vmb-setup.sh              # VM-B TUN interface bootstrap
├── capture.sh                # Capture helper script
├── twingate_lab.lua          # Wireshark dissector
├── mtu_lab_layout.lua        # WezTerm 6-pane layout
└── testbench/
    ├── go.mod
    ├── main.go
    ├── ping.go
    ├── udpgen.go
    ├── mtu_probe.go
    └── http_client.go
```

---

## Validation Checklist

- [x] VM provisioning and TUN interface setup
- [x] Bidirectional C tunnel (encapsulator + decapsulator)
- [x] HTTP server on VM-B TUN interface
- [x] Go testbench (ping, udpgen, mtu-probe, http)
- [x] End-to-end capture and Wireshark analysis
- [x] Lua dissector installation and verification
- [ ] MTU fragmentation observation (requires large inner payload > 1460 bytes)
