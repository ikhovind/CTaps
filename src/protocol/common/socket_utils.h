#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H
#include "ctaps.h"
#include <uv.h>

uv_udp_t* create_udp_listening_on_local(ct_local_endpoint_t* local_endpoint, uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb);

uv_udp_t* create_udp_listening_on_ephemeral(uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb);

int resolve_local_endpoint_from_handle(uv_handle_t* handle, ct_connection_t* connection);
#endif // SOCKET_UTILS_H
