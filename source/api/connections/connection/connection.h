#ifndef CONNECTION_H
#define CONNECTION_H

#include <glib.h>
#include <uv.h>

#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"
#include "message/message.h"
#include "transport_properties/transport_properties.h"

typedef enum {
  CONNECTION_OPEN_TYPE_ACTIVE = 0,
  CONNECTION_OPEN_TYPE_PASSIVE,
} ConnectionOpenType;


typedef struct Connection {
  TransportProperties transport_properties;
  LocalEndpoint local_endpoint;
  RemoteEndpoint remote_endpoint;
  // TODO - decide on if this has to be a pointer
  ProtocolImplementation protocol;
  uv_udp_t udp_handle;
  ConnectionOpenType open_type;
  struct SocketManager* socket_manager;
  // TODO this is shared state and should be locked
  // Queue for pending receive() calls that arrived before the data
  GQueue* received_callbacks;
  GQueue* received_messages;
} Connection;

int send_message(Connection* connection, Message* message);
int receive_message(Connection* connection,
                    ReceiveMessageRequest receive_message_cb);
void connection_build_from_listener(Connection* connection, struct Listener* listener, RemoteEndpoint* remote_endpoint);
void connection_close(Connection* connection);
#endif  // CONNECTION_H
