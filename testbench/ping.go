package main

import (
	"flag"
	"fmt"
	"net"
	"os"
	"time"
)

func pingCmd(args []string) {
	fs := flag.NewFlagSet("ping", flag.ExitOnError)
	target := fs.String("target", "10.200.0.2", "Target IP address")
	count := fs.Int("count", 4, "Number of echo requests")
	interval := fs.Duration("interval", time.Second, "Interval between pings")
	fs.Parse(args)

	dst, err := net.ResolveIPAddr("ip4", *target)
	if err != nil {
		fmt.Fprintf(os.Stderr, "resolve error: %v\n", err)
		os.Exit(1)
	}

	conn, err := net.ListenPacket("ip4:icmp", "")
	if err != nil {
		fmt.Fprintf(os.Stderr, "socket error (need root?): %v\n", err)
		os.Exit(1)
	}
	defer conn.Close()

	id := uint16(os.Getpid() & 0xFFFF)

	fmt.Printf("PING %s (%s):\n", *target, dst.String())

	for i := 0; i < *count; i++ {
		seq := i + 1
		start := time.Now()

		data := make([]byte, 56)
		for j := range data {
			data[j] = byte(j)
		}
		pkt := buildICMP(8, 0, id, uint16(seq), data)

		if _, err := conn.WriteTo(pkt, dst); err != nil {
			fmt.Printf("send seq=%d: %v\n", seq, err)
			continue
		}

		reply := make([]byte, 1500)
		conn.SetReadDeadline(time.Now().Add(2 * time.Second))
		n, peer, err := conn.ReadFrom(reply)
		if err != nil {
			fmt.Printf("seq=%d timeout\n", seq)
			continue
		}

		rtt := time.Since(start)
		if n < 8 {
			fmt.Printf("reply from %s: seq=%d (short packet)\n", peer.String(), seq)
			continue
		}

		// ICMP Echo Reply: type=0, code=0
		if reply[0] == 0 && reply[1] == 0 {
			rid := uint16(reply[4])<<8 | uint16(reply[5])
			rseq := uint16(reply[6])<<8 | uint16(reply[7])
			if rid == id && rseq == uint16(seq) {
				fmt.Printf("reply from %s: seq=%d time=%.2f ms\n",
					peer.String(), seq, float64(rtt.Microseconds())/1000.0)
			} else {
				fmt.Printf("reply from %s: seq=%d (mismatched id/seq %d/%d)\n",
					peer.String(), seq, rid, rseq)
			}
		} else {
			fmt.Printf("reply from %s: unexpected type=%d code=%d\n",
				peer.String(), reply[0], reply[1])
		}

		if i < *count-1 {
			time.Sleep(*interval)
		}
	}
}

func buildICMP(typ, code uint8, id, seq uint16, data []byte) []byte {
	pkt := make([]byte, 8+len(data))
	pkt[0] = typ
	pkt[1] = code
	// checksum placeholder at pkt[2:4]
	pkt[4] = byte(id >> 8)
	pkt[5] = byte(id)
	pkt[6] = byte(seq >> 8)
	pkt[7] = byte(seq)
	copy(pkt[8:], data)

	cs := checksum(pkt)
	pkt[2] = byte(cs >> 8)
	pkt[3] = byte(cs)
	return pkt
}

func checksum(b []byte) uint16 {
	var sum uint32
	for i := 0; i < len(b)-1; i += 2 {
		sum += uint32(b[i])<<8 + uint32(b[i+1])
	}
	if len(b)%2 == 1 {
		sum += uint32(b[len(b)-1]) << 8
	}
	for (sum >> 16) > 0 {
		sum = (sum & 0xFFFF) + (sum >> 16)
	}
	return uint16(^sum)
}
