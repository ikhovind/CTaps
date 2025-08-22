#include "connection.h"

#include "message/message.h"
#include "protocols/protocol_interface.h"

int send_message(Connection* connection, Message* message) {
  return connection->protocol.send(connection, message);
}

int receive_message(Connection* connection,
                    int (*receive_msg_cb)(Connection* connection,
                                          Message** received_message)) {
  return connection->protocol.receive(connection, receive_msg_cb);
}
void connection_close(Connection* connection) {
  connection->protocol.close(connection);
}
