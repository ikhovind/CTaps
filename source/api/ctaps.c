#include "ctaps.h"

#include <logging/log.h>
#include <protocols/registry/protocol_registry.h>

#include "protocols/udp/udp.h"
#include "protocols/tcp/tcp.h"
#include "uv.h"

uv_loop_t* ctaps_event_loop;

int ctaps_initialize() {
  ctaps_event_loop = malloc(sizeof(uv_loop_t));
  uv_loop_init(ctaps_event_loop);

  register_protocol(&udp_protocol_interface);
  register_protocol(&tcp_protocol_interface);
  return 0;
}

void ctaps_start_event_loop() {
  log_info("Starting the libuv event ctaps_event_loop...\n");

  // Run until there are no more waiting tasks
  uv_run(ctaps_event_loop, UV_RUN_DEFAULT);

  uv_loop_close(ctaps_event_loop);
}
