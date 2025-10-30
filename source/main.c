#include "ctaps.h"
#include "connections/preconnection/preconnection.h"
#include "protocols/registry/protocol_registry.h"
#include "transport_properties/selection_properties/selection_properties.h"
#include "transport_properties/transport_properties.h"

 int connection_received(struct Listener* listener, struct Connection* new_conn, void* udata) {
   printf("Connection received\n");
 }
 int establishment_error(struct Listener* listener, const char* reason, void* udata) {
   printf("Establishment error: %s\n", reason);
 }

int main() {

  ctaps_initialize(NULL,NULL);

  return 0;
}
