#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "util/util.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

#define QUIC_PING_PORT 4433

class QuicPingTest : public CTapsGenericFixture {};

TEST_F(QuicPingTest, successfullyConnectsToQuicServer) {
  // --- Setup ---
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, QUIC_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, REQUIRE); // force QUIC

  ct_security_parameters_t security_parameters;
  ct_security_parameters_build(&security_parameters);
  char* alpn_strings = "simple-ping";
  ct_sec_param_set_property_string_array(&security_parameters, ALPN, &alpn_strings, 1);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, &security_parameters);
  ct_connection_t connection;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_on_connection_ready,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_receive_callbacks_t receive_req = { .receive_callback = close_on_message_received, .user_receive_context = &test_context };

  rc = ct_receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_EQ(test_context.messages->size(), 1);
  ASSERT_STREQ(test_context.messages->at(0)->content, "Pong: ping");

  ct_free_security_parameter_content(&security_parameters);
  ct_preconnection_free(&preconnection);
}
