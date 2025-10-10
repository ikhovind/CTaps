#include "listener.h"
#include "socket_manager/socket_manager.h"
#include <logging/log.h>
#include <stdio.h>

void listener_close(const Listener* listener) {
  log_debug("Closing listener");
  listener->socket_manager->listener = NULL;
  socket_manager_decrement_ref(listener->socket_manager);
}
