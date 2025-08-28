#ifndef PROTOCOL_INTERFACE_H
#define PROTOCOL_INTERFACE_H

#include "message/message.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct Listener;
struct Connection;

typedef struct {
  SelectionPreference values[SELECTION_PROPERTY_END];
} ProtocolFeatures;

typedef int (*ReceiveCallback)(struct Connection* connection,
                                Message** received_message,
                                void* user_data
                                );

typedef struct {
  ReceiveCallback receive_cb;
  void* user_data;
} ReceiveMessageRequest;

typedef int (*InitDoneCallback)(struct Connection* connection, void* user_data);

typedef struct {
  InitDoneCallback init_done_callback;
  void* user_data;
} InitDoneCb;

typedef struct ProtocolImplementation {
  const char* name;
  ProtocolFeatures features;
  int (*init)(struct Connection* connection, InitDoneCb init_done_cb);
  int (*send)(struct Connection*, Message*);
  int (*receive)(struct Connection*, ReceiveMessageRequest receive_cb);
  int (*listen)(struct Listener*);
  int (*close)(const struct Connection*);
} ProtocolImplementation;

#endif  // PROTOCOL_INTERFACE_H
