#!/bin/bash
# Helper script to print capture commands.
# Since SSH + sudo automation is unreliable across systems,
# run tcpdump manually on each VM in the shared project directory.

echo "Capture Helper"
echo "=============="
echo ""
echo "Open a terminal on each VM and run the commands below."
echo "These will write .pcap files to the shared project directory."
echo ""
echo "VM-A (while running tests from VM-A):"
echo "  sudo tcpdump -i any -w ./vm_a.pcap 'udp port 9999 or icmp'"
echo ""
echo "VM-B (while decapsulator and http_server are running):"
echo "  sudo tcpdump -i any -w ./vm_b.pcap 'udp port 9999 or icmp or tcp port 8080'"
echo ""
echo "After testing, Ctrl-C each tcpdump. The .pcap files will be in"
echo "the current directory, visible from the host if using a shared folder."
echo ""
echo "Wireshark tips:"
echo "  Filter: udp.port == 9999"
echo "  Look for: outer IP fragmentation when inner + 40 > interface MTU"
