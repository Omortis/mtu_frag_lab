package main

import (
	"fmt"
	"os"
)

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(1)
	}

	switch os.Args[1] {
	case "ping":
		pingCmd(os.Args[2:])
	case "udpgen":
		udpgenCmd(os.Args[2:])
	case "mtu-probe":
		mtuProbeCmd(os.Args[2:])
	case "http":
		httpCmd(os.Args[2:])
	default:
		usage()
		os.Exit(1)
	}
}

func usage() {
	fmt.Fprintf(os.Stderr, "Usage: %s <command> [options]\n\n", os.Args[0])
	fmt.Fprintf(os.Stderr, "Commands:\n")
	fmt.Fprintf(os.Stderr, "  ping       Send ICMP echo requests (requires root)\n")
	fmt.Fprintf(os.Stderr, "  udpgen     Generate UDP datagrams of configurable size\n")
	fmt.Fprintf(os.Stderr, "  mtu-probe  Binary search for path MTU via UDP (DF bit)\n")
	fmt.Fprintf(os.Stderr, "  http       Send HTTP GET and print response\n")
	fmt.Fprintf(os.Stderr, "\nUse %s <command> -h for command-specific help.\n", os.Args[0])
}
