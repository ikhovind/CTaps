#include "udp.h"

#include <ctaps.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <stdlib.h>
#include <uv.h>
#include <connections/connection/connection.h>

#include "protocols/registry/protocol_registry.h"

#define BUFFER_SIZE 1024
#define MAX_EVENTS 1024

struct epoll_event event;
struct epoll_event events[MAX_EVENTS];
char buffer[BUFFER_SIZE];
int udp_fd;
uv_udp_t recv_socket;
uv_udp_t send_socket;

void udp_recv_cb() {

}

int udp_init() {
    // Initialize and bind receive socket
    uv_udp_init(&ctaps_event_loop, &recv_socket);
    struct sockaddr_in recv_addr;
    uv_ip4_addr("0.0.0.0", 4002, &recv_addr);
    uv_udp_bind(&recv_socket, (const struct sockaddr *)&recv_addr, UV_UDP_REUSEADDR);
    uv_udp_recv_start(&recv_socket, buffer, udp_recv_cb);

    // Initialize send socket (can use port 0 for ephemeral port)
    uv_udp_init(&ctaps_event_loop, &send_socket);
    uv_udp_bind(&send_socket, (const struct sockaddr *)uv_ip4_addr("0.0.0.0", 0, &recv_addr), 0);

    printf("UDP server listening on port 4002...\n");
}

int udp_close() {
    close(udp_fd);
}

void register_udp_support() {
    register_protocol(&udp_protocol_interface);
}

void on_send(uv_udp_send_t *req, int status) {
    if (status) {
        fprintf(stderr, "Send error: %s\n", uv_strerror(status));
    }
    if (req) {
        free(req); // Free the send request
    }
}

int udp_send(struct Connection* connection, Message* message) {
    printf("trying to send udp");
    uv_buf_t buf = uv_buf_init("hello world", strlen("hello world"));
    struct sockaddr_in dest_addr;
    uv_ip4_addr("0.0.0.0", 4001, &dest_addr);

    uv_udp_send_t *send_req = malloc(sizeof(uv_udp_send_t));
    if (!send_req) {
        fprintf(stderr, "Failed to allocate send request\n");
        return 1;
    }

    uv_udp_send(send_req, &send_socket, &buf, 1, (const struct sockaddr *)&dest_addr, on_send);

    return 0;
}

int udp_receive(struct Connection* connection, Message* message) {
    return 0;
}
