#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT        5000
#define MSG_SIZE    64
#define N_ITER      100000

static uint64_t timespec_to_ns(const struct timespec *ts) {
    return (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
}

int main(int argc, char** argv) {
    if (argc >= 2) {
        printf("Usage: %s [out_directory]\n", argv[0]);
    }
    const char* filename = "rtt_baseline_ns.txt";
    char out_file[512];

    if (argc == 2) {
        snprintf(out_file, sizeof(out_file), "%s/%s", argv[1], filename);
    } else {
        snprintf(out_file, sizeof(out_file), "%s", filename);
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_port        = htons(PORT);
    srv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    char sendbuf[MSG_SIZE];
    char recvbuf[MSG_SIZE];

    memset(sendbuf, 'A', sizeof(sendbuf));

    FILE *f = fopen(out_file, "w");
    if (!f) {
        perror("fopen");
        close(sock);
        return 1;
    }

    struct timespec t_start, t_end;

    for (int i = 0; i < 1000; ++i) {
        ssize_t s = send(sock, sendbuf, MSG_SIZE, 0);
        if (s < 0) {
            perror("send (warmup)");
            fclose(f);
            close(sock);
            return 1;
        }
        ssize_t r = recv(sock, recvbuf, sizeof(recvbuf), 0);
        if (r < 0) {
            perror("recv (warmup)");
            fclose(f);
            close(sock);
            return 1;
        }
    }

    printf("Starting RTT measurements: %d iterations, msg size %d bytes\n",
           N_ITER, MSG_SIZE);

    for (int i = 0; i < N_ITER; ++i) {
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_start) != 0) {
            perror("clock_gettime start");
            fclose(f);
            close(sock);
            return 1;
        }

        ssize_t s = send(sock, sendbuf, MSG_SIZE, 0);
        if (s < 0) {
            perror("send");
            fclose(f);
            close(sock);
            return 1;
        }

        ssize_t r = recv(sock, recvbuf, sizeof(recvbuf), 0);
        if (r < 0) {
            perror("recv");
            fclose(f);
            close(sock);
            return 1;
        }

        if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_end) != 0) {
            perror("clock_gettime end");
            fclose(f);
            close(sock);
            return 1;
        }

        uint64_t dt_ns = timespec_to_ns(&t_end) - timespec_to_ns(&t_start);
        // Write RTT in nanoseconds
        fprintf(f, "%llu\n", (unsigned long long)dt_ns);
    }

    fclose(f);
    close(sock);

    printf("Done. RTT samples written to %s\n", out_file);
    return 0;
}
