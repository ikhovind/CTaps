//
// Created by ikhovind on 18.08.25.
//

#ifndef TAPS_H
#define TAPS_H
#include <uv.h>

extern uv_loop_t* ctaps_event_loop;

int ctaps_initialize();

void ctaps_start_event_loop();

void ctaps_cleanup();

#endif  // TAPS_H
