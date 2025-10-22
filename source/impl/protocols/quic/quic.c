#include "quic.h"


int quic_init(Connection* connection, const ConnectionCallbacks* connection_callbacks) {
  return -ENOSYS;
}
int quic_close(const Connection* connection) {
  return -ENOSYS;
}
int quic_send(Connection* connection, Message* message, MessageContext*) {
  return -ENOSYS;
}
int quic_receive(Connection* connection, ReceiveCallbacks receive_callbacks) {
  return -ENOSYS;
}
int quic_listen(struct SocketManager* socket_manager) {
  return -ENOSYS;
}
int quic_stop_listen(struct SocketManager* listener) {
  return -ENOSYS;
}
int quic_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer) {
  return -ENOSYS;
}
