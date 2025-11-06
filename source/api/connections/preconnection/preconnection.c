#include "preconnection.h"

#include <ctaps.h>
#include <candidate_gathering/candidate_gathering.h>
#include <candidate_gathering/candidate_racing.h>
#include <logging/log.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib.h>

#include "connections/listener/socket_manager/socket_manager.h"
#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"

#define INT_TO_STRING(x) #x

#define MAX_PORT_STR_LENGTH sizeof(INT_TO_STRING(UINT16_MAX))

int copy_remote_endpoints(Preconnection* preconnection,
                          const RemoteEndpoint* remote_endpoints,
                          const size_t num_remote_endpoints) {
  preconnection->num_remote_endpoints = num_remote_endpoints;
  preconnection->remote_endpoints = malloc(num_remote_endpoints * sizeof(RemoteEndpoint));
  if (preconnection->remote_endpoints == NULL) {
    log_error("Could not allocate memory for remote endpoints: %s");
    return errno;
  }
  for (int i = 0; i < num_remote_endpoints; i++) {
    memcpy(&preconnection->remote_endpoints[i], &remote_endpoints[i], sizeof(RemoteEndpoint));
    if (remote_endpoints[i].hostname != NULL) {
      // We have copied the pointer, but want a deep copy of the string, so just overwrite the pointer
      preconnection->remote_endpoints[i].hostname = strdup(remote_endpoints[i].hostname);
    }
  }
  return 0;
}

int preconnection_build(Preconnection* preconnection,
                         const TransportProperties transport_properties,
                         const RemoteEndpoint* remote_endpoints,
                         const size_t num_remote_endpoints,
                         const SecurityParameters* security_parameters
                         ) {
  memset(preconnection, 0, sizeof(Preconnection));
  preconnection->transport_properties = transport_properties;
  preconnection->security_parameters = security_parameters;
  local_endpoint_build(&preconnection->local);
  return copy_remote_endpoints(preconnection, remote_endpoints, num_remote_endpoints);
}

int preconnection_build_with_local(Preconnection* preconnection,
                                   TransportProperties transport_properties,
                                   RemoteEndpoint remote_endpoints[],
                                   size_t num_remote_endpoints,
                                   const SecurityParameters* security_parameters,
                                   LocalEndpoint local_endpoint) {
  log_debug("Building preconnection");
  memset(preconnection, 0, sizeof(Preconnection));
  preconnection->transport_properties = transport_properties;
  preconnection->num_local_endpoints = 1;
  preconnection->local = local_endpoint;
  preconnection->security_parameters = security_parameters;

  return copy_remote_endpoints(preconnection, remote_endpoints, num_remote_endpoints);
}


int preconnection_listen(Preconnection* preconnection, Listener* listener, ListenerCallbacks listener_callbacks) {
  log_info("Listening from preconnection");
  SocketManager* socket_manager = malloc(sizeof(SocketManager));
  if (socket_manager == NULL) {
    return -errno;
  }
  memset(socket_manager, 0, sizeof(SocketManager));
  GArray* candidate_nodes = get_ordered_candidate_nodes(preconnection);
  if (candidate_nodes->len > 0) {
    const CandidateNode first_node = g_array_index(candidate_nodes, CandidateNode, 0);

    *listener = (Listener){
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
  log_error("No candidate node for Listener found");
  return -EINVAL;
}

int preconnection_initiate(Preconnection* preconnection, Connection* connection,
                           ConnectionCallbacks connection_callbacks) {
  log_info("Initiating connection from preconnection with candidate racing");

  // Use candidate racing to establish connection
  return preconnection_initiate_with_racing(preconnection, connection, connection_callbacks);
}

void preconnection_free(Preconnection* preconnection) {
  if (preconnection->remote_endpoints != NULL) {
    for (int i = 0; i < preconnection->num_remote_endpoints; i++) {
      free_remote_endpoint_strings(&preconnection->remote_endpoints[i]);
    }
    free(preconnection->remote_endpoints);
    preconnection->remote_endpoints = NULL;
  }
  free_local_endpoint_strings(&preconnection->local);
}

void preconnection_build_user_connection(Connection* connection, const Preconnection* preconnection, ConnectionCallbacks connection_callbacks) {
  log_debug("Building user connection from preconnection");
  memset(connection, 0, sizeof(Connection));

  // Initialize transport properties with defaults
  transport_properties_build(&connection->transport_properties);

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
  connection->socket_manager = NULL;
  connection->protocol_state = NULL;

  log_debug("Setting user connection callbacks");
  connection->connection_callbacks = connection_callbacks;
}
