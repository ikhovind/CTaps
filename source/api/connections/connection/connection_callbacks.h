//
// Created by ikhovind on 15.10.25.
//

#ifndef CONNECTION_CALLBACKS_H
#define CONNECTION_CALLBACKS_H

#include "message/message.h"
#include "message/message_context/message_context.h"

//forward declaration of ct_connection_t
struct ct_connection_t;

// TODO - justify double message pointer
typedef struct ct_receive_callbacks_t {
  int (*receive_callback)(struct ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* ctx, void* user_data);
  int (*receive_error)(struct ct_connection_t* connection, ct_message_context_t* ctx, const char* reason, void* user_data);
  int (*receive_partial)(struct ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* ctx, bool end_of_message, void* user_data);
  void* user_data;
} ct_receive_callbacks_t;

typedef struct ct_connection_callbacks_t {
  int (*connection_error)(struct ct_connection_t* connection, void* udata);
  int (*establishment_error)(struct ct_connection_t* connection, void* udata);
  int (*expired)(struct ct_connection_t* connection, void* udata);
  int (*path_change)(struct ct_connection_t* connection, void* udata);
  int (*ready)(struct ct_connection_t* connection, void* udata);
  int (*send_error)(struct ct_connection_t* connection, void* udata);
  int (*sent)(struct ct_connection_t* connection, void* udata);
  int (*soft_error)(struct ct_connection_t* connection, void* udata);
  void* user_data;
} ct_connection_callbacks_t;

#endif //CONNECTION_CALLBACKS_H
