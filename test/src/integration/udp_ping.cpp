#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include <logging/log.h>
#include "fixtures/awaiting_fixture.cpp"
}

#define UDP_PING_PORT 5005

class UdpGenericTests : public CTapsGenericFixture {};

TEST_F(UdpGenericTests, sendsSingleUdpPacket) {
  log_info("Starting test: sendsSingleUdpPacket");
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
  ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);
  ct_tp_set_sel_prop_preference(transport_properties, CONGESTION_CONTROL, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
  ASSERT_NE(preconnection, nullptr);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_and_receive,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // assert state of connection is closed
  ASSERT_TRUE(ct_connection_is_closed(test_context.client_connections[0]));

  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[test_context.client_connections[0]].size(), 1);
  ASSERT_STREQ(per_connection_messages[test_context.client_connections[0]][0]->content, "Pong: ping");

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(UdpGenericTests, packetsAreReadInOrder) {
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
  ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);
  ct_tp_set_sel_prop_preference(transport_properties, CONGESTION_CONTROL, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
  ASSERT_NE(preconnection, nullptr);

  test_context.total_expected_messages = 2;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_two_messages_on_ready,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // --- Assertions ---
  ASSERT_TRUE(ct_connection_is_closed(test_context.client_connections[0]));
  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[test_context.client_connections[0]].size(), 2);
  EXPECT_STREQ(per_connection_messages[test_context.client_connections[0]][0]->content, "Pong: hello 1");
  EXPECT_STREQ(per_connection_messages[test_context.client_connections[0]][1]->content, "Pong: hello 2");

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(UdpGenericTests, canPingArbitraryBytes) {
  log_info("Starting test: canPingArbitraryBytes");
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
  ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);
  ct_tp_set_sel_prop_preference(transport_properties, CONGESTION_CONTROL, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
  ASSERT_NE(preconnection, nullptr);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_bytes_on_ready,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // --- Assertions ---
  ASSERT_TRUE(ct_connection_is_closed(test_context.client_connections[0]));

  char expected_output[] = {'P', 'o', 'n', 'g', ':', ' ', 0, 1, 2, 3, 4, 5};

  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[test_context.client_connections[0]].size(), 1);
  EXPECT_EQ(memcmp(per_connection_messages[test_context.client_connections[0]][0]->content, expected_output, sizeof(expected_output)), 0);
  EXPECT_EQ(per_connection_messages[test_context.client_connections[0]][0]->length, sizeof(expected_output));

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}
