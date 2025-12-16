#include "ctaps.h"

#include "protocol/tcp/tcp.h"
#include "protocol/udp/udp.h"
#include <logging/log.h>
#include <protocol/quic/quic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

uv_loop_t* event_loop;

ct_config_t global_config = {
  .cert_file_name = NULL,
  .key_file_name = NULL
};

int ct_initialize(const char *cert_file_name, const char *key_file_name) {
  // Set default log level to INFO
  log_set_level(LOG_INFO);

  event_loop = malloc(sizeof(uv_loop_t));
  uv_loop_init(event_loop);

  ct_register_protocol(&udp_protocol_interface);
  ct_register_protocol(&tcp_protocol_interface);
  ct_register_protocol(&quic_protocol_interface);

  global_config.cert_file_name = cert_file_name ? strdup(cert_file_name) : NULL;
  global_config.key_file_name = key_file_name ? strdup(key_file_name) : NULL;
  return 0;
}

int ct_close() {
  int rc = uv_loop_close(event_loop);
  if (rc < 0) {
    log_error("Error closing libuv event loop: %s", uv_strerror(rc));
    return rc;
  }
  free(event_loop);
  if (global_config.cert_file_name) {
    free(global_config.cert_file_name);
  }
  if (global_config.key_file_name) {
    free(global_config.key_file_name);
  }
  log_info("Successfully closed CTaps");
  return 0;
}

void ct_start_event_loop() {
  log_info("Starting the libuv event event_loop...");

  // Run until there are no more waiting tasks
  uv_run(event_loop, UV_RUN_DEFAULT);
}

void ct_set_log_level(ct_log_level_t level) {
  log_set_level(level);
}

int ct_add_log_file(const char* file_path, ct_log_level_t min_level) {
  FILE* fp = fopen(file_path, "ae");
  if (fp == NULL) {
    return -1;
  }
  return log_add_fp(fp, min_level);
}
