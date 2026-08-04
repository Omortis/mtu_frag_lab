#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

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

#endif
