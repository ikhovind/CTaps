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

static int receive_file(int sock_fd, size_t expected_size, transfer_stats_t* stats) {
    unsigned char buffer[BUFFER_SIZE];
    size_t total_received = 0;

    int idle_ms = 0;
    timing_start(&stats->transfer_time);
    while (total_received < expected_size) {
        ssize_t received = recv(sock_fd, buffer, BUFFER_SIZE, 0);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                time_received_chunk(stats, 0);  // emit zero point
                idle_ms += 100;
                if (idle_ms >= RECV_TIMEOUT_MS) {
                    return TRANSFER_TIMEOUT;
                }
                continue;
            }
            perror("Failed to receive data");
            return TRANSFER_ERR;
        }
        if (received == 0) {
            break;
        }
        total_received += received;
        time_received_chunk(stats, received);
    }

    timing_end(&stats->transfer_time);
    stats->bytes_received = total_received;

    if (total_received < expected_size) {
        fprintf(stderr, "ERROR: Incomplete file received. Expected %zu bytes, got %zu bytes\n",
                expected_size, total_received);
        return TRANSFER_ERR;
    }

    return TRANSFER_OK;
}

static int transfer_file(const char* host, int port, const char* request, size_t expected_size,
                         transfer_stats_t* stats) {
    timing_start(&stats->handshake_time);

    int sock_fd = connect_to_server(host, port);
    if (sock_fd < 0) {
        return TRANSFER_ERR;
    }

    timing_end(&stats->handshake_time);

    ssize_t sent = send(sock_fd, request, strlen(request), 0);
    if (sent < 0) {
        perror("Failed to send request");
        close(sock_fd);
        return TRANSFER_ERR;
    }

    int rc = receive_file(sock_fd, expected_size, stats);
    close(sock_fd);
    return rc;
}

/* Inject a boolean field before the closing brace of a JSON object string. */
static char* json_inject_bool(const char* json, const char* key, int value) {
    size_t len = strlen(json);
    /* Find the last '}' */
    if (len == 0 || json[len - 1] != '}') {
        return NULL;
    }
    char* out = NULL;
    if (asprintf(&out, "%.*s,\"%s\":%s}", (int)(len - 1), json,
                 key, value ? "true" : "false") == -1) {
        return NULL;
    }
    return out;
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

    int rc = transfer_file(host, port, REQUEST_LARGE, LARGE_FILE_SIZE, large_stats);
    if (rc == TRANSFER_ERR) {
        fprintf(stderr, "ERROR: Failed to transfer large file\n");
        transfer_stats_free(large_stats);
        transfer_stats_free(short_stats);
        printf("ERROR\n");
        return 1;
    }
    if (rc == TRANSFER_TIMEOUT) {
        timed_out = 1;
        /* Skip short file transfer — path is dead */
        goto output;
    }

    if (!json_only_mode) printf("\n--- Transferring SHORT file ---\n");

    rc = transfer_file(host, port, REQUEST_SHORT, SHORT_FILE_SIZE, short_stats);
    if (rc == TRANSFER_ERR) {
        fprintf(stderr, "ERROR: Failed to transfer short file\n");
        transfer_stats_free(large_stats);
        transfer_stats_free(short_stats);
        printf("ERROR\n");
        return 1;
    }
    if (rc == TRANSFER_TIMEOUT) {
        timed_out = 1;
    }

output:;
    char* json = get_json_stats(TRANSFER_MODE_TCP_NATIVE, large_stats, short_stats);
    if (json) {
        char* final_json = json_inject_bool(json, "timed_out", timed_out);
        free(json);
        if (final_json) {
            printf("%s\n", final_json);
            free(final_json);
        }
    }

    transfer_stats_free(large_stats);
    transfer_stats_free(short_stats);
    return timed_out ? 1 : 0;
}
