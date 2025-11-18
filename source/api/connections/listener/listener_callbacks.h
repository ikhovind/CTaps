#ifndef LISTENER_CALLBACKS_H
#define LISTENER_CALLBACKS_H

typedef struct ct_listener_callbacks_t {
  int (*connection_received)(struct ct_listener_t* listener, struct ct_connection_t* new_conn, void* udata);
  int (*establishment_error)(struct ct_listener_t* listener, const char* reason, void* udata);
  int (*stopped)(struct ct_listener_t* listener, void* udata);
  void* user_data;
} ct_listener_callbacks_t;

#endif //LISTENER_CALLBACKS_H
