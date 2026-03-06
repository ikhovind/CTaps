#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define TCP_IP      "127.0.0.1"
#define TCP_PORT    5006
#define BUFFER_SIZE 1024

int main(void) {
    int server_fd, conn_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    /* Allow immediate reuse of the port after restart */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(TCP_PORT);
    if (inet_pton(AF_INET, TCP_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("TCP server listening on %s:%d\n", TCP_IP, TCP_PORT);

    while (1) {
        conn_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (conn_fd < 0) {
            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);
        printf("Connection from ('%s', %d)\n", client_ip, client_port);

        unsigned char buf[BUFFER_SIZE];
        ssize_t n;

        while ((n = recv(conn_fd, buf, sizeof(buf), 0)) > 0) {
            printf("Received %zd bytes from ('%s', %d)\n", n, client_ip, client_port);

            /* Build response: "Pong: " + received data */
            const char *prefix      = "Pong: ";
            size_t      prefix_len  = strlen(prefix);
            size_t      resp_len    = prefix_len + (size_t)n;
            unsigned char *response = malloc(resp_len);
            if (!response) {
                perror("malloc");
                break;
            }
            memcpy(response, prefix, prefix_len);
            memcpy(response + prefix_len, buf, (size_t)n);

            if (send(conn_fd, response, resp_len, 0) < 0) {
                perror("send");
                free(response);
                break;
            }
            printf("Sent response with %zu bytes to ('%s', %d)\n",
                   resp_len, client_ip, client_port);
            free(response);
        }

        if (n < 0) {
            perror("recv");
        }

        close(conn_fd);
        printf("Connection from ('%s', %d) closed\n", client_ip, client_port);
    }

    close(server_fd);
    return EXIT_SUCCESS;
}
