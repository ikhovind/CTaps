#ifndef CONNECTION_H
#define CONNECTION_H

#include <glib.h>
#include <uv.h>

#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"
#include "message/message.h"
#include "transport_properties/transport_properties.h"

struct ProtocolImplementation;

typedef struct Connection {
  TransportProperties transport_properties;
  LocalEndpoint local_endpoint;
  RemoteEndpoint remote_endpoint;
  // TODO - decide on if this has to be a pointer
  ProtocolImplementation protocol;
  uv_udp_t udp_handle;
  // TODO this is shared state and should be locked
  // Queue for pending receive() calls that arrived before the data
  GQueue* received_callbacks;
  GQueue* received_messages;
} Connection;

int send_message(Connection* connection, Message* message);
int receive_message(Connection* connection,
                    int (*receive_msg_cb)(Connection* connection,
                                          Message** received_message));
void connection_close(Connection* connection);
#endif  // CONNECTION_H
