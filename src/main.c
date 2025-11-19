#include "ctaps.h"

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
