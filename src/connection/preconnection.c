
#include "connection/socket_manager/socket_manager.h"
#include "connection/listener.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include "message/message.h"
#include "message/message_context.h"
#include "transport_property/selection_properties/selection_properties.h"
#include <candidate_gathering/candidate_gathering.h>
#include <candidate_gathering/candidate_racing.h>
#include <endpoint/local_endpoint.h>
#include <endpoint/remote_endpoint.h>
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include <security_parameter/security_parameters.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define INT_TO_STRING(x) #x

#define MAX_PORT_STR_LENGTH sizeof(INT_TO_STRING(UINT16_MAX))

int copy_remote_endpoints(ct_preconnection_t* preconnection,
                          const ct_remote_endpoint_t* remote_endpoints,
                          const size_t num_remote_endpoints) {
  preconnection->num_remote_endpoints = num_remote_endpoints;
  preconnection->remote_endpoints = malloc(num_remote_endpoints * sizeof(ct_remote_endpoint_t));
  if (!preconnection->remote_endpoints) {
    log_error("Could not allocate memory for remote endpoints: %s");
    return errno;
  }
  // Deep copy each remote endpoint (copies all strings)
  for (size_t i = 0; i < num_remote_endpoints; i++) {
    int rc = ct_remote_endpoint_copy_content(&remote_endpoints[i], &preconnection->remote_endpoints[i]);
    if (rc != 0) {
      log_error("Failed to copy remote endpoint content for index %zu: %s", i, strerror(-rc));
      // Free any previously copied remote endpoints
      for (size_t j = 0; j < i; j++) {
        ct_remote_endpoint_free_strings(&preconnection->remote_endpoints[j]);
      }
      free(preconnection->remote_endpoints);
      preconnection->remote_endpoints = NULL;
      preconnection->num_remote_endpoints = 0;
      return rc;
    }
  }
  return 0;
}

ct_preconnection_t* ct_preconnection_new(
    const ct_remote_endpoint_t* remote_endpoints,
    const size_t num_remote_endpoints,
    const ct_transport_properties_t* transport_properties,
    const ct_security_parameters_t* security_parameters) {

  ct_preconnection_t* precon = malloc(sizeof(ct_preconnection_t));
  if (!precon) {
    log_error("Failed to allocate memory for preconnection");
    return NULL;
  }

  memset(precon, 0, sizeof(ct_preconnection_t));

  if (transport_properties) {
    log_debug("Copying transport properties into preconnection");
    ct_selection_properties_deep_copy(&precon->transport_properties.selection_properties,
                                     &transport_properties->selection_properties);
    precon->transport_properties.connection_properties = transport_properties->connection_properties;
  } else {
    log_debug("No transport properties provided, initializing with defaults");
    // Initialize with default values
    memcpy(&precon->transport_properties.selection_properties, &DEFAULT_SELECTION_PROPERTIES, sizeof(ct_selection_properties_t));
    memcpy(&precon->transport_properties.connection_properties, &DEFAULT_CONNECTION_PROPERTIES, sizeof(ct_connection_properties_t));
  }

  // Deep copy security parameters so preconnection owns its own copy
  precon->security_parameters = ct_security_parameters_deep_copy(security_parameters);
  ct_local_endpoint_build(&precon->local);

  // Copy remote endpoints if provided
  if (remote_endpoints && num_remote_endpoints > 0) {
    int ret = copy_remote_endpoints(precon, remote_endpoints, num_remote_endpoints);
    if (ret != 0) {
      free(precon);
      return NULL;
    }
  }

  return precon;
}

typedef struct listener_candidate_node_array_ready_context_s {
  const ct_preconnection_t* preconnection;
  ct_listener_callbacks_t listener_callbacks;
  ct_listener_t* listener;
} listener_candidate_node_array_ready_context_t;

void listener_candidate_node_array_ready_cb(GArray* candidate_nodes, void* context) {
  log_info("Candidate gathering complete for listener, processing candidate nodes");
  listener_candidate_node_array_ready_context_t* listener_candidate_node_array_ready_context = (listener_candidate_node_array_ready_context_t*)context;
  const ct_preconnection_t* preconnection = listener_candidate_node_array_ready_context->preconnection;
  ct_listener_callbacks_t listener_callbacks = listener_candidate_node_array_ready_context->listener_callbacks;
  ct_listener_t* listener = listener_candidate_node_array_ready_context->listener;
  if (candidate_nodes && candidate_nodes->len > 0) {
    const ct_candidate_node_t first_node = g_array_index(candidate_nodes, ct_candidate_node_t, 0);


    ct_socket_manager_t* socket_manager = ct_socket_manager_new(first_node.protocol_candidate->protocol_impl, listener);
    if (!socket_manager) {
      log_error("Failed to create socket manager for listener");
      free_candidate_array(candidate_nodes);
      free(listener_candidate_node_array_ready_context);
      if (listener->listener_callbacks.establishment_error) {
        listener->listener_callbacks.establishment_error(listener, -ENOMEM);
      }
      return;
    }
    log_debug("listening on port: %d with protocol: %s", first_node.local_endpoint->port, socket_manager->protocol_impl->name);
    *listener = (ct_listener_t){
      .listener_callbacks = listener_callbacks,
      .local_endpoint = *first_node.local_endpoint,
      .num_local_endpoints = 1,
      .socket_manager = ct_socket_manager_ref(socket_manager),
      .state = CT_LISTENER_STATE_LISTENING,
      .transport_properties = preconnection->transport_properties,
      .security_parameters = preconnection->security_parameters,
    };

    log_info("Starting to listen on ct_listener_t using protocol: %s on port: %d", socket_manager->protocol_impl->name, listener->local_endpoint.port);
    int rc = socket_manager->protocol_impl->listen(socket_manager);
    if (rc != 0) {
      log_error("Failed to listen on socket manager for listener: %d", rc);
      ct_socket_manager_unref(socket_manager);
      if (listener->listener_callbacks.establishment_error) {
        listener->listener_callbacks.establishment_error(listener, rc);
      }
      return;
    }
    if (listener->listener_callbacks.listener_ready) {
      log_debug("Calling listener_ready callback for listener");
      listener->listener_callbacks.listener_ready(listener);
    }
    else {
      log_debug("No listener_ready callback set for listener");
    }
  }
  else {
    log_error("No candidate node for ct_listener_t found");
    if (listener->listener_callbacks.establishment_error) {
      listener->listener_callbacks.establishment_error(listener, -ENOENT);
    }
  }
  free_candidate_array(candidate_nodes);
  free(listener_candidate_node_array_ready_context);
}

ct_listener_t* ct_preconnection_listen(const ct_preconnection_t* preconnection, ct_listener_callbacks_t listener_callbacks) {
  log_info("Listening from preconnection");
  listener_candidate_node_array_ready_context_t* cb_context = calloc(1, sizeof(listener_candidate_node_array_ready_context_t));
  if (!cb_context) {
    log_error("Failed to allocate memory for listener candidate node array ready context");
    return NULL;
  }

  ct_listener_t* listener = ct_listener_new();

  cb_context->preconnection = preconnection;
  cb_context->listener_callbacks = listener_callbacks;
  cb_context->listener = listener;

  ct_candidate_gathering_callbacks_t callbacks = {
    .candidate_node_array_ready_cb = listener_candidate_node_array_ready_cb,
    .context = cb_context,
  };
  int rc = get_ordered_candidate_nodes(preconnection, callbacks);
  if (rc != 0) {
    log_error("Failed to get ordered candidate nodes for listener");
    ct_listener_free(listener);
    free(cb_context);
    return NULL;
  }
  return listener;
}

int ct_preconnection_initiate(ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks) {
  log_info("Initiating connection from preconnection with candidate racing");

  // The winning connection will be passed to the ready()
  return preconnection_race(preconnection, connection_callbacks);
}

int ct_preconnection_initiate_with_send(ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks, const ct_message_t* message, const ct_message_context_t* message_context) {
  log_debug("Initiating connection from preconnection with send");
  ct_message_t* msg_copy = NULL;
  if (message) {
    msg_copy = ct_message_deep_copy(message);
    if (!msg_copy) {
      log_error("Failed to deep copy message for preconnection initiate with send");
      return -ENOMEM;
    }
  }
  ct_message_context_t* message_context_copy = NULL;
  if (message_context) {
    message_context_copy = ct_message_context_deep_copy(message_context);
    if (!message_context_copy) {
      log_error("Failed to deep copy message context for preconnection initiate with send");
      ct_message_free(msg_copy);
      return -ENOMEM;
    }
  }

  if (message_context && ct_message_context_get_safely_replayable(message_context)) {
    log_info("Initiating connection from preconnection with candidate racing and early data");
    return preconnection_race_with_early_data(preconnection, connection_callbacks, msg_copy, message_context_copy);
  }
  log_info("Initiating connection from preconnection with candidate racing and send after ready");
  return preconnection_race_with_send_after_ready(preconnection, connection_callbacks, msg_copy, message_context_copy);

}

void ct_preconnection_free(ct_preconnection_t* preconnection) {
  log_trace("Freeing preconnection");
  if (!preconnection) {
    return;
  }

  // Free remote endpoint strings and array
  if (preconnection->remote_endpoints != NULL) {
    for (size_t i = 0; i < preconnection->num_remote_endpoints; i++) {
      ct_remote_endpoint_free_strings(&preconnection->remote_endpoints[i]);
    }
    free(preconnection->remote_endpoints);
    preconnection->remote_endpoints = NULL;
  }

  ct_local_endpoint_free_strings(&preconnection->local);

  ct_selection_properties_cleanup(&preconnection->transport_properties.selection_properties);

  if (preconnection->security_parameters) {
    ct_security_parameters_free(preconnection->security_parameters);
  }

  free(preconnection);
}

void ct_preconnection_set_local_endpoint(ct_preconnection_t* preconnection, const ct_local_endpoint_t* local_endpoint) {
  if (!preconnection || !local_endpoint) {
    return;
  }
  // Deep copy the local endpoint so preconnection owns its own copy
  // First free any existing strings in preconnection->local
  ct_local_endpoint_free_strings(&preconnection->local);
  // Then do a deep copy
  ct_local_endpoint_copy_content(local_endpoint, &preconnection->local);
  preconnection->num_local_endpoints = 1;
}

void ct_preconnection_set_framer(ct_preconnection_t* preconnection, ct_framer_impl_t* framer_impl) {
  if (!preconnection) {
    return;
  }
  preconnection->framer_impl = framer_impl;
}

const ct_local_endpoint_t* preconnection_get_local_endpoint(const ct_preconnection_t* preconnection) {
  return &preconnection->local;
}

ct_remote_endpoint_t* const * preconnection_get_remote_endpoints(const ct_preconnection_t* preconnection, size_t* out_count) {
  if (!preconnection) {
    return NULL;
  }
  if (out_count) {
    *out_count = preconnection->num_remote_endpoints;
  }
  return &preconnection->remote_endpoints;
}

const ct_transport_properties_t* preconnection_get_transport_properties(const ct_preconnection_t* preconnection) {
  return &preconnection->transport_properties;
}
