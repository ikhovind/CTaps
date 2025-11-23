#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "util/util.h"
#include <logging/log.h>
#include "fixtures/awaiting_fixture.cpp"
}

#define UDP_PING_PORT 5005

class UdpGenericTests : public CTapsGenericFixture {};

TEST_F(UdpGenericTests, sendsSingleUdpPacket) {
  log_info("Starting test: sendsSingleUdpPacket");
  // --- Setup ---
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, CONGESTION_CONTROL, PROHIBIT);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
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

  // assert state of connection is closed
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);

  ASSERT_EQ(test_context.messages->size(), 1);
  ASSERT_STREQ(test_context.messages->at(0)->content, "Pong: ping");
}

TEST_F(UdpGenericTests, packetsAreReadInOrder) {
  log_info("Starting test: packetsAreReadInOrder");
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, CONGESTION_CONTROL, PROHIBIT);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_two_messages_on_ready,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  test_context.total_expected_messages = 2;

  // Post two receive requests
  ct_receive_callbacks_t receive_req = { .receive_callback = close_on_expected_num_messages_received, .user_receive_context = &test_context };
  rc = ct_receive_message(&connection, receive_req);
  ASSERT_EQ(rc, 0);

  rc = ct_receive_message(&connection, receive_req);
  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // --- Assertions ---
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_EQ(test_context.messages->size(), 2);
  EXPECT_STREQ(test_context.messages->at(0)->content, "Pong: hello 1");
  EXPECT_STREQ(test_context.messages->at(1)->content, "Pong: hello 2");
}

TEST_F(UdpGenericTests, canPingArbitraryBytes) {
  log_info("Starting test: canPingArbitraryBytes");
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, CONGESTION_CONTROL, PROHIBIT);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_bytes_on_ready,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_receive_callbacks_t receive_req = { .receive_callback = close_on_message_received, .user_receive_context = &test_context };

  rc = ct_receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // --- Assertions ---
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);

  char expected_output[] = {'P', 'o', 'n', 'g', ':', ' ', 0, 1, 2, 3, 4, 5};
  ASSERT_EQ(test_context.messages->size(), 1);
  EXPECT_EQ(memcmp(test_context.messages->at(0)->content, expected_output, sizeof(expected_output)), 0);
  EXPECT_EQ(test_context.messages->at(0)->length, sizeof(expected_output));
}
