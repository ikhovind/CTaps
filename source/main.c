#include <stdio.h>

#include "lib.h"
#include "ctaps.h"
#include "connections/preconnection/preconnection.h"
#include "protocols/registry/protocol_registry.h"
#include "transport_properties/selection_properties/selection_properties.h"
#include "transport_properties/transport_properties.h"

int main() {
  struct library lib = create_library();

  ctaps_initialize();

  printf("first protocol is: %s\n", get_supported_protocols()[0]->name);

  for (int i = 0; i < 5; i ++) {
    printf("Preference[i] is: %d\n", get_supported_protocols()[0]->features.values[i]);
  }

  TransportProperties transport_properties;
  selection_properties_init(&transport_properties.selection_properties);
  selection_properties_require(&transport_properties.selection_properties, "reliability");

  Preconnection preconnection;

  RemoteEndpoint remote_endpoint;

  remote_endpoint_with_family(&remote_endpoint, AF_INET);
  remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");
  remote_endpoint_with_port(&remote_endpoint, 5005);
  preconnection_build(&preconnection, transport_properties, remote_endpoint);


  if (printf("Hello from %s!\n", lib.name) < 0) {
    return 1;
  }

  return 0;
}
