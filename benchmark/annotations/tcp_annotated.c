I #include "../common/protocol.h"
I #include "../common/timing.h"
I #include "../common/file_generator.h"
I #include "../common/benchmark_stats.h"
I #include <stdio.h>
I #include <stdlib.h>
I #include <string.h>
I #include <unistd.h>
I #include <sys/socket.h>
I #include <netinet/in.h>
I #include <arpa/inet.h>
I #include <errno.h>
-
I #define RECV_TIMEOUT_MS 3000
-
- /* Return codes for transfer functions */
I #define TRANSFER_OK      0
I #define TRANSFER_ERR    -1
I #define TRANSFER_TIMEOUT -2
-
I int json_only_mode = 0;
-
C static int connect_to_server(const char* host, int port) {
C     int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
E     if (sock_fd < 0) {
E         perror("Failed to create socket");
E         return -1;
E     }
-
C     struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
E     if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
E         perror("Failed to set SO_RCVTIMEO");
E         close(sock_fd);
E         return -1;
E     }
-
C     struct sockaddr_in server_addr;
C     memset(&server_addr, 0, sizeof(server_addr));
C     server_addr.sin_family = AF_INET;
C     server_addr.sin_port = htons(port);
-
E     if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
E         perror("Invalid address");
E         close(sock_fd);
E         return -1;
E     }
-
C     if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
E         perror("Failed to connect");
E         close(sock_fd);
E         return -1;
C     }
C     return sock_fd;
C }
-
D static int receive_file(int sock_fd, size_t expected_size, transfer_stats_t* stats) {
D     unsigned char buffer[BUFFER_SIZE];
D     size_t total_received = 0;
-
E     int idle_ms = 0;
D     timing_start(&stats->transfer_time);
D     while (total_received < expected_size) {
D         ssize_t received = recv(sock_fd, buffer, BUFFER_SIZE, 0);
E         if (received < 0) {
E             if (errno == EAGAIN || errno == EWOULDBLOCK) {
D                 time_received_chunk(stats, 0);  // emit zero point
E                 idle_ms += 100;
E                 if (idle_ms >= RECV_TIMEOUT_MS) {
E                     return TRANSFER_TIMEOUT;
E                 }
E                 continue;
E             }
E             perror("Failed to receive data");
E             return TRANSFER_ERR;
E         }
E         if (received == 0) {
E             break;
E         }
D         total_received += received;
D         time_received_chunk(stats, received);
D     }
-
D     timing_end(&stats->transfer_time);
D     stats->bytes_received = total_received;
-
E     if (total_received < expected_size) {
E         fprintf(stderr, "ERROR: Incomplete file received. Expected %zu bytes, got %zu bytes\n",
E                 expected_size, total_received);
E         return TRANSFER_ERR;
E     }
-
D     return TRANSFER_OK;
D }
-
C static int transfer_file(const char* host, int port, const char* request, size_t expected_size,
C                          transfer_stats_t* stats) {
C     timing_start(&stats->handshake_time);
-
C     int sock_fd = connect_to_server(host, port);
E     if (sock_fd < 0) {
E         return TRANSFER_ERR;
E     }
-
C     timing_end(&stats->handshake_time);
-
D     ssize_t sent = send(sock_fd, request, strlen(request), 0);
E     if (sent < 0) {
E         perror("Failed to send request");
E         close(sock_fd);
E         return TRANSFER_ERR;
E     }
-
D     int rc = receive_file(sock_fd, expected_size, stats);
T     close(sock_fd);
D     return rc;
D }
-
I int main(int argc, char* argv[]) {
I     const char* host = "127.0.0.1";
I     int port = DEFAULT_PORT;
I     int arg_idx = 1;
-
I     if (argc > arg_idx) { host = argv[arg_idx]; arg_idx++; }
I     if (argc > arg_idx) { port = atoi(argv[arg_idx]); arg_idx++; }
I     if (argc > arg_idx && strcmp(argv[arg_idx], "--json") == 0) {
I         json_only_mode = 1;
I         arg_idx++;
I     }
-
I     if (!json_only_mode) {
I         printf("TCP Client connecting to %s:%d\n", host, port);
I     }
-
I     transfer_stats_t* large_stats = transfer_stats_new();
I     transfer_stats_t* short_stats = transfer_stats_new();
I     int timed_out = 0;
-
D     if (!json_only_mode) printf("\n--- Transferring LARGE file ---\n");
-
D     int rc = transfer_file(host, port, REQUEST_LARGE, LARGE_FILE_SIZE, large_stats);
E     if (rc == TRANSFER_ERR) {
E         fprintf(stderr, "ERROR: Failed to transfer large file\n");
T         transfer_stats_free(large_stats);
T         transfer_stats_free(short_stats);
E         printf("ERROR\n");
E         return 1;
E     }
E     if (rc == TRANSFER_TIMEOUT) {
E         timed_out = 1;
E         /* Skip short file transfer — path is dead */
E         goto output;
E     }
-
D     if (!json_only_mode) printf("\n--- Transferring SHORT file ---\n");
-
D     rc = transfer_file(host, port, REQUEST_SHORT, SHORT_FILE_SIZE, short_stats);
E     if (rc == TRANSFER_ERR) {
E         fprintf(stderr, "ERROR: Failed to transfer short file\n");
T         transfer_stats_free(large_stats);
T         transfer_stats_free(short_stats);
E         printf("ERROR\n");
E         return 1;
E     }
E     if (rc == TRANSFER_TIMEOUT) {
E         timed_out = 1;
E     }
-
T output:;
T     char* json = get_json_stats(TRANSFER_MODE_TCP_NATIVE, large_stats, short_stats);
T     if (json) {
T         printf("%s\n", json);
T         free(json);
T     }
-
T     transfer_stats_free(large_stats);
T     transfer_stats_free(short_stats);
T     return timed_out ? 1 : 0;
T }
