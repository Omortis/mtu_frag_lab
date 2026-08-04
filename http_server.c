#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LISTEN_ADDR "10.200.0.2"
#define LISTEN_PORT 8080
#define MAX_REQUEST 4096

static const char *http_body =
    "<html>\n"
    "<head><title>SD-WAN Lab</title></head>\n"
    "<body>\n"
    "<h1>Hello from VM-B TUN side</h1>\n"
    "<p>TCP + HTTP survived the custom tunnel.</p>\n"
    "</body>\n"
    "</html>\n";

int main(int argc, char **argv)
{
    const char *addr_str = (argc > 1) ? argv[1] : LISTEN_ADDR;
    int port             = (argc > 2) ? atoi(argv[2]) : LISTEN_PORT;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, addr_str, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid bind address: %s\n", addr_str);
        return 1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(fd, 1) < 0) {
        perror("listen");
        return 1;
    }

    printf("HTTP server listening on %s:%d\n", addr_str, port);

    while (1) {
        struct sockaddr_in client;
        socklen_t addrlen = sizeof(client);
        int cfd = accept(fd, (struct sockaddr *)&client, &addrlen);
        if (cfd < 0) {
            perror("accept");
            continue;
        }

        char client_str[INET_ADDRSTRLEN];
        printf("Connection from %s:%d\n",
               inet_ntop(AF_INET, &client.sin_addr, client_str, sizeof(client_str)),
               ntohs(client.sin_port));

        char request[MAX_REQUEST];
        memset(request, 0, sizeof(request));
        int total = 0;
        while (total < MAX_REQUEST - 1) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(cfd, &rfds);
            struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
            int n = select(cfd + 1, &rfds, NULL, NULL, &tv);
            if (n <= 0) break;

            int r = read(cfd, request + total, MAX_REQUEST - 1 - total);
            if (r <= 0) break;
            total += r;
            if (strstr(request, "\r\n\r\n")) break;
        }

        char response[1024];
        int len = snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(http_body), http_body);

        if (write(cfd, response, len) < 0) {
            /* ignore short write errors on HTTP response */
        }
        close(cfd);
    }

    close(fd);
    return 0;
}
