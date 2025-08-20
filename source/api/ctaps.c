#include "ctaps.h"
#include "uv.h"

#include "protocols/udp/udp.h"


uv_loop_t *ctaps_event_loop;

int ctaps_initialize() {
    ctaps_event_loop = uv_default_loop();
    register_udp_support();
    return 0;
}

void idle_cb(struct uv_idle_s* id) {
    printf("idle_cb\n");
}


void ctaps_start_event_loop() {
    printf("Starting the libuv event ctaps_event_loop...\n");

    // Run until there are no more waiting tasks
    uv_run(ctaps_event_loop, UV_RUN_DEFAULT);

    uv_loop_close(ctaps_event_loop); // Close the ctaps_event_loop, freeing associated resources
}

