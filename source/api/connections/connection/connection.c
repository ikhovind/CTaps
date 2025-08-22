#include "connection.h"

#include "message/message.h"
#include "protocols/protocol_interface.h"

int send_message(Connection* connection, Message* message) {
  return connection->protocol.send(connection, message);
}

int receive_message(Connection* connection,
                    ReceiveMessageCb receive_message_cb
                    ) {
  return connection->protocol.receive(connection, receive_message_cb);
}
void connection_close(Connection* connection) {
  connection->protocol.close(connection);
}
