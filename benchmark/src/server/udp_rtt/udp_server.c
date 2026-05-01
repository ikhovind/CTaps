#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT       5000
#define BUF_SIZE   2048

static volatile sig_atomic_t keep_running = 1;

void handle_sigint(int sig) {
    (void)sig;
    keep_running = 0;
}

int main(void) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    signal(SIGINT, handle_sigint);
    printf("Echo server listening on 127.0.0.1:%d (Ctrl+C to stop)\n", PORT);

    char buf[BUF_SIZE];
    while (keep_running) {
        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);
        ssize_t r = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&src, &srclen);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }
        // Echo back
        ssize_t s = sendto(sock, buf, r, 0,
                           (struct sockaddr *)&src, srclen);
        if (s < 0) {
            perror("sendto");
            break;
        }
    }

    close(sock);
    return 0;
}
