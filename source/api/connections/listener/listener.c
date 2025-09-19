#include "listener.h"
#include "socket_manager/socket_manager.h"
#include <stdio.h>

void listener_close(Listener* listener) {
  printf("Closing listener pointer: %p\n", listener);
  listener->socket_manager->listener = NULL;
  socket_manager_decrement_ref(listener->socket_manager);
}
