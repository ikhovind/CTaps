#include "ctaps.h"

#include "protocols/udp/udp.h"
#include "uv.h"

uv_loop_t* ctaps_event_loop;

int ctaps_initialize() {
  ctaps_event_loop = uv_default_loop();
  register_udp_support();
  return 0;
}

void ctaps_start_event_loop() {
  printf("Starting the libuv event ctaps_event_loop...\n");

  // Run until there are no more waiting tasks
  uv_run(ctaps_event_loop, UV_RUN_DEFAULT);

  uv_loop_close(ctaps_event_loop);
}
