
#include "connection/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include "transport_property/selection_properties/selection_properties.h"
#include <candidate_gathering/candidate_gathering.h>
#include <candidate_gathering/candidate_racing.h>
#include <endpoint/local_endpoint.h>
#include <endpoint/remote_endpoint.h>
#include <security_parameter/security_parameters.h>
#include "message/message.h"
#include "message/message_context.h"
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
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
  if (preconnection->remote_endpoints == NULL) {
    log_error("Could not allocate memory for remote endpoints: %s");
    return errno;
  }
  // Deep copy each remote endpoint (copies all strings)
  for (size_t i = 0; i < num_remote_endpoints; i++) {
    preconnection->remote_endpoints[i] = ct_remote_endpoint_copy_content(&remote_endpoints[i]);
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

  // Deep copy transport properties or use defaults
  // Note: We can't use ct_transport_properties_deep_copy() here because transport_properties
  // is embedded in the preconnection struct, not a pointer. We manually copy using the
  // underlying helper functions.
  if (transport_properties) {
    ct_selection_properties_deep_copy(&precon->transport_properties.selection_properties,
                                     &transport_properties->selection_properties);
    precon->transport_properties.connection_properties = transport_properties->connection_properties;
  } else {
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


int ct_preconnection_listen(ct_preconnection_t* preconnection, ct_listener_t* listener, ct_listener_callbacks_t listener_callbacks) {
  log_info("Listening from preconnection");
  GArray* candidate_nodes = get_ordered_candidate_nodes(preconnection);
  if (candidate_nodes->len > 0) {
    const ct_candidate_node_t first_node = g_array_index(candidate_nodes, ct_candidate_node_t, 0);


    ct_socket_manager_t* socket_manager = ct_socket_manager_new(first_node.protocol_candidate->protocol_impl, listener);
    if (socket_manager == NULL) {
      return -errno;
    }
    *listener = (ct_listener_t){
      .listener_callbacks = listener_callbacks,
      .local_endpoint = *first_node.local_endpoint,
      .num_local_endpoints = 1,
      .socket_manager = ct_socket_manager_ref(socket_manager),
      .transport_properties = preconnection->transport_properties,
      .security_parameters = preconnection->security_parameters,
    };

    log_info("Starting to listen on ct_listener_t using protocol: %s on port: %d", socket_manager->protocol_impl->name, listener->local_endpoint.port);
    return socket_manager->protocol_impl->listen(socket_manager);
  }
  g_array_free(candidate_nodes, true);
  log_error("No candidate node for ct_listener_t found");
  return -EINVAL;
}

int ct_preconnection_initiate(ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks) {
  log_info("Initiating connection from preconnection with candidate racing");

  // The winning connection will be passed to the ready()
  //
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

  if (message_context && ct_message_properties_get_safely_replayable(ct_message_context_get_message_properties(message_context))) {
    log_info("Initiating connection from preconnection with candidate racing and early data");
    return preconnection_race_with_early_data(preconnection, connection_callbacks, msg_copy, message_context_copy);
  }
  log_info("Initiating connection from preconnection with candidate racing and send after ready");
  return preconnection_race_with_send_after_ready(preconnection, connection_callbacks, msg_copy, message_context_copy);

}

void ct_preconnection_free(ct_preconnection_t* preconnection) {
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

  // Free local endpoint strings
  ct_local_endpoint_free_strings(&preconnection->local);

  // Clean up embedded transport properties (frees GHashTable if created)
  ct_selection_properties_cleanup(&preconnection->transport_properties.selection_properties);

  // Free security parameters (owns a deep copy)
  if (preconnection->security_parameters) {
    ct_sec_param_free(preconnection->security_parameters);
  }

  // Free the preconnection struct itself
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
  preconnection->local = ct_local_endpoint_copy_content(local_endpoint);
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
