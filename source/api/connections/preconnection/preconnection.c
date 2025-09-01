#include "preconnection.h"

#include <ctaps.h>

#define INT_TO_STRING(x) #x

#define MAX_PORT_STR_LENGTH sizeof(INT_TO_STRING(UINT16_MAX))


int perform_dns_lookup(const char* hostname, const char* service, RemoteEndpoint** out_list, size_t* out_count, uv_getaddrinfo_cb getaddrinfo_cb) {
  printf("Performing dns lookup for hostname: %s\n", hostname);
  uv_getaddrinfo_t request;

  int rc = uv_getaddrinfo(
    ctaps_event_loop,
    &request,
    getaddrinfo_cb,
    hostname,
    service,
    NULL//&hints
  );

  if (rc < 0) {
    return rc;
  }

  *out_count = 0;
  int count = 0;

  for (struct addrinfo* ptr = request.addrinfo; ptr != NULL; ptr = ptr->ai_next) {
    count++;
  }

  *out_list = malloc(count);
  if (*out_list == NULL) {
    return -1;
  }

  printf("About to add to output list\n");
  printf("Found %d addresses\n", count);

  // 3. Loop through the source addrinfo list.
  for (struct addrinfo* ptr = request.addrinfo; ptr != NULL; ptr = ptr->ai_next) {
    // 4. Allocate a new RemoteEndpoint node.
    RemoteEndpoint new_node;

    new_node.type = REMOTE_ENDPOINT_TYPE_ADDRESS;

    if (ptr->ai_family == AF_INET) {
      memcpy(&new_node.data.address, ptr->ai_addr, sizeof(struct sockaddr_in));
      new_node.port = ntohs(((struct sockaddr_in*)ptr->ai_addr)->sin_port);
    } else if (ptr->ai_family == AF_INET6) {
      memcpy(&new_node.data.address, ptr->ai_addr, sizeof(struct sockaddr_in6));
      new_node.port = ntohs(((struct sockaddr_in6*)ptr->ai_addr)->sin6_port);
    } else {
      // Skip address families we don't handle.
      continue;
    }
    (*out_list)[*out_count] = new_node;
    (*out_count)++;
  }
  return 0;
}

int copy_remote_endpoints(Preconnection* preconnection,
                         RemoteEndpoint* remote_endpoints,
                         size_t num_remote_endpoints) {
  preconnection->num_remote_endpoints = num_remote_endpoints;
  preconnection->remote_endpoints = malloc(num_remote_endpoints * sizeof(RemoteEndpoint));
  for (int i = 0; i < num_remote_endpoints; i++) {
    if (remote_endpoints[i].type == REMOTE_ENDPOINT_TYPE_UNSPECIFIED) {
      printf("Remote endpoint is not specified\n");
      return -1;
    }
    memcpy(&preconnection->remote_endpoints[i], &remote_endpoints[i], sizeof(RemoteEndpoint));
    if (remote_endpoints[i].type == REMOTE_ENDPOINT_TYPE_HOSTNAME) {
      // We have copied the pointer, but want a deep copy of the string, so just overwrite the pointer
      preconnection->remote_endpoints[i].data.hostname = malloc(strlen(remote_endpoints[i].data.hostname) + 1);
      strcpy(preconnection->remote_endpoints[i].data.hostname, remote_endpoints[i].data.hostname);
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
  transport_properties_protocol_stacks_with_selection_properties(
      &preconnection->transport_properties, &listener->protocol,
      &num_found_protocols);
  if (num_found_protocols > 0) {
    printf("Found at least one protocolt when listening\n");
  }

  return socket_manager_create(socket_manager, listener);
}

int preconnection_initiate(Preconnection* preconnection, Connection* connection,
                           InitDoneCb init_done_cb, uv_getaddrinfo_cb dns_cb) {
  printf("Initiating connection from preconnection\n");


  GArray* resolved_endpoints = g_array_new(false, true, sizeof(RemoteEndpoint));
  int num_failed_lookups = 0;

  printf("Num remote endpoints is: %d\n", preconnection->num_remote_endpoints);
  for (int i = 0; i < preconnection->num_remote_endpoints; i++) {
    if (preconnection->remote_endpoints[i].type == REMOTE_ENDPOINT_TYPE_HOSTNAME) {
      RemoteEndpoint* dns_lookup_list = NULL;
      size_t num_dns_results = 0;

      char service_str[MAX_PORT_STR_LENGTH];
      sprintf(service_str, "%d", preconnection->remote_endpoints[i].port);

      int rc = perform_dns_lookup(
        preconnection->remote_endpoints[i].data.hostname,
        service_str,
        &dns_lookup_list,
        &num_dns_results,
        NULL
      );
      if (rc < 0) {
        printf("DNS lookup failed for hostname %s with error %d\n", preconnection->remote_endpoints[i].data.hostname, rc);
        // only return error code if we have *no* valid endpoints
        if (preconnection->num_remote_endpoints == ++num_failed_lookups) {
          return rc;
        }
      }
      g_array_append_vals(resolved_endpoints, dns_lookup_list, num_dns_results);
      free(dns_lookup_list);
    }
    else {
      g_array_append_val(resolved_endpoints, preconnection->remote_endpoints[i]);
    }
  }

  int num_found_protocols = 0;
  transport_properties_protocol_stacks_with_selection_properties(
      &preconnection->transport_properties, &connection->protocol,
      &num_found_protocols);
  if (num_found_protocols > 0) {
    connection->remote_endpoint = g_array_index(resolved_endpoints, RemoteEndpoint, 0);
    g_array_free(resolved_endpoints, true);

    connection->open_type = CONNECTION_OPEN_TYPE_ACTIVE;
    connection->local_endpoint = preconnection->local;
    connection->protocol.init(connection, init_done_cb);
    return 0;
  }
  printf("No protocol found\n");
  return -1;
}

