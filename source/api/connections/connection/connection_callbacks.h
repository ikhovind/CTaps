//
// Created by ikhovind on 15.10.25.
//

#ifndef CONNECTION_CALLBACKS_H
#define CONNECTION_CALLBACKS_H

#include "message/message.h"
#include "message/message_context/message_context.h"

typedef struct ReceiveCallbacks {
  int (*receive_callback)(struct Connection* connection, Message** received_message, MessageContext* ctx, void* user_data);
  int (*receive_error)(struct Connection* connection, MessageContext* ctx, const char* reason, void* user_data);
  int (*receive_partial)(struct Connection* connection, Message** received_message, MessageContext* ctx, bool end_of_message, void* user_data);
  void* user_data;
} ReceiveCallbacks;

typedef struct ConnectionCallbacks {
  int (*connection_error)(struct Connection* connection, void* udata);
  int (*establishment_error)(struct Connection* connection, void* udata);
  int (*expired)(struct Connection* connection, void* udata);
  int (*path_change)(struct Connection* connection, void* udata);
  int (*ready)(struct Connection* connection, void* udata);
  int (*send_error)(struct Connection* connection, void* udata);
  int (*sent)(struct Connection* connection, void* udata);
  int (*soft_error)(struct Connection* connection, void* udata);
  void* user_data;
} ConnectionCallbacks;

#endif //CONNECTION_CALLBACKS_H
