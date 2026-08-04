package main

import (
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"
	"syscall"
	"time"
)

func mtuProbeCmd(args []string) {
	fs := flag.NewFlagSet("mtu-probe", flag.ExitOnError)
	target := fs.String("target", "10.200.0.2:9000", "Target host:port")
	timeout := fs.Duration("timeout", 2*time.Second, "Reply timeout")
	fs.Parse(args)

	host, portStr, err := net.SplitHostPort(*target)
	if err != nil {
		fmt.Fprintf(os.Stderr, "invalid target: %v\n", err)
		os.Exit(1)
	}
	port, err := strconv.Atoi(portStr)
	if err != nil {
		fmt.Fprintf(os.Stderr, "invalid port: %v\n", err)
		os.Exit(1)
	}

	addr := &net.UDPAddr{IP: net.ParseIP(host), Port: port}
	conn, err := net.DialUDP("udp", nil, addr)
	if err != nil {
		fmt.Fprintf(os.Stderr, "dial error: %v\n", err)
		os.Exit(1)
	}
	defer conn.Close()

	rawConn, err := conn.SyscallConn()
	if err != nil {
		fmt.Fprintf(os.Stderr, "SyscallConn error: %v\n", err)
		os.Exit(1)
	}
	err = rawConn.Control(func(fd uintptr) {
		if err := syscall.SetsockoptInt(int(fd), syscall.IPPROTO_IP, ipMTUDiscover, ipPMTUDiscDo); err != nil {
			fmt.Fprintf(os.Stderr, "setsockopt DF error: %v\n", err)
		}
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "Control error: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("Probing MTU to %s (DF bit set, timeout=%v)...\n", *target, *timeout)

	low := 1
	high := 2000
	maxPayload := 0

	for low <= high {
		mid := (low + high) / 2
		payload := make([]byte, mid)
		for i := range payload {
			payload[i] = byte(i % 256)
		}

		_, err := conn.Write(payload)
		if err != nil {
			high = mid - 1
		} else {
			maxPayload = mid
			low = mid + 1
		}
		time.Sleep(10 * time.Millisecond)
	}

	fmt.Printf("Maximum unfragmented UDP payload: %d bytes\n", maxPayload)
	fmt.Printf("Implied path MTU: %d bytes (20 IP + 8 UDP + payload)\n", maxPayload+28)
}
