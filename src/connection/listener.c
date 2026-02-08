#include "ctaps.h"
#include <connection/socket_manager/socket_manager.h>
#include "candidate_gathering/candidate_gathering.h"
#include <logging/log.h>
#include <stdio.h>

void ct_listener_close(ct_listener_t* listener) {
  log_debug("Closing listener");
  listener->socket_manager->listener = NULL;
  socket_manager_decrement_ref(listener->socket_manager);
  if (listener->listener_callbacks.stopped) {
    log_debug("Invoking listener stopped callback");
    listener->listener_callbacks.stopped(listener);
  }
  else {
    log_debug("No listener stopped callback registered");
  }
}

ct_listener_t* ct_listener_new() {
  ct_listener_t* listener = malloc(sizeof(ct_listener_t));
  if (listener == NULL) {
    log_error("Could not allocate memory for ct_listener_t: %s", strerror(errno));
    return NULL;
  }
  memset(listener, 0, sizeof(ct_listener_t));
  return listener;
}

ct_local_endpoint_t ct_listener_get_local_endpoint(const ct_listener_t* listener) {
  return listener->local_endpoint;
}
