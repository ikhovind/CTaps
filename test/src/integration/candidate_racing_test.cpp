#include <gmock/gmock-matchers.h>
#include <thread>
#include <chrono>

#include "gtest/gtest.h"
#include "fff.h"

extern "C" {
#include "ctaps.h"
#include "fixtures/integration_fixture.h"
#include <logging/log.h>
}

#define TCP_PING_PORT 5006
#define INVALID_TCP_PORT_1 5050
#define INVALID_TCP_PORT_2 5051
DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(fake_on_ready_counter);
FAKE_VOID_FUNC(fake_on_establishment_error_counter);

extern "C" {
  typedef struct ct_candidate_racing_test_context_s {
    ct_connection_t* captured_connection;
    bool connection_succeeded;
    char* successful_protocol;
  } ct_candidate_racing_test_context_t;

  // ct_callback_t that marks connection as successful
  int racing_test_on_ready(struct ct_connection_s* connection) {
    fake_on_ready_counter();
    log_info("ct_connection_t succeeded via protocol: %s", ct_connection_get_protocol_name(connection));
    ct_candidate_racing_test_context_t* context = (ct_candidate_racing_test_context_t*)ct_connection_get_callback_context(connection);
    context->connection_succeeded = true;
    ct_connection_close(connection);
    return 0;
  }

  int capture_connection_on_close(struct ct_connection_s* connection) {
    fake_on_ready_counter();
    ct_candidate_racing_test_context_t* test_context = (ct_candidate_racing_test_context_t*)ct_connection_get_callback_context(connection);
    test_context->captured_connection = connection;
    ct_connection_close(connection);
    return 0;
  }

  // ct_callback_t that tracks failures
  int racing_test_on_establishment_error(struct ct_connection_s* connection) {
    fake_on_establishment_error_counter();
    if (connection == nullptr) {
      log_error("No successful connection could be created on establishment error");
      return 0;
    }
    log_error("ct_connection_t failed");
    ct_candidate_racing_test_context_t* context = (ct_candidate_racing_test_context_t*)ct_connection_get_callback_context(connection);
    context->connection_succeeded = false;
    return 0;
  }

  // ct_callback_t that tracks which protocol succeeded
  int racing_test_on_ready_track_protocol(struct ct_connection_s* connection) {
    log_info("ct_connection_t succeeded via protocol: %s", ct_connection_get_protocol_name(connection));
    ct_candidate_racing_test_context_t* ctx = (ct_candidate_racing_test_context_t*)ct_connection_get_callback_context(connection);
    ctx->successful_protocol = strdup(ct_connection_get_protocol_name(connection));
    ct_connection_close(connection);
    return 0;
  }

  int free_on_close(ct_connection_t* connection) {
    log_debug("Connection %s was closed, freeing resources", connection->uuid);
    ct_connection_free(connection);
    return 0;
  }


}

class CandidateRacingTests : public CTapsGenericFixture {
protected:
    void SetUp() override {
        CTapsGenericFixture::SetUp();
        // Reset all mock data before each test
        FFF_RESET_HISTORY();
        RESET_FAKE(fake_on_ready_counter);
        RESET_FAKE(fake_on_establishment_error_counter);
    }

    
    void TearDown() override {
      CTapsGenericFixture::TearDown();
      if (racing_context.successful_protocol) {
        free(racing_context.successful_protocol);
      }
    }

    ct_candidate_racing_test_context_t racing_context = {
      .captured_connection = nullptr,
      .connection_succeeded = false,
      .successful_protocol = nullptr,
    };
};

/**
 * Test that racing works with multiple candidates where first succeeds
 */
TEST_F(CandidateRacingTests, FirstCandidateSucceeds) {
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();

  // Don't require specific protocol - let racing choose
  ct_transport_properties_set_reliability(transport_properties, PREFER);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .closed = free_on_close,
    .user_connection_context = &racing_context,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify
  ASSERT_TRUE(racing_context.connection_succeeded);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(CandidateRacingTests, connectionContainsSeveralRemotes) {
  ct_remote_endpoint_t remotes[2] = {0};
  ct_remote_endpoint_with_ipv4(&remotes[0], inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remotes[0], TCP_PING_PORT);

  ct_remote_endpoint_with_ipv4(&remotes[1], inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remotes[1], UDP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();

  ct_transport_properties_set_reliability(transport_properties, PREFER);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remotes, 2, transport_properties,NULL);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .closed = capture_connection_on_close,
    .user_connection_context = &racing_context,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_NE(racing_context.captured_connection, nullptr);
  // Verify
  EXPECT_TRUE(racing_context.connection_succeeded);
  EXPECT_EQ(racing_context.captured_connection->num_remote_endpoints, 2);

  ct_remote_endpoint_free_content(&remotes[0]);
  ct_remote_endpoint_free_content(&remotes[1]);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
  ct_connection_free(racing_context.captured_connection);
}

TEST_F(CandidateRacingTests, connectionContainsSeveralLocals) {
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
  ct_local_endpoint_with_interface(local_endpoint, "any");

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();

  ct_transport_properties_set_reliability(transport_properties, PREFER);

  ct_preconnection_t* preconnection = ct_preconnection_new(
    local_endpoint,
    1,
    remote_endpoint,
    1,
    transport_properties,
    NULL);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .closed = capture_connection_on_close,
    .user_connection_context = &racing_context,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_NE(racing_context.captured_connection, nullptr);
  EXPECT_NE(racing_context.captured_connection->all_local_endpoints, nullptr);
  EXPECT_GT(racing_context.captured_connection->num_local_endpoints, 1);
  // Verify
  EXPECT_TRUE(racing_context.connection_succeeded);

  ct_local_endpoint_free(local_endpoint);
  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
  ct_connection_free(racing_context.captured_connection);
}

TEST_F(CandidateRacingTests, AllCandidatesFail) {
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, INVALID_TCP_PORT_1); // Invalid port

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();

  ct_transport_properties_set_reliability(transport_properties, NO_PREFERENCE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, NO_PREFERENCE);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .closed = free_on_close,
    .user_connection_context = &racing_context,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_FALSE(racing_context.connection_succeeded);
  ASSERT_EQ(fake_on_ready_counter_fake.call_count, 0);
  ASSERT_EQ(fake_on_establishment_error_counter_fake.call_count, 1);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

/**
 * Test that racing respects transport property preferences
 * TCP should be preferred over UDP when reliability is required
 */
TEST_F(CandidateRacingTests, RespectsProtocolPreferences) {
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);
  // Allocated with ct_transport_properties_new()

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
  ASSERT_NE(preconnection, nullptr);


  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready_track_protocol,
    .closed = free_on_close,
    .user_connection_context = &racing_context,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify - should use TCP due to reliability requirement
  ASSERT_NE(racing_context.successful_protocol, nullptr);
  ASSERT_STREQ(racing_context.successful_protocol, "TCP");

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(CandidateRacingTests, WorksWithHostnameResolution) {
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_hostname(remote_endpoint, "localhost");
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, PROHIBIT); // force TCP

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
  ASSERT_NE(preconnection, nullptr);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .closed = free_on_close,
    .user_connection_context = &racing_context,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify
  ASSERT_TRUE(racing_context.connection_succeeded);
  ASSERT_EQ(fake_on_ready_counter_fake.call_count, 1);
  ASSERT_EQ(fake_on_establishment_error_counter_fake.call_count, 0);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

/**
 * Test single candidate optimization (should not use racing overhead)
 */
TEST_F(CandidateRacingTests, SingleCandidateOptimization) {
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();

  // Select TCP specifically - stream-based, reliable, no multistreaming
  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, PROHIBIT);
  ct_transport_properties_set_multistreaming(transport_properties, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .closed = free_on_close,
    .user_connection_context = &racing_context,
  };

  // Execute - should use single-candidate path
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify
  ASSERT_TRUE(racing_context.connection_succeeded);
  ASSERT_EQ(fake_on_ready_counter_fake.call_count, 1);
  ASSERT_EQ(fake_on_establishment_error_counter_fake.call_count, 0);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(CandidateRacingTests, HandlesNoCandidates) {
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT); // Invalid port

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();

  ct_transport_properties_set_reliability(transport_properties, PROHIBIT);
  ct_transport_properties_set_multistreaming(transport_properties, REQUIRE);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);

  ct_candidate_racing_test_context_t test_context = {
    .captured_connection = nullptr,
    .connection_succeeded = false,
    .successful_protocol = nullptr,
  };

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = racing_test_on_establishment_error,
    .ready = racing_test_on_ready,
    .closed = free_on_close,
    .user_connection_context = &test_context,
  };

  // Execute
  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify - all attempts should fail
  ASSERT_FALSE(test_context.connection_succeeded);
  ASSERT_EQ(fake_on_ready_counter_fake.call_count, 0);
  ASSERT_EQ(fake_on_establishment_error_counter_fake.call_count, 1);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}
