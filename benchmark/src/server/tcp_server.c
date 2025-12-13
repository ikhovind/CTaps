#include "../common/protocol.h"
#include "../common/file_generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#define LARGE_FILE_PATH "large_file.dat"
#define SHORT_FILE_PATH "short_file.dat"

typedef struct {
    int client_fd;
    int connection_id;
} client_context_t;

static int send_file(int client_fd, const char *filepath, size_t file_size) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        perror("Failed to open file");
        return -1;
    }

    unsigned char buffer[BUFFER_SIZE];
    size_t total_sent = 0;

    while (total_sent < file_size) {
        size_t to_read = (file_size - total_sent < BUFFER_SIZE) ?
                         (file_size - total_sent) : BUFFER_SIZE;

        size_t read_bytes = fread(buffer, 1, to_read, fp);
        if (read_bytes == 0) {
            if (feof(fp)) {
                break;
            }
            perror("Failed to read file");
            fclose(fp);
            return -1;
        }

        ssize_t sent = send(client_fd, buffer, read_bytes, 0);
        if (sent < 0) {
            perror("Failed to send data");
            fclose(fp);
            return -1;
        }

        total_sent += sent;
    }

    fclose(fp);
    printf("Sent %zu bytes from %s\n", total_sent, filepath);
    return 0;
}

static void *handle_client(void *arg) {
    client_context_t *ctx = (client_context_t *)arg;
    int client_fd = ctx->client_fd;
    int conn_id = ctx->connection_id;
    free(ctx);

    printf("[Connection %d] Client connected\n", conn_id);

    /* Set MSS to 1460 bytes */
    int mss = 1460;
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_MAXSEG, &mss, sizeof(mss)) < 0) {
        perror("[Warning] Failed to set TCP_MAXSEG");
    }
    else {
        printf("[Connection %d] Set TCP MSS to %d bytes\n", conn_id, mss);
    }

    /* Set generous timeouts for network emulation (60 seconds) */
    struct timeval timeout;
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;

    if (setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("[Warning] Failed to set SO_SNDTIMEO");
    }
    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("[Warning] Failed to set SO_RCVTIMEO");
    }

    char request[16];
    memset(request, 0, sizeof(request));

    ssize_t received = recv(client_fd, request, sizeof(request) - 1, 0);
    if (received <= 0) {
        perror("Failed to receive request");
        close(client_fd);
        return NULL;
    }

    request[received] = '\0';
    printf("[Connection %d] Received request: %s", conn_id, request);

    if (strncmp(request, REQUEST_LARGE, strlen(REQUEST_LARGE)) == 0) {
        printf("[Connection %d] Sending LARGE file\n", conn_id);
        send_file(client_fd, LARGE_FILE_PATH, LARGE_FILE_SIZE);
    } else if (strncmp(request, REQUEST_SHORT, strlen(REQUEST_SHORT)) == 0) {
        printf("[Connection %d] Sending SHORT file\n", conn_id);
        send_file(client_fd, SHORT_FILE_PATH, SHORT_FILE_SIZE);
    } else {
        fprintf(stderr, "[Connection %d] Invalid request: %s", conn_id, request);
    }

    close(client_fd);
    printf("[Connection %d] Connection closed\n", conn_id);

    return NULL;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("TCP Server starting on port %d\n", port);

    if (access(LARGE_FILE_PATH, F_OK) != 0) {
        printf("Generating large file...\n");
        if (generate_test_file(LARGE_FILE_PATH, LARGE_FILE_SIZE) != 0) {
            fprintf(stderr, "Failed to generate large file\n");
            return 1;
        }
    }

    if (access(SHORT_FILE_PATH, F_OK) != 0) {
        printf("Generating short file...\n");
        if (generate_test_file(SHORT_FILE_PATH, SHORT_FILE_SIZE) != 0) {
            fprintf(stderr, "Failed to generate short file\n");
            return 1;
        }
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Failed to create socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Failed to set SO_REUSEADDR");
        close(server_fd);
        return 1;
    }

    /* Set MSS to 1460 bytes on listening socket (inherited by accepted connections) */
    int mss = 1460;
    if (setsockopt(server_fd, IPPROTO_TCP, TCP_MAXSEG, &mss, sizeof(mss)) < 0) {
        perror("Failed to set TCP_MAXSEG on listening socket");
        close(server_fd);
        return 1;
    }
    printf("Set TCP MSS to %d bytes on listening socket\n", mss);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Failed to listen");
        close(server_fd);
        return 1;
    }

    printf("Server listening on port %d\n", port);

    int connection_id = 0;

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            perror("Failed to accept connection");
            continue;
        }

        client_context_t *ctx = malloc(sizeof(client_context_t));
        ctx->client_fd = client_fd;
        ctx->connection_id = ++connection_id;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, ctx) != 0) {
            perror("Failed to create thread");
            close(client_fd);
            free(ctx);
            continue;
        }

        pthread_detach(thread);
    }

    close(server_fd);
    return 0;
}
