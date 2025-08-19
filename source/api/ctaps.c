#include "ctaps.h"
#include "uv.h"

#include "protocols/udp/udp.h"


uv_loop_t ctaps_event_loop;

int ctaps_initialize() {
    register_udp_support();
    return 0;
}

void idle_cb(struct uv_idle_s* id) {
    printf("idle_cb\n");
}


void ctaps_start_event_loop() {

    // Get the default event ctaps_event_loop
    uv_loop_t *loop = uv_default_loop();

    // Initialize an idle handle
    uv_idle_t idler;
    uv_idle_init(loop, &idler);

    // Start the idle handle, associating it with the callback function
    uv_idle_start(&idler, idle_cb);

    printf("Starting the libuv event ctaps_event_loop...\n");

    // Run the event ctaps_event_loop in default mode (blocks until no more active handles/requests)
    uv_run(loop, UV_RUN_DEFAULT);

    // Close the handle and free resources
    uv_close((uv_handle_t*)&idler, NULL);
    uv_loop_close(loop); // Close the ctaps_event_loop, freeing associated resources

    printf("Application finished.\n");
}

