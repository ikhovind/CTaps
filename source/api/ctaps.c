#include "ctaps.h"

#include "protocols/udp/udp.h"

int ctaps_initialize() {
    register_udp_support();
    return 0;
}


void ctaps_start_event_loop() {

}

