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
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

#define MTU 1500
#define MAX_PACKET (HEADER_LEN + MTU)

static int tun_alloc(const char *dev)
{
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

int main(int argc, char **argv)
{
    const char *tun_dev = "tun0";
    int listen_port     = (argc > 1) ? atoi(argv[1]) : 9999;

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

    int opt = 1;
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(listen_port);
    bind_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("bind");
        return 1;
    }

    printf("Decapsulator: TUN=%s  UDP:%d\n", tun_dev, listen_port);

    uint8_t buf[MAX_PACKET];

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

        if (FD_ISSET(udp_fd, &rfds)) {
            struct sockaddr_in src_addr;
            socklen_t addrlen = sizeof(src_addr);
            ssize_t r = recvfrom(udp_fd, buf, sizeof(buf), 0,
                                 (struct sockaddr *)&src_addr, &addrlen);
            if (r < 0) {
                perror("recvfrom");
                continue;
            }
            if (r < HEADER_LEN) {
                fprintf(stderr, "Packet too short (%zd), discarding\n", r);
                continue;
            }

            struct custom_hdr *hdr = (struct custom_hdr *)buf;
            if (ntohl(hdr->magic) != MAGIC) {
                fprintf(stderr, "Bad magic 0x%08x, discarding\n", ntohl(hdr->magic));
                continue;
            }

            uint16_t payload_len = ntohs(hdr->payload_len);
            if (payload_len > (size_t)r - HEADER_LEN) {
                fprintf(stderr, "Payload len mismatch (%u > %zd), discarding\n",
                        payload_len, (size_t)r - HEADER_LEN);
                continue;
            }

            ssize_t w = write(tun_fd, buf + HEADER_LEN, payload_len);
            if (w < 0) {
                perror("write tun");
            } else {
                char src_str[INET_ADDRSTRLEN], dst_str[INET_ADDRSTRLEN];
                struct in_addr s, d;
                s.s_addr = hdr->src_tun_addr;
                d.s_addr = hdr->dst_tun_addr;
                printf("Decapsulated seq=%u %s -> %s (%u bytes inner)\n",
                       ntohl(hdr->seq_num),
                       inet_ntop(AF_INET, &s, src_str, sizeof(src_str)),
                       inet_ntop(AF_INET, &d, dst_str, sizeof(dst_str)),
                       payload_len);
            }
        }

        if (FD_ISSET(tun_fd, &rfds)) {
            // Future expansion: return traffic could be encapsulated here.
            uint8_t discard[MTU];
            if (read(tun_fd, discard, sizeof(discard)) < 0) {
                /* ignore errors on discard read */
            }
        }
    }

    close(tun_fd);
    close(udp_fd);
    return 0;
}
