#ifndef PROTOCOL_INTERFACE_H
#define PROTOCOL_INTERFACE_H

#include <message/message_context/message_context.h>
#include <connections/connection/connection_callbacks.h>
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
  int (*receive)(struct Connection*, ReceiveCallbacks receive_callbacks);
  int (*listen)(struct SocketManager* socket_manager);
  int (*stop_listen)(struct SocketManager*);
  int (*close)(const struct Connection*);
  int (*remote_endpoint_from_peer)(uv_handle_t* peer, RemoteEndpoint* resolved_peer);
} ProtocolImplementation;

#endif  // PROTOCOL_INTERFACE_H
