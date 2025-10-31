#include "ctaps.h"

#include <logging/log.h>
#include <protocols/quic/quic.h>
#include <protocols/registry/protocol_registry.h>

#include "protocols/udp/udp.h"
#include "protocols/tcp/tcp.h"
#include "uv.h"

uv_loop_t* ctaps_event_loop;

CtapsConfig ctaps_global_config = {
  .cert_file_name = NULL,
  .key_file_name = NULL
};

int ctaps_initialize(const char *cert_file_name, const char *key_file_name) {
  ctaps_event_loop = malloc(sizeof(uv_loop_t));
  uv_loop_init(ctaps_event_loop);

  register_protocol(&udp_protocol_interface);
  register_protocol(&tcp_protocol_interface);
  register_protocol(&quic_protocol_interface);

  ctaps_global_config.cert_file_name = cert_file_name ? strdup(cert_file_name) : NULL;
  ctaps_global_config.key_file_name = key_file_name ? strdup(key_file_name) : NULL;
  log_trace("Set cert and key");
  log_trace("Set cert to: %s", ctaps_global_config.cert_file_name);
  log_trace("Set key to: %s", ctaps_global_config.key_file_name);
  return 0;
}

void ctaps_start_event_loop() {
  log_info("Starting the libuv event ctaps_event_loop...\n");

  // Run until there are no more waiting tasks
  uv_run(ctaps_event_loop, UV_RUN_DEFAULT);

  uv_loop_close(ctaps_event_loop);
}
