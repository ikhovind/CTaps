#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
#include "util/util.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

#include <mutex>
#include <condition_variable>

extern "C" {
  int on_connection_ready(struct Connection* connection, void* udata) {
    log_info("Connection is ready");
    // close the connection
    connection_close(connection);
  }

  int on_connection_error(struct Connection* connection, void* udata) {
    log_error("Connection error occurred");
    return 0;
  }
}

TEST(TcpGenericTests, sendsSingleTcpPacket) {
  // --- Setup ---
  ctaps_initialize();
  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, 5006);

  TransportProperties transport_properties;

  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);
  Connection connection;

  ConnectionCallbacks connection_callbacks = {
    .connection_error = on_connection_error,
    .ready = on_connection_ready,
  };

  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();
  //awaiter.await(2);
  //ASSERT_EQ(awaiter.get_signal_count(), 2);
}
