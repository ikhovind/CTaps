#ifndef PROTOCOL_INTERFACE_H
#define PROTOCOL_INTERFACE_H

#include <connections/connection/connection_callbacks.h>
#include <message/message_context/message_context.h>
#include <sys/socket.h>
#include <uv.h>

#include "endpoints/remote/remote_endpoint.h"
#include "message/message.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct ct_listener_t;
struct ct_connection_t;
struct ct_socket_manager_t;

typedef struct ct_protocol_implementation_t {
  const char* name;
  ct_selection_properties_t selection_properties;
  int (*init)(struct ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
  int (*send)(struct ct_connection_t*, ct_message_t*, ct_message_context_t*);
  int (*listen)(struct ct_socket_manager_t* socket_manager);
  int (*stop_listen)(struct ct_socket_manager_t*);
  int (*close)(const struct ct_connection_t*);
  int (*remote_endpoint_from_peer)(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
  // Optional: Update internal protocol handles/state to reference a new connection
  // Called when protocol state is transferred from one connection to another (e.g., during candidate racing)
  // from_connection: The connection whose protocol_state contains the handles
  // to_connection: The connection that protocol callbacks/handles should now point to
  void (*retarget_protocol_connection)(struct ct_connection_t* from_connection, struct ct_connection_t* to_connection);
} ct_protocol_implementation_t;

#endif  // PROTOCOL_INTERFACE_H
