
#include <candidate_gathering/candidate_gathering.h>
#include <candidate_gathering/candidate_racing.h>
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include "ctaps.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "connection/socket_manager/socket_manager.h"
#include "connection/connection.h"
#include "ctaps.h"

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
  for (int i = 0; i < num_remote_endpoints; i++) {
    memcpy(&preconnection->remote_endpoints[i], &remote_endpoints[i], sizeof(ct_remote_endpoint_t));
    if (remote_endpoints[i].hostname != NULL) {
      // We have copied the pointer, but want a deep copy of the string, so just overwrite the pointer
      preconnection->remote_endpoints[i].hostname = strdup(remote_endpoints[i].hostname);
    }
  }
  return 0;
}

int ct_preconnection_build_ex(ct_preconnection_t* preconnection,
                         const ct_transport_properties_t transport_properties,
                         const ct_remote_endpoint_t* remote_endpoints,
                         const size_t num_remote_endpoints,
                         const ct_security_parameters_t* security_parameters,
                         ct_framer_impl_t* framer_impl
                         ) {
  memset(preconnection, 0, sizeof(ct_preconnection_t));
  preconnection->transport_properties = transport_properties;
  preconnection->security_parameters = security_parameters;
  preconnection->framer_impl = framer_impl;
  ct_local_endpoint_build(&preconnection->local);
  return copy_remote_endpoints(preconnection, remote_endpoints, num_remote_endpoints);
}

int ct_preconnection_build(ct_preconnection_t* preconnection,
                         const ct_transport_properties_t transport_properties,
                         const ct_remote_endpoint_t* remote_endpoints,
                         const size_t num_remote_endpoints,
                         const ct_security_parameters_t* security_parameters
                         ) {
  return ct_preconnection_build_ex(preconnection, transport_properties,
                                   remote_endpoints, num_remote_endpoints,
                                   security_parameters, NULL);
}

int ct_preconnection_build_with_local(ct_preconnection_t* preconnection,
                                   ct_transport_properties_t transport_properties,
                                   ct_remote_endpoint_t remote_endpoints[],
                                   size_t num_remote_endpoints,
                                   const ct_security_parameters_t* security_parameters,
                                   ct_local_endpoint_t local_endpoint) {
  log_debug("Building preconnection");
  memset(preconnection, 0, sizeof(ct_preconnection_t));
  preconnection->transport_properties = transport_properties;
  preconnection->num_local_endpoints = 1;
  preconnection->local = local_endpoint;
  preconnection->security_parameters = security_parameters;

  return copy_remote_endpoints(preconnection, remote_endpoints, num_remote_endpoints);
}


int ct_preconnection_listen(ct_preconnection_t* preconnection, ct_listener_t* listener, ct_listener_callbacks_t listener_callbacks) {
  log_info("Listening from preconnection");
  ct_socket_manager_t* socket_manager = malloc(sizeof(ct_socket_manager_t));
  if (socket_manager == NULL) {
    return -errno;
  }
  memset(socket_manager, 0, sizeof(ct_socket_manager_t));
  GArray* candidate_nodes = get_ordered_candidate_nodes(preconnection);
  if (candidate_nodes->len > 0) {
    const ct_candidate_node_t first_node = g_array_index(candidate_nodes, ct_candidate_node_t, 0);

    *listener = (ct_listener_t){
      .listener_callbacks = listener_callbacks,
      .local_endpoint = *first_node.local_endpoint,
      .num_local_endpoints = 1,
      .socket_manager = socket_manager,
      .transport_properties = preconnection->transport_properties,
      .security_parameters = preconnection->security_parameters,
    };
    socket_manager->protocol_impl = *first_node.protocol;

    socket_manager_build(socket_manager, listener);
    return socket_manager->protocol_impl.listen(socket_manager);
  }
  g_array_free(candidate_nodes, true);
  free(socket_manager);
  log_error("No candidate node for ct_listener_t found");
  return -EINVAL;
}

int ct_preconnection_initiate(ct_preconnection_t* preconnection, ct_connection_t* connection,
                           ct_connection_callbacks_t connection_callbacks) {
  log_info("Initiating connection from preconnection with candidate racing");

  // Use candidate racing to establish connection
  return preconnection_initiate_with_racing(preconnection, connection, connection_callbacks);
}

void ct_preconnection_free(ct_preconnection_t* preconnection) {
  if (preconnection->remote_endpoints != NULL) {
    for (int i = 0; i < preconnection->num_remote_endpoints; i++) {
      ct_free_remote_endpoint_strings(&preconnection->remote_endpoints[i]);
    }
    free(preconnection->remote_endpoints);
    preconnection->remote_endpoints = NULL;
  }
  ct_free_local_endpoint_strings(&preconnection->local);
}

void ct_preconnection_build_user_connection(ct_connection_t* connection, const ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks) {
  log_debug("Building user connection from preconnection");
  ct_connection_build_with_connection_group(connection);

  // Initialize transport properties with defaults
  ct_transport_properties_build(&connection->transport_properties);

  // Copy transport properties from preconnection
  connection->transport_properties = preconnection->transport_properties;

  // Set initial connection state to ESTABLISHING
  connection->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_ESTABLISHING;

  // Initialize message queues
  connection->received_messages = g_queue_new();
  connection->received_callbacks = g_queue_new();
  log_info("Received callback of user connection: %p", connection->received_callbacks);

  // Set basic fields from preconnection
  connection->open_type = CONNECTION_TYPE_STANDALONE;
  connection->security_parameters = preconnection->security_parameters;
  connection->framer_impl = preconnection->framer_impl;  // Copy framer from preconnection
  connection->socket_manager = NULL;
  connection->internal_connection_state = NULL;

  log_debug("Setting user connection callbacks");
  connection->connection_callbacks = connection_callbacks;
}
