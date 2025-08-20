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

void on_send(uv_udp_send_t *req, int status) {
    printf("on_send\n");
    if (status) {
        fprintf(stderr, "Send error: %s\n", uv_strerror(status));
    }
    if (req) {
        free(req); // Free the send request
    }
}

int udp_init() {
    uv_udp_init(ctaps_event_loop, &send_socket);
}

int udp_close() {
    close(udp_fd);
}

void register_udp_support() {
    register_protocol(&udp_protocol_interface);
}


int udp_send(struct Connection* connection, Message* message) {
    struct sockaddr_in dest_addr;
    uv_ip4_addr("127.0.0.1", 4001, &dest_addr);

    char msg[] = "pingpong6";
    uv_buf_t buffer = uv_buf_init(msg, strlen(msg));

    uv_udp_send_t *send_req = malloc(sizeof(uv_udp_send_t));
    if (!send_req) {
        fprintf(stderr, "Failed to allocate send request\n");
        return 1;
    }

    printf("Queueing UDP message to be sent...\n");
    uv_udp_send(send_req, &send_socket, &buffer, 1, (const struct sockaddr*)&connection->remote_endpoint, on_send);

    return 0;
}

int udp_receive(struct Connection* connection, Message* message) {
    return 0;
}
