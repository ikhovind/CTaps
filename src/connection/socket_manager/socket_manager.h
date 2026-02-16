#ifndef SOCKET_MANAGER_H
#define SOCKET_MANAGER_H
#include <uv.h>
#include "ctaps.h"
#include "ctaps_internal.h"
#include <glib.h>

ct_socket_manager_t* ct_socket_manager_ref(ct_socket_manager_t* socket_manager);

void socket_manager_free(ct_socket_manager_t* socket_manager);

void ct_socket_manager_unref(ct_socket_manager_t* socket_manager);

void new_stream_connection_cb(uv_stream_t *server, int status);

ct_connection_t* socket_manager_get_from_demux_table(ct_socket_manager_t* socket_manager, const struct sockaddr_storage* remote_addr);

int socket_manager_insert_connection(ct_socket_manager_t* socket_manager, const ct_remote_endpoint_t* remote, ct_connection_t* connection);

ct_socket_manager_t* ct_socket_manager_new(const ct_protocol_impl_t* protocol_impl, ct_listener_t* listener);

int ct_socket_manager_get_num_open_connections(const ct_socket_manager_t* socket_manager);

void ct_socket_manager_close(ct_socket_manager_t* socket_manager);

int ct_socket_manager_close_connection(ct_socket_manager_t*, ct_connection_t*);

int ct_socket_manager_listener_stop(ct_socket_manager_t* socket_manager);

#endif //SOCKET_MANAGER_H
