#ifndef QUIC_H
#define QUIC_H


#include "ctaps.h"
#include "ctaps_internal.h"
#include <picoquic.h>
#include <stdbool.h>
#include <uv.h>

struct ct_socket_manager_s;
struct ct_listener_s;

// Passed as a parameter to picoquic_create()
#define MAX_CONCURRENT_QUIC_CONNECTIONS 256

// Per-socket QUIC state
// Gotten through socket_manager internal state
// 1-1 with socket manager, so freed when socket manager is freed.
typedef struct ct_quic_socket_state_s {
  uv_udp_t* udp_handle;
  picoquic_quic_t* picoquic_ctx;
  uv_timer_t* timer_handle;
  struct ct_socket_manager_s* socket_manager;  // Back-pointer to owner
  char* cert_file_name;
  char* key_file_name;
  char* ticket_store_path;             // Path for 0-RTT session ticket persistence
  ct_message_t* initial_message;          // For freeing when a client connection is done
  ct_message_context_t* initial_message_context; // For freeing when a client connection is done
} ct_quic_socket_state_t;

// Shared state across all streams in a QUIC connection group
// Gotten through connection group internal state
typedef struct ct_connection_quic_group_state_s {
  picoquic_cnx_t* picoquic_connection;
  bool attempted_early_data;
  bool close_initiated;
} ct_quic_connection_group_state_t;

// Per QUIC stream. This maps to a ct_connection_t in a connection group
// Gotten through connection internal state
typedef struct ct_quic_stream_state_s {
  uint64_t stream_id;
  bool stream_initialized;
} ct_quic_stream_state_t;


// QUIC context management
ct_quic_socket_state_t* ct_quic_socket_state_new(const char* cert_file, 
                                          const char* key_file, 
                                          ct_socket_manager_t* socket_manager,
                                          const ct_security_parameters_t* security_parameters,
                                          ct_message_t* initial_message,
                                          ct_message_context_t* initial_message_context
                                          );
void ct_close_quic_context(ct_quic_socket_state_t* ctx);
void ct_free_quic_connection_group_state(ct_quic_connection_group_state_t* group_state);

ct_quic_stream_state_t* ct_quic_stream_state_new(void);

int quic_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int quic_init_with_send(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks, ct_message_t* initial_message, ct_message_context_t* initial_message_context);
int quic_close(ct_connection_t*, void(*on_connection_close)(ct_connection_t* connection));
int quic_close_socket(ct_socket_manager_t*);
void quic_abort(ct_connection_t* connection);
int quic_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int quic_listen(struct ct_socket_manager_s* socket_manager);
int quic_stop_listen(struct ct_socket_manager_s* socket_manager);
int quic_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
int quic_clone_connection(const struct ct_connection_s* source_connection, struct ct_connection_s* target_connection);
int quic_free_state(ct_connection_t* connection);

ct_quic_socket_state_t* ct_quic_context_ref(ct_quic_socket_state_t* context);
ct_quic_socket_state_t* ct_quic_context_deref(ct_quic_socket_state_t* context);

// Helper functions but also used in tests, so declared here
ct_quic_connection_group_state_t* ct_connection_get_quic_group_state(const ct_connection_t* connection);
ct_quic_stream_state_t* ct_connection_get_stream_state(const ct_connection_t* connection);
ct_quic_socket_state_t* ct_connection_get_quic_socket_state(const ct_connection_t* connection);

// Protocol interface (definition in quic.c)
extern const ct_protocol_impl_t quic_protocol_interface;

#endif //QUIC_H
