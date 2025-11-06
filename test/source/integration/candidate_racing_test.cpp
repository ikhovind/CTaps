#include <gmock/gmock-matchers.h>
#include <thread>
#include <chrono>

#include "gtest/gtest.h"
#include "fff.h"

extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

#define TCP_PING_PORT 5006
#define INVALID_TCP_PORT_1 5050
#define INVALID_TCP_PORT_2 5051

extern "C" {
  // Callback that marks connection as successful
  int racing_test_on_ready(struct Connection* connection, void* udata) {
    log_info("Connection succeeded via protocol: %s", connection->protocol.name);
    bool* connection_succeeded = (bool*)udata;
    *connection_succeeded = true;
    connection_close(connection);
    return 0;
  }

  // Callback that tracks failures
  int racing_test_on_establishment_error(struct Connection* connection, void* udata) {
    log_error("Connection failed");
    bool* connection_succeeded = (bool*)udata;
    *connection_succeeded = false;
    return 0;
  }

  // Callback that tracks which protocol succeeded
  int racing_test_on_ready_track_protocol(struct Connection* connection, void* udata) {
    log_info("Connection succeeded via protocol: %s", connection->protocol.name);
    char** protocol_name = (char**)udata;
    *protocol_name = strdup(connection->protocol.name);
    connection_close(connection);
    return 0;
  }
}

/**
 * Test that racing works with multiple candidates where first succeeds
 */
TEST(CandidateRacingTests, FirstCandidateSucceeds) {
  // Setup
  ctaps_initialize(NULL, NULL);

  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  TransportProperties transport_properties;
  transport_properties_build(&transport_properties);

  // Don't require specific protocol - let racing choose
  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PREFER);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

  Connection connection;
  bool connection_succeeded = false;

  ConnectionCallbacks connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .user_data = &connection_succeeded,
  };

  // Execute
  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // Verify
  ASSERT_TRUE(connection_succeeded);

  preconnection_free(&preconnection);
}

/**
 * Test that racing handles all candidates failing
 */
TEST(CandidateRacingTests, AllCandidatesFail) {
  // Setup
  ctaps_initialize(NULL, NULL);

  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, INVALID_TCP_PORT_1); // Invalid port

  TransportProperties transport_properties;
  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

  Connection connection;
  bool connection_succeeded = true; // Start as true, should be set to false

  ConnectionCallbacks connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .user_data = &connection_succeeded,
  };

  // Execute
  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // Verify - all attempts should fail
  ASSERT_FALSE(connection_succeeded);

  preconnection_free(&preconnection);
}

/**
 * Test that racing respects transport property preferences
 * TCP should be preferred over UDP when reliability is required
 */
TEST(CandidateRacingTests, RespectsProtocolPreferences) {
  // Setup
  ctaps_initialize(NULL, NULL);

  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  TransportProperties transport_properties;
  transport_properties_build(&transport_properties);

  // Require reliability - should prefer TCP
  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

  Connection connection;
  char* winning_protocol = NULL;

  ConnectionCallbacks connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready_track_protocol,
    .user_data = &winning_protocol,
  };

  // Execute
  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // Verify - should use TCP due to reliability requirement
  ASSERT_NE(winning_protocol, nullptr);
  ASSERT_STREQ(winning_protocol, "TCP");

  free(winning_protocol);
  preconnection_free(&preconnection);
}

/**
 * Test that racing works with hostname resolution
 */
TEST(CandidateRacingTests, WorksWithHostnameResolution) {
  // Setup
  ctaps_initialize(NULL, NULL);

  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_hostname(&remote_endpoint, "localhost");
  remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  TransportProperties transport_properties;
  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

  Connection connection;
  bool connection_succeeded = false;

  ConnectionCallbacks connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .user_data = &connection_succeeded,
  };

  // Execute
  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // Verify
  ASSERT_TRUE(connection_succeeded);

  preconnection_free(&preconnection);
}

/**
 * Test single candidate optimization (should not use racing overhead)
 */
TEST(CandidateRacingTests, SingleCandidateOptimization) {
  // Setup
  ctaps_initialize(NULL, NULL);

  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  TransportProperties transport_properties;
  transport_properties_build(&transport_properties);

  // Require both reliability and message boundaries - should only match one protocol
  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  tp_set_sel_prop_preference(&transport_properties, PRESERVE_MSG_BOUNDARIES, REQUIRE);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

  Connection connection;
  bool connection_succeeded = false;

  ConnectionCallbacks connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .user_data = &connection_succeeded,
  };

  // Execute - should use single-candidate path
  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // Verify
  ASSERT_TRUE(connection_succeeded);

  preconnection_free(&preconnection);
}

/**
 * Test that connection can send and receive data after racing completes
 */
TEST(CandidateRacingTests, ConnectionUsableAfterRacing) {
  // Setup
  ctaps_initialize(NULL, NULL);

  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  TransportProperties transport_properties;
  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

  Connection connection;
  bool connection_ready = false;

  // Callback that sends a message when connection is ready
  auto send_on_ready = [](struct Connection* conn, void* udata) -> int {
    log_info("Connection ready, sending test message");

    Message message;
    const char* test_data = "racing_test";
    message_build_with_content(&message, test_data, strlen(test_data) + 1);

    int rc = send_message(conn, &message);
    message_free_content(&message);

    if (rc == 0) {
      bool* ready_flag = (bool*)udata;
      *ready_flag = true;
    }

    connection_close(conn);
    return 0;
  };

  ConnectionCallbacks connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = send_on_ready,
    .user_data = &connection_ready,
  };

  // Execute
  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // Verify - connection should be usable for sending data
  ASSERT_TRUE(connection_ready);

  preconnection_free(&preconnection);
}
