#include <gmock/gmock-matchers.h>
#include <thread>
#include <chrono>

#include "gtest/gtest.h"
#include "fff.h"

extern "C" {
#include "ctaps.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

#define TCP_PING_PORT 5006
#define INVALID_TCP_PORT_1 5050
#define INVALID_TCP_PORT_2 5051

extern "C" {
  // ct_callback_t that marks connection as successful
  int racing_test_on_ready(struct ct_connection_s* connection) {
    log_info("ct_connection_t succeeded via protocol: %s", ct_connection_get_protocol_name(connection));
    bool* connection_succeeded = (bool*)ct_connection_get_callback_context(connection);
    *connection_succeeded = true;
    ct_connection_close(connection);
    return 0;
  }

  // ct_callback_t that tracks failures
  int racing_test_on_establishment_error(struct ct_connection_s* connection) {
    if (connection == nullptr) {
      log_error("No successful connection could be created on establishment error");
      return 0;
    }
    log_error("ct_connection_t failed");
    bool* connection_succeeded = (bool*)ct_connection_get_callback_context(connection);
    *connection_succeeded = false;
    return 0;
  }

  // ct_callback_t that tracks which protocol succeeded
  int racing_test_on_ready_track_protocol(struct ct_connection_s* connection) {
    log_info("ct_connection_t succeeded via protocol: %s", ct_connection_get_protocol_name(connection));
    char** protocol_name = (char**)ct_connection_get_callback_context(connection);
    *protocol_name = strdup(ct_connection_get_protocol_name(connection));
    ct_connection_close(connection);
    return 0;
  }
}

/**
 * Test that racing works with multiple candidates where first succeeds
 */
TEST(CandidateRacingTests, FirstCandidateSucceeds) {
  // Setup
  ct_initialize();

  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);
  // Allocated with ct_transport_properties_new()

  // Don't require specific protocol - let racing choose
  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PREFER);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
  ASSERT_NE(preconnection, nullptr);

  bool connection_succeeded = false;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .user_connection_context = &connection_succeeded,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify
  ASSERT_TRUE(connection_succeeded);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

/**
 * Test that racing handles all candidates failing
 */
TEST(CandidateRacingTests, AllCandidatesFail) {
  // Setup
  ct_initialize();

  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, INVALID_TCP_PORT_1); // Invalid port

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);
  // Allocated with ct_transport_properties_new()

  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, REQUIRE);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
  ASSERT_NE(preconnection, nullptr);

  bool connection_succeeded = false;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .user_connection_context = &connection_succeeded,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify - all attempts should fail
  ASSERT_FALSE(connection_succeeded);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

/**
 * Test that racing respects transport property preferences
 * TCP should be preferred over UDP when reliability is required
 */
TEST(CandidateRacingTests, RespectsProtocolPreferences) {
  // Setup
  ct_initialize();

  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);
  // Allocated with ct_transport_properties_new()

  // Require reliability - should prefer TCP
  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, REQUIRE);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
  ASSERT_NE(preconnection, nullptr);

  char* winning_protocol = NULL;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready_track_protocol,
    .user_connection_context = &winning_protocol,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify - should use TCP due to reliability requirement
  ASSERT_NE(winning_protocol, nullptr);
  ASSERT_STREQ(winning_protocol, "TCP");

  free(winning_protocol);
  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

/**
 * Test that racing works with hostname resolution
 */
TEST(CandidateRacingTests, WorksWithHostnameResolution) {
  // Setup
  ct_initialize();

  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_hostname(remote_endpoint, "localhost");
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_MSG_BOUNDARIES, PROHIBIT); // force TCP

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
  ASSERT_NE(preconnection, nullptr);

  bool connection_succeeded = false;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .user_connection_context = &connection_succeeded,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify
  ASSERT_TRUE(connection_succeeded);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

/**
 * Test single candidate optimization (should not use racing overhead)
 */
TEST(CandidateRacingTests, SingleCandidateOptimization) {
  // Setup
  ct_initialize();

  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);
  // Allocated with ct_transport_properties_new()

  // Select TCP specifically - stream-based, reliable, no multistreaming
  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
  ct_tp_set_sel_prop_preference(transport_properties, MULTISTREAMING, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
  ASSERT_NE(preconnection, nullptr);

  bool connection_succeeded = false;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .user_connection_context = &connection_succeeded,
  };

  // Execute - should use single-candidate path
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify
  ASSERT_TRUE(connection_succeeded);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}
