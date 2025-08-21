#ifndef CONNECTION_H
#define CONNECTION_H


#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"

#include "transport_properties/transport_properties.h"
#include "message/message.h"

struct ProtocolImplementation;

typedef struct Connection {
    TransportProperties transport_properties;
    LocalEndpoint local_endpoint;
    RemoteEndpoint remote_endpoint;
    // TODO - decide on if this has to be a pointer
    ProtocolImplementation protocol;
} Connection;

int send_message(Connection* connection, Message* message);
Message* receive_message(Connection* connection);
void connection_close (Connection* connection);
#endif //CONNECTION_H
