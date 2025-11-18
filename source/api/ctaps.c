#include "ctaps.h"

#include <logging/log.h>
#include <protocols/quic/quic.h>
#include <protocols/registry/protocol_registry.h>

#include "protocols/udp/udp.h"
#include "protocols/tcp/tcp.h"
#include "uv.h"

uv_loop_t* event_loop;

ct_config_t global_config = {
  .cert_file_name = NULL,
  .key_file_name = NULL
};

int ct_initialize(const char *cert_file_name, const char *key_file_name) {
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
  log_info("Starting the libuv event event_loop...\n");

  // Run until there are no more waiting tasks
  uv_run(event_loop, UV_RUN_DEFAULT);
}
