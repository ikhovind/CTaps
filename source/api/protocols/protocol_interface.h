#ifndef PROTOCOL_INTERFACE_H
#define PROTOCOL_INTERFACE_H

#include <connections/connection/connection_callbacks.h>
#include <message/message_context/message_context.h>
#include <sys/socket.h>
#include <uv.h>

#include "endpoints/remote/remote_endpoint.h"
#include "message/message.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct Listener;
struct Connection;
struct SocketManager;

typedef struct ProtocolImplementation {
  const char* name;
  SelectionProperties selection_properties;
  int (*init)(struct Connection* connection, const ConnectionCallbacks* connection_callbacks);
  int (*send)(struct Connection*, Message*, MessageContext*);
  int (*listen)(struct SocketManager* socket_manager);
  int (*stop_listen)(struct SocketManager*);
  int (*close)(const struct Connection*);
  int (*remote_endpoint_from_peer)(uv_handle_t* peer, RemoteEndpoint* resolved_peer);
  // Optional: Update internal protocol handles/state to reference a new connection
  // Called when protocol state is transferred from one connection to another (e.g., during candidate racing)
  // from_connection: The connection whose protocol_state contains the handles
  // to_connection: The connection that protocol callbacks/handles should now point to
  void (*retarget_protocol_connection)(struct Connection* from_connection, struct Connection* to_connection);
} ProtocolImplementation;

#endif  // PROTOCOL_INTERFACE_H
