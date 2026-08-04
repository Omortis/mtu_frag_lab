package main

import (
	"flag"
	"fmt"
	"net"
	"os"
	"syscall"
	"time"
)

// Linux socket option constants (not present in syscall on darwin)
const ipMTUDiscover = 10 // IP_MTU_DISCOVER
const ipPMTUDiscDo  = 2  // IP_PMTUDISC_DO

func udpgenCmd(args []string) {
	fs := flag.NewFlagSet("udpgen", flag.ExitOnError)
	target := fs.String("target", "10.200.0.2:9000", "Target host:port")
	size := fs.Int("size", 1400, "Payload size in bytes")
	count := fs.Int("count", 10, "Number of datagrams")
	interval := fs.Duration("interval", 100*time.Millisecond, "Interval between sends")
	df := fs.Bool("df", false, "Set Don't Fragment bit")
	fs.Parse(args)

	conn, err := net.Dial("udp", *target)
	if err != nil {
		fmt.Fprintf(os.Stderr, "dial error: %v\n", err)
		os.Exit(1)
	}
	defer conn.Close()

	if *df {
		udpConn := conn.(*net.UDPConn)
		rawConn, err := udpConn.SyscallConn()
		if err != nil {
			fmt.Fprintf(os.Stderr, "SyscallConn error: %v\n", err)
			os.Exit(1)
		}
		rawConn.Control(func(fd uintptr) {
			if err := syscall.SetsockoptInt(int(fd), syscall.IPPROTO_IP, ipMTUDiscover, ipPMTUDiscDo); err != nil {
				fmt.Fprintf(os.Stderr, "setsockopt DF error: %v\n", err)
			}
		})
	}

	payload := make([]byte, *size)
	for i := range payload {
		payload[i] = byte(i % 256)
	}

	for i := 0; i < *count; i++ {
		_, err := conn.Write(payload)
		if err != nil {
			fmt.Printf("send %d: error: %v\n", i+1, err)
		} else {
			fmt.Printf("send %d: %d bytes\n", i+1, *size)
		}
		time.Sleep(*interval)
	}
}
