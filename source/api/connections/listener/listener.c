#include "listener.h"
#include <connections/listener/socket_manager/socket_manager.h>
#include <logging/log.h>
#include <stdio.h>

void listener_close(const Listener* listener) {
  log_debug("Closing listener");
  listener->socket_manager->listener = NULL;
  socket_manager_decrement_ref(listener->socket_manager);
  if (listener->listener_callbacks.stopped) {
    log_debug("Invoking listener stopped callback");
    listener->listener_callbacks.stopped(listener, listener->listener_callbacks.user_data);
  }
  else {
    log_debug("No listener stopped callback registered");
  }
}

LocalEndpoint listener_get_local_endpoint(const Listener* listener) {
  return listener->local_endpoint;
}
