#include "preconnection.h"

#include <ctaps.h>
#include <logging/log.h>

#include "endpoints/remote/remote_endpoint.h"
#include <protocols/registry/protocol_registry.h>

#define INT_TO_STRING(x) #x

#define MAX_PORT_STR_LENGTH sizeof(INT_TO_STRING(UINT16_MAX))

int copy_remote_endpoints(Preconnection* preconnection,
                         RemoteEndpoint* remote_endpoints,
                         size_t num_remote_endpoints) {
  preconnection->num_remote_endpoints = num_remote_endpoints;
  preconnection->remote_endpoints = malloc(num_remote_endpoints * sizeof(RemoteEndpoint));
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
                         RemoteEndpoint* remote_endpoints,
                         size_t num_remote_endpoints
                         ) {
  memset(preconnection, 0, sizeof(Preconnection));
  printf("initializing preconnection\n");
  preconnection->transport_properties = transport_properties;
  local_endpoint_build(&preconnection->local);
  return copy_remote_endpoints(preconnection, remote_endpoints, num_remote_endpoints);
}

int preconnection_build_with_local(Preconnection* preconnection,
                                    TransportProperties transport_properties,
                                    RemoteEndpoint remote_endpoints[],
                                    size_t num_remote_endpoints,
                                    LocalEndpoint local_endpoint) {
  printf("initializing preconnection\n");
  memset(preconnection, 0, sizeof(Preconnection));
  preconnection->transport_properties = transport_properties;
  preconnection->num_local_endpoints = 1;
  preconnection->local = local_endpoint;

  return copy_remote_endpoints(preconnection, remote_endpoints, num_remote_endpoints);
}


int preconnection_listen(Preconnection* preconnection, Listener* listener, ConnectionReceivedCb connection_received_cb, void* user_data) {
  SocketManager* socket_manager = malloc(sizeof(SocketManager));
  memset(socket_manager, 0, sizeof(SocketManager));
  if (socket_manager == NULL) {
    return -1;
  }
  *listener = (Listener){
    .connection_received_cb = connection_received_cb,
    .local_endpoint = preconnection->local,
    .num_local_endpoints = 1,
    .socket_manager = socket_manager,
    .transport_properties = preconnection->transport_properties,
    .user_data = user_data
  };
  printf("Setting user data pointer to %p\n", user_data);

  int num_found_protocols = 0;
  ProtocolImplementation* candidate_stacks[MAX_PROTOCOLS];
  transport_properties_get_candidate_stacks(
    &preconnection->transport_properties.selection_properties, candidate_stacks,
    &num_found_protocols);
  if (num_found_protocols > 0) {
    printf("Found at least one protocol when listening\n");
  }
  socket_manager->protocol_impl = *candidate_stacks[0];
  LocalEndpoint* local_endpoint_list = NULL;
  size_t num_found_local = 0;
  local_endpoint_resolve(&listener->local_endpoint, &local_endpoint_list, &num_found_local);
  if (num_found_local == 0) {
    log_error("No local endpoints found when listening\n");
    free(socket_manager);
    return -1;
  }
    listener->local_endpoint = local_endpoint_list[0];


  return socket_manager_build(socket_manager, listener);
}

int preconnection_initiate(Preconnection* preconnection, Connection* connection,
                           InitDoneCb init_done_cb, uv_getaddrinfo_cb dns_cb) {
  printf("Initiating connection from preconnection\n");


  GArray* resolved_endpoints = g_array_new(false, true, sizeof(RemoteEndpoint));
  for (int i = 0; i < preconnection->num_remote_endpoints; i++) {
    RemoteEndpoint* remote_endpoint_list = NULL;
    size_t num_found_remote_endpoints = 0;
    remote_endpoint_resolve(&preconnection->remote_endpoints[i], &remote_endpoint_list, &num_found_remote_endpoints);
    g_array_append_vals(resolved_endpoints, remote_endpoint_list, num_found_remote_endpoints);
    free(remote_endpoint_list);
  }

  int num_found_protocols = 0;
  ProtocolImplementation* candidate_stacks[MAX_PROTOCOLS];
  transport_properties_get_candidate_stacks(
    &preconnection->transport_properties.selection_properties, candidate_stacks,
    &num_found_protocols);
  if (num_found_protocols > 0) {
    connection->protocol = *candidate_stacks[0];
    connection->remote_endpoint = g_array_index(resolved_endpoints, RemoteEndpoint, 0);
    g_array_free(resolved_endpoints, true);

    connection->open_type = CONNECTION_OPEN_TYPE_ACTIVE;
    connection->local_endpoint = preconnection->local;


    if (num_found_protocols > 0) {
      printf("Found at least one protocol when listening\n");
    }
    LocalEndpoint* local_endpoint_list = NULL;
    size_t num_found_local = 0;
    local_endpoint_resolve(&connection->local_endpoint, &local_endpoint_list, &num_found_local);

    if (num_found_local == 0) {
      log_error("No local endpoints found when initiating connection\n");
      return -1;
    }

    connection->local_endpoint = local_endpoint_list[0];

    connection->protocol.init(connection, init_done_cb);
    return 0;
  }
  printf("No protocol found\n");
  return -1;
}

