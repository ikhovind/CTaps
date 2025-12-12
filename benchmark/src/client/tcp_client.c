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

    /* Set generous timeouts for network emulation (60 seconds) */
    struct timeval timeout;
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;

    if (setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("[Warning] Failed to set SO_SNDTIMEO");
    }
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("[Warning] Failed to set SO_RCVTIMEO");
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

    timing_end(&stats->transfer_time);

    stats->bytes_received = total_received;

    return 0;
}

static int transfer_file(const char *host, int port, const char *request,
                         size_t expected_size, transfer_stats_t *stats) {
    memset(stats, 0, sizeof(transfer_stats_t));

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

static void print_stats(const char *file_type, const transfer_stats_t *stats) {
    if (json_only_mode) return;

    double duration_sec = timing_get_duration_ms(&stats->transfer_time) / 1000.0;
    double throughput_mbps = 0.0;
    if (duration_sec > 0) {
        throughput_mbps = (stats->bytes_received * 8.0) / (duration_sec * 1000000.0);
    }

    printf("\n=== %s File Transfer Stats ===\n", file_type);
    printf("Handshake time: %.2f ms\n", timing_get_duration_ms(&stats->handshake_time));
    printf("Transfer time: %.2f ms\n", timing_get_duration_ms(&stats->transfer_time));
    printf("Bytes received: %zu\n", stats->bytes_received);
    printf("Throughput: %.2f Mbps\n", throughput_mbps);
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
    uint64_t large_end_time;

    if (!json_only_mode) printf("\n--- Transferring LARGE file ---\n");
    if (transfer_file(host, port, REQUEST_LARGE, LARGE_FILE_SIZE, &large_stats) != 0) {
        if (json_only_mode) {
            printf("ERROR\n");
        } else {
            fprintf(stderr, "Failed to transfer large file\n");
        }
        return 1;
    }
    large_end_time = timing_get_timestamp_us();
    print_stats("LARGE", &large_stats);

    if (!json_only_mode) printf("\n--- Transferring SHORT file ---\n");
    if (transfer_file(host, port, REQUEST_SHORT, SHORT_FILE_SIZE, &short_stats) != 0) {
        if (json_only_mode) {
            printf("ERROR\n");
        } else {
            fprintf(stderr, "Failed to transfer short file\n");
        }
        return 1;
    }
    print_stats("SHORT", &short_stats);

    char *json = get_json_stats(TRANSFER_MODE_TCP_NATIVE, &large_stats, &short_stats, 0);
    if (json) {
        if (json_only_mode) {
            printf("%s\n", json);
        } else {
            printf("\n%s\n", json);
        }
        free(json);
    }

    return 0;
}
