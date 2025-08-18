#include "connection.h"
#include "message/message.h"
#include "protocols/protocol_interface.h"

int send_message(Connection* connection, Message* message) {
    return connection->protocol->send(connection, message);
}
void receive_message(Connection* connection, Message* message) {
    connection->protocol->receive(connection, message);
}
void connection_close(Connection* connection) {
}
