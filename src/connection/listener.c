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
  return listener;
}

ct_local_endpoint_t ct_listener_get_local_endpoint(const ct_listener_t* listener) {
  return listener->local_endpoint;
}

void ct_listener_free(ct_listener_t* listener) {
  if (!listener) {
    return;
  }
  free(listener);
}
