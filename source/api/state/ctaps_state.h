//
// Created by ikhovind on 18.08.25.
//

#ifndef CTAPS_STATE_H
#define CTAPS_STATE_H
#include <stdbool.h>
#include <uv.h>

typedef struct ct_config_t {
  char* cert_file_name;
  char* key_file_name;
} ct_config_t;

extern ct_config_t global_config; // TODO - move from per-library to per-instance config, see REFACTOR.md

extern uv_loop_t* event_loop;

int ct_initialize(const char *cert_file_name, const char *key_file_name);

void ct_start_event_loop();

int ct_close();

#endif  // CTAPS_STATE_H
