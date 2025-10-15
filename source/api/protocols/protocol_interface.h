#ifndef PROTOCOL_INTERFACE_H
#define PROTOCOL_INTERFACE_H

#include <message/message_context/message_context.h>
#include <connections/connection/connection_callbacks.h>

#include "message/message.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct Listener;
struct Connection;
struct SocketManager;

typedef int (*ReceiveCallback)(struct Connection* connection,
                                Message** received_message,
                                void* user_data
                                );

typedef struct {
  ReceiveCallback receive_cb;
  void* user_data;
} ReceiveMessageRequest;

typedef struct ProtocolImplementation {
  const char* name;
  SelectionProperties selection_properties;
  int (*init)(struct Connection* connection, const ConnectionCallbacks* connection_callbacks);
  int (*send)(struct Connection*, Message*, MessageContext*);
  int (*receive)(struct Connection*, ReceiveMessageRequest receive_cb);
  int (*listen)(struct SocketManager* socket_manager);
  int (*stop_listen)(struct SocketManager*);
  int (*close)(const struct Connection*);
} ProtocolImplementation;

#endif  // PROTOCOL_INTERFACE_H
