#include <stdio.h>

#include "lib.h"
#include "api/transport_properties/selection_properties/selection_properties.h"
#include "api/transport_properties/transport_properties.h"

int main() {
  struct library lib = create_library();

  TransportProperties transport_properties;
  selection_properties_init(&transport_properties.selection_properties);

  selection_properties_require(&transport_properties.selection_properties, "hello world");


  if (printf("Hello from %s!\n", lib.name) < 0) {
    return 1;
  }

  return 0;
}
