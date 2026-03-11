#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fixtures/integration_fixture.h"
extern "C" {
#include "fff.h"
#include "ctaps.h"
#include <logging/log.h>
}

DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(fake_message_sent, ct_connection_t*, ct_message_context_t*);

class UdpPingTests : public CTapsGenericFixture {
protected:
  void SetUp() override {
    CTapsGenericFixture::SetUp();
    RESET_FAKE(fake_message_sent);
    FFF_RESET_HISTORY();
  }
};

TEST_F(UdpPingTests, sendsSingleUdpPacketWithoutEarlySend) {
  log_info("Starting test: sendsSingleUdpPacket");
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, PROHIBIT);
  ct_transport_properties_set_preserve_order(transport_properties, PROHIBIT);
  ct_transport_properties_set_congestion_control(transport_properties, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
  ASSERT_NE(preconnection, nullptr);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_and_receive,
    .sent = fake_message_sent,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_TRUE(ct_connection_is_closed(test_context.client_connections[0]));

  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[test_context.client_connections[0]].size(), 1);
  ASSERT_STREQ(per_connection_messages[test_context.client_connections[0]][0]->content, "Pong: ping");

  ASSERT_EQ(fake_message_sent_fake.call_count, 1);
  ASSERT_EQ(fake_message_sent_fake.arg0_val, test_context.client_connections[0]);
  ASSERT_NE(fake_message_sent_fake.arg1_val, nullptr);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(UdpPingTests, packetsAreReadInOrder) {
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, PROHIBIT);
  ct_transport_properties_set_preserve_order(transport_properties, PROHIBIT);
  ct_transport_properties_set_congestion_control(transport_properties, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
  ASSERT_NE(preconnection, nullptr);

  test_context.total_expected_messages = 2;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_two_messages_on_ready,
    .sent = fake_message_sent,
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

  ASSERT_EQ(fake_message_sent_fake.call_count, 2);
  ASSERT_EQ(fake_message_sent_fake.arg0_history[0], test_context.client_connections[0]);
  ASSERT_EQ(fake_message_sent_fake.arg0_history[1], test_context.client_connections[0]);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(UdpPingTests, canPingArbitraryBytes) {
  log_info("Starting test: canPingArbitraryBytes");
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, PROHIBIT);
  ct_transport_properties_set_preserve_order(transport_properties, PROHIBIT);
  ct_transport_properties_set_congestion_control(transport_properties, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
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

TEST_F(UdpPingTests, sendsSingleUdpPacketWithInitiateWithSend) {
  log_info("Starting test: sendsSingleUdpPacketWithInitiateWithSend");
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, PROHIBIT);
  ct_transport_properties_set_preserve_order(transport_properties, PROHIBIT);
  ct_transport_properties_set_congestion_control(transport_properties, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
  ASSERT_NE(preconnection, nullptr);


  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = receive_on_ready,
    .sent = fake_message_sent,
    .user_connection_context = &test_context,
  };

  ct_message_t* message = ct_message_new_with_content("ping", strlen("ping") + 1);

  int rc = ct_preconnection_initiate_with_send(preconnection, connection_callbacks, message, NULL);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // assert state of connection is closed
  ASSERT_TRUE(ct_connection_is_closed(test_context.client_connections[0]));

  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[test_context.client_connections[0]].size(), 1);
  ASSERT_STREQ(per_connection_messages[test_context.client_connections[0]][0]->content, "Pong: ping");

  ASSERT_EQ(fake_message_sent_fake.call_count, 1);
  ASSERT_EQ(fake_message_sent_fake.arg0_val, test_context.client_connections[0]);
  ASSERT_NE(fake_message_sent_fake.arg1_val, nullptr);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
  ct_message_free(message);
}
