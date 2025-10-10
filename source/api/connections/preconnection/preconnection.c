#include "preconnection.h"

#include <ctaps.h>
#include <candidate_gathering/candidate_gathering.h>
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
                         const size_t num_remote_endpoints
                         ) {
  memset(preconnection, 0, sizeof(Preconnection));
  preconnection->transport_properties = transport_properties;
  local_endpoint_build(&preconnection->local);
  return copy_remote_endpoints(preconnection, remote_endpoints, num_remote_endpoints);
}

int preconnection_build_with_local(Preconnection* preconnection,
                                    TransportProperties transport_properties,
                                    RemoteEndpoint remote_endpoints[],
                                    size_t num_remote_endpoints,
                                    LocalEndpoint local_endpoint) {
  log_debug("Building preconnection\n");
  memset(preconnection, 0, sizeof(Preconnection));
  preconnection->transport_properties = transport_properties;
  preconnection->num_local_endpoints = 1;
  preconnection->local = local_endpoint;

  return copy_remote_endpoints(preconnection, remote_endpoints, num_remote_endpoints);
}


int preconnection_listen(Preconnection* preconnection, Listener* listener, ConnectionReceivedCb connection_received_cb, void* user_data) {
  SocketManager* socket_manager = malloc(sizeof(SocketManager));
  if (socket_manager == NULL) {
    return -errno;
  }
  memset(socket_manager, 0, sizeof(SocketManager));
  GArray* candidate_nodes = get_ordered_candidate_nodes(preconnection);
  if (candidate_nodes->len > 0) {
    const CandidateNode first_node = g_array_index(candidate_nodes, CandidateNode, 0);

    *listener = (Listener){
      .connection_received_cb = connection_received_cb,
      .local_endpoint = first_node.local_endpoint,
      .num_local_endpoints = 1,
      .socket_manager = socket_manager,
      .transport_properties = preconnection->transport_properties,
      .user_data = user_data
    };
    socket_manager->protocol_impl = *first_node.protocol;


    return socket_manager_build(socket_manager, listener);
  }
  g_array_free(candidate_nodes, true);
  free(socket_manager);
  log_error("No candidate node for Listener found");
  return -EINVAL;
}

int preconnection_initiate(Preconnection* preconnection, Connection* connection,
                           InitDoneCb init_done_cb, uv_getaddrinfo_cb dns_cb) {
  log_info("Initiating connection from preconnection\n");

  GArray* candidate_nodes = get_ordered_candidate_nodes(preconnection);

  if (candidate_nodes->len > 0) {
    CandidateNode first_node = g_array_index(candidate_nodes, CandidateNode, 0);
    connection->protocol = *first_node.protocol;
    connection->remote_endpoint = first_node.remote_endpoint;

    connection->open_type = CONNECTION_OPEN_TYPE_ACTIVE;
    connection->local_endpoint = first_node.local_endpoint;

    g_array_free(candidate_nodes, true);

    connection->protocol.init(connection, init_done_cb);
    return 0;
  }
  log_error("No candidate node for Connection found\n");
  return -EINVAL;
}

