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

static int json_only_mode = 0;

static int connect_to_server(const char *host, int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Failed to create socket");
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

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to connect");
        close(sock_fd);
        return -1;
    }
    return sock_fd;
}

static int receive_file(int sock_fd, size_t expected_size, transfer_stats_t *stats) {
    unsigned char buffer[BUFFER_SIZE];
    size_t total_received = 0;

    timing_start(&stats->transfer_time);

    while (total_received < expected_size) {
        ssize_t received = recv(sock_fd, buffer, BUFFER_SIZE, 0);
        if (received < 0) {
            perror("Failed to receive data");
            return -1;
        }
        if (received == 0) {
            break;
        }
        total_received += received;
    }
    if (total_received < expected_size) {
        fprintf(stderr, "ERROR: Incomplete file received. Expected %zu bytes, got %zu bytes\n",
                expected_size, total_received);
        return -1;
    }

    timing_end(&stats->transfer_time);

    stats->bytes_received = total_received;

    return 0;
}

static int transfer_file(const char *host, int port, const char *request,
                         size_t expected_size, transfer_stats_t *stats) {
    timing_start(&stats->handshake_time);

    int sock_fd = connect_to_server(host, port);
    if (sock_fd < 0) {
        return -1;
    }

    timing_end(&stats->handshake_time);

    ssize_t sent = send(sock_fd, request, strlen(request), 0);
    if (sent < 0) {
        perror("Failed to send request");
        close(sock_fd);
        return -1;
    }

    if (receive_file(sock_fd, expected_size, stats) != 0) {
        close(sock_fd);
        return -1;
    }

    close(sock_fd);

    return 0;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = DEFAULT_PORT;
    int arg_idx = 1;

    if (argc > arg_idx) {
        host = argv[arg_idx];
        arg_idx++;
    }
    if (argc > arg_idx) {
        port = atoi(argv[arg_idx]);
        arg_idx++;
    }
    /* Parse --json flag */
    if (argc > arg_idx && strcmp(argv[arg_idx], "--json") == 0) {
        json_only_mode = 1;
        arg_idx++;
    }

    if (!json_only_mode) printf("TCP Client connecting to %s:%d\n", host, port);

    transfer_stats_t large_stats, short_stats;

    if (!json_only_mode) printf("\n--- Transferring LARGE file ---\n");
    if (transfer_file(host, port, REQUEST_LARGE, LARGE_FILE_SIZE, &large_stats) != 0) {
        fprintf(stderr, "ERROR: Failed to transfer large file\n");
        return -1;
    }

    if (!json_only_mode) printf("\n--- Transferring SHORT file ---\n");
    if (transfer_file(host, port, REQUEST_SHORT, SHORT_FILE_SIZE, &short_stats) != 0) {
        fprintf(stderr, "ERROR: Failed to transfer short file\n");
        return -1;
    }

    char *json = get_json_stats(TRANSFER_MODE_TCP_NATIVE, &large_stats, &short_stats, 0);
    if (json) {
        printf("%s\n", json);
        free(json);
    }

    return 0;
}
