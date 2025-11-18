#ifndef CONNECTION_H
#define CONNECTION_H

#include <glib.h>
#include <uv.h>

#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"
#include "message/message.h"
#include "message/message_context/message_context.h"
#include "transport_properties/transport_properties.h"
#include "security_parameters/security_parameters.h"

typedef enum {
  CONNECTION_TYPE_STANDALONE = 0,
  CONNECTION_OPEN_TYPE_MULTIPLEXED,
} ct_connection_type_t;

typedef struct ct_connection_t {
  ct_transport_properties_t transport_properties;
  const ct_security_parameters_t* security_parameters;
  ct_local_endpoint_t local_endpoint;
  ct_remote_endpoint_t remote_endpoint;
  // TODO - decide on if this has to be a pointer
  ct_protocol_implementation_t protocol;
  void* protocol_state;
  ct_connection_type_t open_type;
  ct_connection_callbacks_t connection_callbacks;
  struct ct_socket_manager_t* socket_manager;
  // TODO this is shared state and should be locked
  // Queue for pending receive() calls that arrived before the data
  GQueue* received_callbacks;
  GQueue* received_messages;
} ct_connection_t;

int ct_send_message(ct_connection_t* connection, ct_message_t* message);
int ct_send_message_full(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* message_context);
int ct_receive_message(ct_connection_t* connection,
                    ct_receive_callbacks_t receive_callbacks);
void ct_connection_build_multiplexed(ct_connection_t* connection, const struct ct_listener_t* listener, const ct_remote_endpoint_t* remote_endpoint);
ct_connection_t* ct_connection_build_from_received_handle(const struct ct_listener_t* listener, uv_stream_t* received_handle);
void ct_connection_build(ct_connection_t* connection);
void ct_connection_free(ct_connection_t* connection);
void ct_connection_close(ct_connection_t* connection);
#endif  // CONNECTION_H
