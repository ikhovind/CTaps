#include "ctaps.h"
#include <connection/socket_manager/socket_manager.h>
#include "transport_property/transport_properties.h"
#include "security_parameter/security_parameters.h"
#include "endpoint/local_endpoint.h"
#include "candidate_gathering/candidate_gathering.h"
#include <logging/log.h>
#include <stdio.h>

void ct_listener_close(ct_listener_t* listener) {
  int rc = ct_socket_manager_listener_close(listener->socket_manager); 
  if (rc) {
    log_error("Error in stopping listener: %d", rc);
  }
}

ct_listener_t* ct_listener_new(
  const ct_transport_properties_t* transport_properties,
  const ct_local_endpoint_t* local_endpoint,
  const ct_listener_callbacks_t* listener_callbacks,
  const ct_connection_callbacks_t* connection_callbacks,
  const ct_security_parameters_t* security_parameters,
  const ct_protocol_impl_t* protocol_impl
) {
  if (!local_endpoint || !protocol_impl) {
    log_error("Local endpoint and protocol are required to create a listener");
    return NULL;
  }
  ct_listener_t* listener = calloc(1, sizeof(ct_listener_t));
  if (!listener) {
    log_error("Could not allocate memory for ct_listener_t: %s", strerror(errno));
    return NULL;
  }

  listener->state = CT_LISTENER_STATE_ESTABLISHING;
  if (transport_properties) {
    listener->transport_properties = ct_transport_properties_deep_copy(transport_properties);
    if (!listener->transport_properties) {
      log_error("Failed to deep copy transport properties for listener");
      free(listener);
      return NULL;
    }
  }
  listener->local_endpoint = ct_local_endpoint_deep_copy(local_endpoint);
  if (!listener->local_endpoint) {
    log_error("Failed to deep copy local endpoint for listener");
    ct_listener_free(listener);
    return NULL;
  }
  listener->num_local_endpoints = 1;
  if (listener_callbacks) {
    listener->listener_callbacks = *listener_callbacks;
  }
  if (connection_callbacks) {
    listener->connection_callbacks = *connection_callbacks;
  }
  if (security_parameters) {
    listener->security_parameters = ct_security_parameters_deep_copy(security_parameters);
    if (!listener->security_parameters) {
      log_error("Failed to deep copy security parameters for listener");
      ct_listener_free(listener);
      return NULL;
    }
  }
  if (listener_callbacks) {
    listener->listener_callbacks = *listener_callbacks;
  }
  ct_socket_manager_t* socket_manager = ct_socket_manager_new(protocol_impl, listener);
  if (!socket_manager) {
    log_error("Failed to create socket manager for listener");
    ct_listener_free(listener);
    return NULL;
  }

  listener->socket_manager = ct_socket_manager_ref(socket_manager);

  return listener;
}

const ct_local_endpoint_t* ct_listener_get_local_endpoint(const ct_listener_t* listener) {
  return listener->local_endpoint;
}

void ct_listener_free(ct_listener_t* listener) {
  log_trace("Freeing ct_listener_t %p", (void*)listener);
  if (!listener) {
    return;
  }

  if (listener->socket_manager) {
    ct_socket_manager_t* socket_manager = listener->socket_manager;
    if (socket_manager->listener) {
      socket_manager->listener = NULL;
    }
    ct_socket_manager_unref(listener->socket_manager);
    listener->socket_manager = NULL;
  }
  ct_local_endpoint_free(listener->local_endpoint);
  ct_transport_properties_free(listener->transport_properties);
  ct_security_parameters_free(listener->security_parameters);
  free(listener);
}

bool ct_listener_is_closed(const ct_listener_t* listener) {
  if (!listener) {
    log_warn("NULL listener parameter for ct_listener_is_closed");
    return true;
  }
  return listener->state == CT_LISTENER_STATE_CLOSED;
}

void ct_listener_mark_as_closed(ct_listener_t* listener) {
  if (!listener) {
    log_warn("NULL listener parameter for ct_listener_mark_as_closed");
    return;
  }
  listener->state = CT_LISTENER_STATE_CLOSED;
}
