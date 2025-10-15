//
// Created by ikhovind on 15.10.25.
//

#ifndef CONNECTION_CALLBACKS_H
#define CONNECTION_CALLBACKS_H

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
