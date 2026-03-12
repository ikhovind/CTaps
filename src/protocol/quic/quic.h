#ifndef QUIC_H
#define QUIC_H


#include "ctaps.h"
#include "ctaps_internal.h"
#include "protocol/common/socket_utils.h"
#include <picoquic.h>
#include <stdbool.h>
#include <uv.h>

struct ct_socket_manager_s;
struct ct_listener_s;

#define MAX_QUIC_PACKET_SIZE PICOQUIC_MAX_PACKET_SIZE

// Passed as a parameter to picoquic_create()
#define MAX_CONCURRENT_QUIC_CONNECTIONS 256

// Per-socket QUIC state
// Gotten through socket_manager internal state
// 1-1 with socket manager, so freed when socket manager is freed.
typedef struct ct_quic_socket_state_s {
  ct_udp_poll_handle_t* poll_handle;
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
                                          ct_message_t* initial_message
                                          );

void on_quic_poll_read(ct_socket_manager_t* socket_manager,
                       const uint8_t* buf, ssize_t nread,
                       const struct sockaddr* addr_from,
                       const struct sockaddr* addr_to);

void ct_quic_socket_state_free(ct_quic_socket_state_t* socket_state);
void ct_close_quic_context(ct_quic_socket_state_t* socket_state);

void ct_free_quic_connection_group_state(ct_connection_group_t* connection_group);

ct_quic_stream_state_t* ct_quic_stream_state_new(void);

/**
* @Brief Try to initialize a QUIC connection
*
* @Param connection The connection to initialize
*
* @Return non-zero on sync error, callback on ready or async error
*
* @note On non-zero return the connection can be freed immediately.
*/
int quic_init(ct_connection_t* connection);
int quic_init_with_send(ct_connection_t* connection, ct_message_t* initial_message, ct_message_context_t* initial_message_context);
int quic_close_connection(ct_connection_t*);
void quic_close_socket(ct_socket_manager_t*);
void quic_abort(ct_connection_t* connection);
int quic_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int quic_listen(struct ct_socket_manager_s* socket_manager);
void quic_close_listener(struct ct_socket_manager_s* socket_manager);
int quic_clone_connection(const struct ct_connection_s* source_connection, struct ct_connection_s* target_connection);
void quic_free_state(ct_connection_t* connection);
void quic_close_connection_group(ct_connection_group_t* connection_group);
int quic_set_connection_priority(ct_connection_t* connection, uint8_t priority);
void quic_free_socket_state(struct ct_socket_manager_s* socket_manager);

ct_quic_socket_state_t* ct_quic_context_ref(ct_quic_socket_state_t* context);
ct_quic_socket_state_t* ct_quic_context_deref(ct_quic_socket_state_t* context);

// Helper functions but also used in tests, so declared here
ct_quic_connection_group_state_t* ct_connection_get_quic_group_state(const ct_connection_t* connection);
ct_quic_stream_state_t* ct_connection_get_stream_state(const ct_connection_t* connection);
ct_quic_socket_state_t* ct_connection_get_quic_socket_state(const ct_connection_t* connection);

// Protocol interface (definition in quic.c)
extern const ct_protocol_impl_t quic_protocol_interface;

#endif //QUIC_H
