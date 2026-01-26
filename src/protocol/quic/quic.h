#ifndef QUIC_H
#define QUIC_H


#include "ctaps.h"
#include <picoquic.h>
#include <uv.h>
#include <stdbool.h>

struct ct_socket_manager_s;
struct ct_listener_s;

// Passed as a parameter to picoquic_create()
#define MAX_CONCURRENT_QUIC_CONNECTIONS 256

// Per-context QUIC state (one per listener or client connection group)
// Holds a picoquic_quic_t context with its own timer and certificates
typedef struct ct_quic_context_s {
  picoquic_quic_t* picoquic_ctx;
  uv_timer_t* timer_handle;
  struct ct_listener_s* listener;      // NULL for client connections
  uint32_t num_active_connections;
  char* cert_file_name;
  char* key_file_name;
  char* ticket_store_path;             // Path for 0-RTT session ticket persistence
  ct_message_t* initial_message;          // For freeing when a client connection is done
  ct_message_context_t* initial_message_context; // For freeing when a client connection is done
} ct_quic_context_t;

// Per-stream state for individual connections
typedef struct ct_quic_stream_state_t {
  uint64_t stream_id;
  bool stream_initialized;
  bool attempted_early_data;
} ct_quic_stream_state_t;

// Shared state across all streams in a QUIC connection group
typedef struct ct_quic_group_state_s {
  uv_udp_t* udp_handle;
  picoquic_cnx_t* picoquic_connection;
  ct_quic_context_t* quic_context;     // Reference to per-listener/client context
} ct_quic_group_state_t;

// QUIC context management
ct_quic_context_t* ct_create_quic_context(const char* cert_file, 
                                          const char* key_file, 
                                          struct ct_listener_s* listener, 
                                          const ct_security_parameters_t* security_parameters,
                                          ct_message_t* initial_message,
                                          ct_message_context_t* initial_message_context
                                          );
void ct_close_quic_context(ct_quic_context_t* ctx);

int quic_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int quic_init_with_send(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks, ct_message_t* initial_message, ct_message_context_t* initial_message_context);
int quic_close(ct_connection_t* connection);
void quic_abort(ct_connection_t* connection);
int quic_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int quic_listen(struct ct_socket_manager_s* socket_manager);
int quic_stop_listen(struct ct_socket_manager_s* socket_manager);
int quic_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
void quic_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection);
int quic_clone_connection(const struct ct_connection_s* source_connection, struct ct_connection_s* target_connection);

// Protocol interface (definition in quic.c)
extern const ct_protocol_impl_t quic_protocol_interface;

#endif //QUIC_H
