#include "ctaps.h"
#include "connections/preconnection/preconnection.h"
#include "protocols/registry/protocol_registry.h"
#include "transport_properties/selection_properties/selection_properties.h"
#include "transport_properties/transport_properties.h"

 int connection_received(struct ct_listener_t* listener, struct ct_connection_t* new_conn, void* udata) {
   printf("ct_connection_t received\n");
 }
 int establishment_error(struct ct_listener_t* listener, const char* reason, void* udata) {
   printf("Establishment error: %s\n", reason);
 }

int main() {

  ct_initialize(NULL,NULL);

  return 0;
}
