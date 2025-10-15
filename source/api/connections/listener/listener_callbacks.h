#ifndef LISTENER_CALLBACKS_H
#define LISTENER_CALLBACKS_H

typedef struct ListenerCallbacks {
  int (*connection_received)(struct Listener* listener, struct Connection* new_conn, void* udata);
  int (*establishment_error)(struct Listener* listener, const char* reason, void* udata);
  int (*stopped)(struct Listener* listener, void* udata);
  void* user_data;
} ListenerCallbacks;

#endif //LISTENER_CALLBACKS_H
