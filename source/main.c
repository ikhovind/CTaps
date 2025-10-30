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
  Listener listener;
  Connection client_connection;

  LocalEndpoint listener_endpoint;
  local_endpoint_build(&listener_endpoint);

  local_endpoint_with_interface(&listener_endpoint, "lo");
  local_endpoint_with_port(&listener_endpoint, 1239);

  RemoteEndpoint listener_remote;
  remote_endpoint_build(&listener_remote);
  remote_endpoint_with_hostname(&listener_remote, "127.0.0.1");

  TransportProperties listener_props;
  transport_properties_build(&listener_props);

  tp_set_sel_prop_preference(&listener_props, RELIABILITY, REQUIRE);

  Preconnection listener_precon;
  preconnection_build_with_local(&listener_precon, listener_props, &listener_remote, 1, listener_endpoint);

  ListenerCallbacks listener_callbacks = {
      .connection_received = connection_received,
  };

  int listen_res = preconnection_listen(&listener_precon, &listener, listener_callbacks);

  ctaps_start_event_loop();

  return 0;
}
