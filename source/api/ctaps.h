//
// Created by ikhovind on 18.08.25.
//

#ifndef TAPS_H
#define TAPS_H
#include <stdbool.h>
#include <uv.h>

typedef struct CtapsConfig {
  char* cert_file_name;
  char* key_file_name;
} CtapsConfig;

extern CtapsConfig ctaps_global_config; // TODO - move from per-library to per-instance config, see REFACTOR.md

extern uv_loop_t* ctaps_event_loop;

int ctaps_initialize(const char *cert_file_name, const char *key_file_name);

void ctaps_start_event_loop();

int ctaps_close();

#endif  // TAPS_H
