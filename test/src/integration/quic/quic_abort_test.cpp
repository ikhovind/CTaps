#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
#include <picoquic.h>
}

DEFINE_FFF_GLOBALS;

#define QUIC_ABORT_PORT 4433

// Mock picoquic functions
FAKE_VALUE_FUNC(int, faked_picoquic_reset_stream, picoquic_cnx_t*, uint64_t, uint64_t);
FAKE_VOID_FUNC(faked_picoquic_close_immediate, picoquic_cnx_t*);

// Declare the real implementations (linker renames them with --wrap)
extern "C" {
  int __real_picoquic_reset_stream(picoquic_cnx_t* cnx, uint64_t stream_id, uint64_t error_code);
  void __real_picoquic_close_immediate(picoquic_cnx_t* cnx);

  int __wrap_picoquic_reset_stream(picoquic_cnx_t* cnx, uint64_t stream_id, uint64_t error_code) {
    log_info("MOCK: picoquic_reset_stream called with stream_id=%llu", (unsigned long long)stream_id);
    faked_picoquic_reset_stream(cnx, stream_id, error_code);
    return __real_picoquic_reset_stream(cnx, stream_id, error_code);
  }

  void __wrap_picoquic_close_immediate(picoquic_cnx_t* cnx) {
    log_info("MOCK: picoquic_close_immediate called");
    faked_picoquic_close_immediate(cnx);
    __real_picoquic_close_immediate(cnx);
  }
}

class QuicAbortTest : public CTapsGenericFixture {
protected:
  void SetUp() override {
    CTapsGenericFixture::SetUp();
    RESET_FAKE(faked_picoquic_reset_stream);
    RESET_FAKE(faked_picoquic_close_immediate);
    FFF_RESET_HISTORY();
  }
};

// Test Case 1: Single connection abort - should call picoquic_close_immediate
TEST_F(QuicAbortTest, singleConnectionAbortCallsCloseImmediate) {
  // Setup QUIC connection
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, QUIC_ABORT_PORT);

  ct_transport_properties_t transport_properties;
  ct_transport_properties_build(&transport_properties);
  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_MSG_BOUNDARIES, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, REQUIRE); // Force QUIC

  ct_security_parameters_t security_parameters;
  ct_security_parameters_build(&security_parameters);
  char* alpn_strings = "simple-ping";
  ct_sec_param_set_property_string_array(&security_parameters, ALPN, &alpn_strings, 1);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, &security_parameters);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = abort_on_ready,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(&preconnection, connection_callbacks);
  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify: picoquic_close_immediate was called (single stream abort)
  ASSERT_GE(faked_picoquic_close_immediate_fake.call_count, 1)
    << "picoquic_close_immediate should be called for single connection abort";

  // Verify: picoquic_reset_stream was NOT called (not multi-stream)
  ASSERT_EQ(faked_picoquic_reset_stream_fake.call_count, 0)
    << "picoquic_reset_stream should NOT be called for single connection abort";

  for (const auto& conn : test_context.client_connections) {
    ASSERT_TRUE(ct_connection_is_closed(conn)) << "Connection should be closed after abort";
  }

  ct_free_security_parameter_content(&security_parameters);
  ct_preconnection_free(&preconnection);
}

// Test Case 2: Multi-stream abort - should call picoquic_reset_stream
TEST_F(QuicAbortTest, multiStreamAbortCallsResetStream) {
  // Setup QUIC connection
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, QUIC_ABORT_PORT);

  ct_transport_properties_t transport_properties;
  ct_transport_properties_build(&transport_properties);
  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_MSG_BOUNDARIES, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, REQUIRE); // Force QUIC

  ct_security_parameters_t security_parameters;
  ct_security_parameters_build(&security_parameters);
  char* alpn_strings = "simple-ping";
  ct_sec_param_set_property_string_array(&security_parameters, ALPN, &alpn_strings, 1);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, &security_parameters);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = clone_and_abort_on_ready,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(&preconnection, connection_callbacks);
  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // Verify: We should have 2 connections (original + clone)
  ASSERT_EQ(test_context.client_connections.size(), 2)
    << "Should have original connection + cloned connection";

  ASSERT_EQ(faked_picoquic_reset_stream_fake.call_count, 1)
    << "picoquic_reset_stream should be called for multi-stream abort";

  // After resetting the first stream, the last connection will be closed as a single connection
  ASSERT_EQ(faked_picoquic_close_immediate_fake.call_count, 1)
    << "picoquic_close_immediate should be called for single connection abort";

  for (const auto& conn : test_context.client_connections) {
    ASSERT_TRUE(ct_connection_is_closed(conn)) << "Connection should be closed after abort";
  }

  ct_free_security_parameter_content(&security_parameters);
  ct_preconnection_free(&preconnection);
}
