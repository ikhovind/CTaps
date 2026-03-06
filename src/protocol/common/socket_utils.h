#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H
#include "ctaps.h"
#include <uv.h>

uv_udp_t* create_udp_listening_on_local(const ct_local_endpoint_t* local_endpoint, uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb);

uv_udp_t* create_udp_listening_on_ephemeral(uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb);

int resolve_local_endpoint_from_handle(uv_handle_t* handle, ct_connection_t* connection);

bool ct_address_is_unspecified(const struct sockaddr_storage* addr);

void ct_get_addr_string(const struct sockaddr_storage* addr, char* buffer, size_t buffer_len, uint16_t* port);

int get_sockaddr_from_handle(const uv_handle_t* handle, struct sockaddr_storage* addr);

#endif // SOCKET_UTILS_H
