#include "preconnection.h"

void preconnection_build(Preconnection* preconnection,
                         const TransportProperties transport_properties,
                         RemoteEndpoint remote_endpoint) {
  printf("initializing preconnection\n");
  preconnection->transport_properties = transport_properties;
  printf("Remote endpoint port when building preconnection is: %d\n",
         remote_endpoint.addr.ipv4_addr.sin_port);
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
