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
uv_udp_t send_socket;

void udp_recv_cb() {

}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    // We'll use a static buffer for this simple example, but in a real
    // application, you would likely use malloc or a buffer pool.
    static char slab[65536];
    *buf = uv_buf_init(slab, sizeof(slab));
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

void on_read(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
    printf("on_read\n");
    if (nread < 0) {
        fprintf(stderr, "Read error: %s\n", uv_err_name(nread));
        uv_close((uv_handle_t*) req, NULL);
        free(buf->base);
        return;
    }

    if (addr == NULL) {
        // No more data to read, or an empty packet.
        return;
    }

    // Get the sender's address and port
    char sender[17] = { 0 };
    uv_ip4_name((const struct sockaddr_in*) addr, sender, 16);
    int port = ntohs(((const struct sockaddr_in*) addr)->sin_port);

    printf("Received message from %s:%d\n", sender, port);

    // Print the received data (nread holds the number of bytes received)
    printf("Data: %.*s\n", (int)nread, buf->base);

    // Stop receiving so the event loop can exit
    uv_udp_recv_stop(req);
}


int udp_init(Connection *connection) {
    uv_udp_init(ctaps_event_loop, &send_socket);

    if (connection->local_endpoint.initialized) {
        // TODO - set local endpoint in test check that this works
        uv_udp_bind(&send_socket, (const struct sockaddr *)&connection->local_endpoint.addr.ipv4_addr, 0);
        printf("Listening for UDP packets on port %d...\n", ntohs(connection->local_endpoint.addr.ipv4_addr.sin_port));
    }
    else {
        struct sockaddr_in bind_addr;
        uv_ip4_addr("0.0.0.0", 0, &bind_addr);

        uv_udp_bind(&send_socket, (const struct sockaddr *)&bind_addr, 0);
        printf("Listening for UDP packets on port %d...\n", ntohs(bind_addr.sin_port));
    }

    uv_udp_recv_start(&send_socket, alloc_buffer, on_read);
}

int udp_close() {
    close(udp_fd);
}

void register_udp_support() {
    register_protocol(&udp_protocol_interface);
}


int udp_send(Connection* connection, Message* message) {
    const uv_buf_t buffer = uv_buf_init(message->content, strlen(message->content));

    uv_udp_send_t *send_req = malloc(sizeof(uv_udp_send_t));
    if (!send_req) {
        fprintf(stderr, "Failed to allocate send request\n");
        return 1;
    }

    uv_udp_send(send_req, &send_socket, &buffer, 1, (const struct sockaddr*)&connection->remote_endpoint.addr.ipv4_addr, on_send);

    return 0;
}

int udp_receive(struct Connection* connection, Message* message) {
    return 0;
}
