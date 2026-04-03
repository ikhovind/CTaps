#include "../common/protocol.h"
#include "../common/timing.h"
#include "../common/file_generator.h"
#include "../common/benchmark_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define RECV_TIMEOUT_MS 3000

/* Return codes for transfer functions */
#define TRANSFER_OK      0
#define TRANSFER_ERR    -1
#define TRANSFER_TIMEOUT -2

int json_only_mode = 0;

static int connect_to_server(const char* host, int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Failed to create socket");
        return -1;
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Failed to set SO_RCVTIMEO");
        close(sock_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock_fd);
        return -1;
    }

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to connect");
        close(sock_fd);
        return -1;
    }
    return sock_fd;
}

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    int port = DEFAULT_PORT;
    int arg_idx = 1;

    if (argc > arg_idx) { host = argv[arg_idx]; arg_idx++; }
    if (argc > arg_idx) { port = atoi(argv[arg_idx]); arg_idx++; }
    if (argc > arg_idx && strcmp(argv[arg_idx], "--json") == 0) {
        json_only_mode = 1;
        arg_idx++;
    }

    if (!json_only_mode) {
        printf("TCP Client connecting to %s:%d\n", host, port);
    }

    transfer_stats_t* large_stats = transfer_stats_new();
    transfer_stats_t* short_stats = transfer_stats_new();
    int timed_out = 0;

    if (!json_only_mode) printf("\n--- Transferring LARGE file ---\n");

    timing_start(&large_stats->handshake_time);

    int sock_fd = connect_to_server(host, port);
    if (sock_fd < 0) {
        return TRANSFER_ERR;
    }

    timing_end(&large_stats->handshake_time);

    char* json = get_json_stats(TRANSFER_MODE_TCP_NATIVE, large_stats, short_stats);
    if (json) {
        printf("%s\n", json);
        free(json);
    }

    transfer_stats_free(large_stats);
    transfer_stats_free(short_stats);
    return timed_out ? 1 : 0;
}
