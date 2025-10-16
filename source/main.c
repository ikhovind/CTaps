#include "ctaps.h"
#include "connections/preconnection/preconnection.h"
#include "protocols/registry/protocol_registry.h"
#include "transport_properties/selection_properties/selection_properties.h"
#include "transport_properties/transport_properties.h"

int main() {

  ctaps_initialize();

  printf("first protocol is: %s\n", get_supported_protocols()[0]->name);

  SelectionProperties selection_properties;
  selection_properties_build(&selection_properties);
  selection_properties.selection_property[RELIABILITY].value.simple_preference = REQUIRE;
  /*
  for (int i = 0; i < 5; i++) {
    printf("Preference[i] is: %d\n",
           get_supported_protocols()[0]->selection_properties.selection_property[i].value);
  }
  */

  TransportProperties transport_properties;
  selection_properties_build(&transport_properties.selection_properties);
  /*
  selection_properties_require(&transport_properties.selection_properties,
                               "reliability");
                               */

  Preconnection preconnection;

  RemoteEndpoint remote_endpoint;

  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, 5005);
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);

  return 0;
}
