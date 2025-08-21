#include "connection.h"
#include "message/message.h"
#include "protocols/protocol_interface.h"

int send_message(Connection* connection, Message* message) {
    return connection->protocol.send(connection, message);
}

Message* receive_message(Connection* connection) {
    return connection->protocol.receive(connection);
}
void connection_close(Connection* connection) {
}
