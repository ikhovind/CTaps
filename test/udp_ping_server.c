#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define UDP_IP      "127.0.0.1"
#define UDP_PORT    5005
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int sock_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(UDP_PORT);
    if (inet_pton(AF_INET, UDP_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    int ready_fd = atoi(argv[1]);
    write(ready_fd, "1", 1);
    close(ready_fd);

    printf("UDP server listening on %s:%d\n", UDP_IP, UDP_PORT);

    unsigned char buf[BUFFER_SIZE];

    while (1) {
        ssize_t n = recvfrom(sock_fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&client_addr, &client_addr_len);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        printf("Received %zd bytes from ('%s', %d)\n", n, client_ip, client_port);

        /* Build response: "Pong: " + received data */
        const char *prefix     = "Pong: ";
        size_t      prefix_len = strlen(prefix);
        size_t      resp_len   = prefix_len + (size_t)n;
        unsigned char *response = malloc(resp_len);
        if (!response) {
            perror("malloc");
            continue;
        }
        memcpy(response, prefix, prefix_len);
        memcpy(response + prefix_len, buf, (size_t)n);

        if (sendto(sock_fd, response, resp_len, 0,
                   (struct sockaddr *)&client_addr, client_addr_len) < 0) {
            perror("sendto");
        } else {
            printf("Sent response with %zu bytes to ('%s', %d)\n",
                   resp_len, client_ip, client_port);
        }

        free(response);
    }

    close(sock_fd);
    return EXIT_SUCCESS;
}
