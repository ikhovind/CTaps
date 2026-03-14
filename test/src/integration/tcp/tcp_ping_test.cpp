#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
#include "fixtures/integration_fixture.h"
extern "C" {
#include "ctaps.h"
#include <logging/log.h>
}

#define TCP_PING_PORT 5006
#define INVALID_TCP_PORT 5007

DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(fake_message_sent, ct_connection_t*, ct_message_context_t*);

class TcpPingTest : public CTapsGenericFixture {
protected:
  void SetUp() override {
    CTapsGenericFixture::SetUp();
    RESET_FAKE(fake_message_sent);
    FFF_RESET_HISTORY();
  }
};

TEST_F(TcpPingTest, successfullyConnectsToTcpServer) {
  log_info("Starting test: successfullyConnectsToTcpServer");
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, PROHIBIT);
  ct_transport_properties_set_multistreaming(transport_properties, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
  ASSERT_NE(preconnection, nullptr);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = mark_connection_as_success_and_close,
    .sent = fake_message_sent,
    .per_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, &connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_TRUE(test_context.connection_succeeded);

  ASSERT_EQ(fake_message_sent_fake.call_count, 0);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(TcpPingTest, connectionErrorCalledWhenNoServer) {
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, INVALID_TCP_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, PROHIBIT);
  ct_transport_properties_set_multistreaming(transport_properties, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
  ASSERT_NE(preconnection, nullptr);

  // Set to true, since only on_connection_error will set it to false
  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = on_connection_ready,
    .sent = fake_message_sent,
    .per_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, &connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_FALSE(test_context.connection_succeeded);

  ASSERT_EQ(fake_message_sent_fake.call_count, 0);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(TcpPingTest, sendsSingleTcpMessage) {
  int rc;
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, PROHIBIT);
  ct_transport_properties_set_multistreaming(transport_properties, PROHIBIT);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);

  ct_message_t* msg_received = nullptr;
  ct_receive_callbacks_t receive_req = { .receive_callback = close_on_message_received, .per_receive_context = &test_context };


  // Set to true, since only on_connection_error will set it to false
  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_and_receive,
    .sent = fake_message_sent,
    .per_connection_context = &test_context,
  };

  rc = ct_preconnection_initiate(preconnection, &connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ct_connection_t* saved_connection = test_context.client_connections[0];
  // assert state of connection is closed
  ASSERT_TRUE(ct_connection_is_closed(saved_connection));
  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[saved_connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[saved_connection][0]->content, "Pong: ping");

  ASSERT_EQ(fake_message_sent_fake.call_count, 1);
  ASSERT_EQ(fake_message_sent_fake.arg0_val, saved_connection);
  ASSERT_NE(fake_message_sent_fake.arg1_val, nullptr);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}
