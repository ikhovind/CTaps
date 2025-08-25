#include "preconnection.h"

#include <ctaps.h>

int lookup(const char* hostname, RemoteEndpoint* out_head) {
  uv_getaddrinfo_t request;

  int rc = uv_getaddrinfo(
    ctaps_event_loop,
    &request,
    NULL,
    hostname,
    NULL,
    NULL//&hints
  );

  if (rc < 0) {
    return rc;
  }

  RemoteEndpoint* head = NULL;
  RemoteEndpoint** current_ptr = &head;

  // 3. Loop through the source addrinfo list.
  for (struct addrinfo* ptr = request.addrinfo; ptr != NULL; ptr = ptr->ai_next) {

    // 4. Allocate a new RemoteEndpoint node.
    RemoteEndpoint* new_node = malloc(sizeof(RemoteEndpoint));
    if (new_node == NULL) {
      // In a real application, you should free the list built so far.
      return -1;
    }

    new_node->type = ENDPOINT_TYPE_ADDRESS;
    new_node->next = NULL;

    // 5. Copy the address data.
    if (ptr->ai_family == AF_INET) {
      memcpy(&new_node->data.address, ptr->ai_addr, sizeof(struct sockaddr_in));
    } else if (ptr->ai_family == AF_INET6) {
      memcpy(&new_node->data.address, ptr->ai_addr, sizeof(struct sockaddr_in6));
    } else {
      // Skip address families we don't handle.
      free(new_node);
      continue;
    }

    // 6. Link the new node into our list and advance the pointer.
    *current_ptr = new_node;
    current_ptr = &new_node->next;
  }

  out_head = head;


  // Normally we free this is the callback, but on error or synchronous we cannot
  /*
  if (rc < 0 || callback == NULL) {
    free(request);
  }
  */

  return rc;
}

void preconnection_build(Preconnection* preconnection,
                         const TransportProperties transport_properties,
                         RemoteEndpoint remote_endpoint) {
  printf("initializing preconnection\n");
  preconnection->transport_properties = transport_properties;
  preconnection->remote = remote_endpoint;
  preconnection->local.initialized = false;
}

void preconnection_build_with_local(Preconnection* preconnection,
                                    TransportProperties transport_properties,
                                    RemoteEndpoint remote_endpoint,
                                    LocalEndpoint local_endpoint) {
  printf("initializing preconnection\n");
  preconnection->transport_properties = transport_properties;
  preconnection->remote = remote_endpoint;
  preconnection->local = local_endpoint;
}

int preconnection_initiate(Preconnection* preconnection, Connection* connection,
                            InitDoneCb init_done_cb) {
  printf("Initiating connection from preconnection\n");


  // TODO - sort out the building of this linked list
  /*
  RemoteEndpoint* head = NULL;
  RemoteEndpoint** current_ptr = &head;
  RemoteEndpoint* remote_endpoint;
  for (remote_endpoint = &preconnection->remote; remote_endpoint != NULL; remote_endpoint = NULL) {
    if (remote_endpoint->type == ENDPOINT_TYPE_UNSPECIFIED) {
      printf("Remote endpoint is not specified\n");
      return -1;
    }
    if (remote_endpoint->type == ENDPOINT_TYPE_HOSTNAME) {
      if (remote_endpoint->data.hostname == NULL) {
        printf("Remote endpoint hostname is not specified\n");
        return -1;
      }

      RemoteEndpoint lookup_head;

      lookup(remote_endpoint->data.hostname, &lookup_head);

    }
    if (remote_endpoint->type == ENDPOINT_TYPE_ADDRESS) {
      if (remote_endpoint->data.address.ss_family == AF_UNSPEC) {
        printf("Remote endpoint address family is not specified\n");
        return -1;
      }
    }
  }
  */

  int num_found_protocols = 0;
  transport_properties_protocol_stacks_with_selection_properties(
      &preconnection->transport_properties, &connection->protocol,
      &num_found_protocols);
  if (num_found_protocols > 0) {
    connection->remote_endpoint = preconnection->remote;

    if (preconnection->local.initialized) {
      connection->local_endpoint = preconnection->local;
      connection->local_endpoint.initialized = true;
    }
    else {
      connection->local_endpoint.initialized = false;
    }
    connection->protocol.init(connection, init_done_cb);
    return 0;
  }
  printf("No protocol found\n");
  return -1;
}

