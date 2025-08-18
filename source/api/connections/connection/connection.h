//
// Created by ikhovind on 12.08.25.
//

#ifndef CONNECTION_H
#define CONNECTION_H

#include "transport_properties/transport_properties.h"
#include "message/message.h"

typedef struct {
    TransportProperties transport_properties;
} Connection;

void send_message(Connection* connection, Message* message);
void receive_message(Connection* connection, Message* message);
void connection_close (Connection* connection);
#endif //CONNECTION_H
