#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H
#include "ctaps.h"
#include <uv.h>

typedef struct {
    uv_poll_t poll;  // Must be first — callers can cast to uv_poll_t*
    int fd;
    int32_t local_port;
} ct_udp_poll_handle_t;

// New callback type for poll-based recv
typedef void (*ct_udp_recv_cb)(uv_poll_t* handle, int fd, const uint8_t* buf,
                                ssize_t nread, const struct sockaddr* addr_from,
                                const struct sockaddr* addr_to);


uv_udp_t* create_udp_listening_on_local(const ct_local_endpoint_t* local_endpoint, uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb);

uv_udp_t* create_udp_listening_on_ephemeral(uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb);

int resolve_local_endpoint_from_poll(ct_udp_poll_handle_t* handle, ct_connection_t* connection);

int resolve_local_endpoint_from_handle(uv_handle_t* handle, ct_connection_t* connection);

bool ct_address_is_unspecified(const struct sockaddr_storage* addr);

void ct_get_addr_string(const struct sockaddr_storage* addr, char* buffer, size_t buffer_len, uint16_t* port);

int get_sockaddr_from_handle(const uv_handle_t* handle, struct sockaddr_storage* addr);

ct_udp_poll_handle_t* create_udp_poll_on_local(
    const ct_local_endpoint_t* local_endpoint);

#endif // SOCKET_UTILS_H
