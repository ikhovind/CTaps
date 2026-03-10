#include "ctaps.h"
#include <connection/socket_manager/socket_manager.h>
#include "candidate_gathering/candidate_gathering.h"
#include <logging/log.h>
#include <stdio.h>

void ct_listener_close(ct_listener_t* listener) {
  int rc = ct_socket_manager_listener_stop(listener->socket_manager); 
  if (rc) {
    log_error("Error in stopping listener: %d", rc);
  }
}

ct_listener_t* ct_listener_new(void) {
  ct_listener_t* listener = malloc(sizeof(ct_listener_t));
  if (!listener) {
    log_error("Could not allocate memory for ct_listener_t: %s", strerror(errno));
    return NULL;
  }
  memset(listener, 0, sizeof(ct_listener_t));
  listener->state = CT_LISTENER_STATE_ESTABLISHING;
  return listener;
}

ct_local_endpoint_t ct_listener_get_local_endpoint(const ct_listener_t* listener) {
  return listener->local_endpoint;
}

void ct_listener_free(ct_listener_t* listener) {
  log_trace("Freeing ct_listener_t %p", (void*)listener);
  if (!listener) {
    return;
  }
  if (listener->socket_manager) {
    ct_socket_manager_unref(listener->socket_manager);
    listener->socket_manager = NULL;
  }
  ct_local_endpoint_free_content(&listener->local_endpoint);
  free(listener);
}

bool ct_listener_is_closed(const ct_listener_t* listener) {
  if (!listener) {
    log_warn("NULL listener parameter for ct_listener_is_closed");
    return true;
  }
  return listener->state == CT_LISTENER_STATE_CLOSED;
}
