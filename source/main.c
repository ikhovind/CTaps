#include <stdio.h>

#include "lib.h"
#include "connections/preconnection/preconnection.h"
#include "transport_properties/selection_properties/selection_properties.h"
#include "transport_properties/transport_properties.h"

int main() {
  struct library lib = create_library();

  TransportProperties transport_properties;
  selection_properties_init(&transport_properties.selection_properties);
  selection_properties_require(&transport_properties.selection_properties, "reliability");

  Preconnection preconnection;

  preconnection_init(&preconnection, transport_properties);


  if (printf("Hello from %s!\n", lib.name) < 0) {
    return 1;
  }

  return 0;
}
