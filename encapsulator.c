#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

#define MTU 1500
#define MAX_PACKET (HEADER_LEN + MTU)

static uint32_t seq_counter = 0;

static int tun_alloc(const char *dev)
{
    // ifreq: 
    struct ifreq ifr;
    int fd, err;

    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        perror("open /dev/net/tun");
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (*dev) {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ - 1);
    }

    err = ioctl(fd, TUNSETIFF, (void *)&ifr);
    if (err < 0) {
        perror("ioctl TUNSETIFF");
        close(fd);
        return err;
    }

    return fd;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void build_header(struct custom_hdr *hdr, size_t payload_len,
                         const uint8_t *packet)
{
    hdr->magic       = htonl(MAGIC);
    hdr->version     = 0x01;
    hdr->flags       = 0x00;
    hdr->payload_len = htons((uint16_t)payload_len);
    hdr->session_id  = htonl(0x00000001);
    hdr->seq_num     = htonl(++seq_counter);
    hdr->timestamp   = htobe64(now_ns());

    if (payload_len >= 20) {
        hdr->src_tun_addr = ((struct in_addr *)(packet + 12))->s_addr;
        hdr->dst_tun_addr = ((struct in_addr *)(packet + 16))->s_addr;
    } else {
        hdr->src_tun_addr = 0;
        hdr->dst_tun_addr = 0;
    }

    memset(hdr->reserved, 0, sizeof(hdr->reserved));
}

int main(int argc, char **argv)
{
    const char *tun_dev   = "tun0";
    const char *remote_ip = (argc > 1) ? argv[1] : "10.200.0.2";
    int remote_port       = (argc > 2) ? atoi(argv[2]) : 9999;

    int tun_fd = tun_alloc(tun_dev);
    if (tun_fd < 0) {
        fprintf(stderr, "Failed to open TUN device\n");
        return 1;
    }

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in dst_addr;
    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port   = htons(remote_port);
    if (inet_pton(AF_INET, remote_ip, &dst_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid remote IP: %s\n", remote_ip);
        return 1;
    }

    printf("Encapsulator: TUN=%s  UDP->%s:%d\n", tun_dev, remote_ip, remote_port);

    uint8_t buf[MTU];
    uint8_t outbuf[MAX_PACKET];

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(tun_fd, &rfds);
        FD_SET(udp_fd, &rfds);
        int maxfd = (tun_fd > udp_fd) ? tun_fd : udp_fd;

        int n = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(tun_fd, &rfds)) {
            ssize_t r = read(tun_fd, buf, sizeof(buf));
            if (r < 0) {
                perror("read tun");
                continue;
            }

            struct custom_hdr hdr;
            build_header(&hdr, (size_t)r, buf);

            memcpy(outbuf, &hdr, HEADER_LEN);
            memcpy(outbuf + HEADER_LEN, buf, (size_t)r);

            ssize_t sent = sendto(udp_fd, outbuf, HEADER_LEN + (size_t)r, 0,
                                  (struct sockaddr *)&dst_addr, sizeof(dst_addr));
            if (sent < 0) {
                perror("sendto");
            } else {
                printf("Encapsulated %zd bytes -> %s:%d (seq=%u)\n",
                       r, remote_ip, remote_port, seq_counter);
            }
        }

        if (FD_ISSET(udp_fd, &rfds)) {
            // Not expected in this direction; drain and ignore.
            uint8_t discard[2048];
            recvfrom(udp_fd, discard, sizeof(discard), 0, NULL, NULL);
        }
    }

    close(tun_fd);
    close(udp_fd);
    return 0;
}
