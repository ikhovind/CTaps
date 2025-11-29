#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "util/util.h"
#include <logging/log.h>
}
#include "fixtures/awaiting_fixture.cpp"

#define TCP_PING_PORT 5006
#define INVALID_TCP_PORT 5007

class TcpGenericTests : public CTapsGenericFixture {};

TEST_F(TcpGenericTests, successfullyConnectsToTcpServer) {
  log_info("Starting test: successfullyConnectsToTcpServer");
  // --- Setup ---
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, PROHIBIT);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = mark_connection_as_success_and_close,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_TRUE(test_context.connection_succeeded);
}

TEST_F(TcpGenericTests, connectionErrorCalledWhenNoServer) {
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, INVALID_TCP_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, PROHIBIT);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  // Set to true, since only on_connection_error will set it to false
  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = mark_connection_as_success_and_close,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_FALSE(test_context.connection_succeeded);
  // assert state of connection is closed
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
}

TEST_F(TcpGenericTests, sendsSingleTcpMessage) {
  int rc;
  // --- Setup ---
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, PROHIBIT);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  // Set to true, since only on_connection_error will set it to false
  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_on_connection_ready,
    .user_connection_context = &test_context,
  };

  rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_message_t* msg_received = nullptr;

  ct_receive_callbacks_t receive_req = { .receive_callback = close_on_message_received, .user_receive_context = &test_context };

  rc = ct_receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // assert state of connection is closed
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[&connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[&connection][0]->content, "Pong: ping");
}
