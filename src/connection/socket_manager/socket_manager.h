#ifndef SOCKET_MANAGER_H
#define SOCKET_MANAGER_H
#include <uv.h>
#include "ctaps.h"
#include "ctaps_internal.h"
#include <glib.h>

int socket_manager_remove_connection_group(ct_socket_manager_t* socket_manager, const struct sockaddr_storage* remote_addr);

int socket_manager_build(ct_socket_manager_t* socket_manager, struct ct_listener_s* listener);

int socket_manager_decrement_ref(ct_socket_manager_t* socket_manager);

void socket_manager_increment_ref(ct_socket_manager_t* socket_manager);

void socket_manager_free(ct_socket_manager_t* socket_manager);

ct_socket_manager_t* ct_socket_manager_ref(ct_socket_manager_t* socket_manager);

void ct_socket_manager_unref(ct_socket_manager_t* socket_manager);

void new_stream_connection_cb(uv_stream_t *server, int status);

ct_connection_group_t* socket_manager_get_connection_group(ct_socket_manager_t* socket_manager, const struct sockaddr_storage* remote_addr);

int socket_manager_insert_connection_group(ct_socket_manager_t* socket_manager, const ct_remote_endpoint_t* remote, ct_connection_group_t* connection_group);

ct_socket_manager_t* ct_socket_manager_new(const ct_protocol_impl_t* protocol_impl, ct_listener_t* listener);

#endif //SOCKET_MANAGER_H
