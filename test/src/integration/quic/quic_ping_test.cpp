#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "fixtures/integration_fixture.h"
#include <logging/log.h>
}

DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(fake_message_sent, ct_connection_t*, ct_message_context_t*);

class QuicPingTest : public CTapsGenericFixture {
protected:
  void SetUp() override {
    CTapsGenericFixture::SetUp();
    RESET_FAKE(fake_message_sent);
    FFF_RESET_HISTORY();
  }
};

TEST_F(QuicPingTest, successfullyPingsQuicServerWithout0Rtt) {
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
  ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  const char* alpn_strings = "simple-ping";
  ct_security_parameters_add_alpn(security_parameters, alpn_strings);
  ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_security_parameters_free(security_parameters);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_and_receive,
    .sent = fake_message_sent,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ct_connection_t* connection = test_context.client_connections[0];

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
  ASSERT_FALSE(ct_connection_sent_early_data(connection));

  ASSERT_EQ(fake_message_sent_fake.call_count, 1);
  ASSERT_EQ(fake_message_sent_fake.arg0_val, connection);
  ASSERT_NE(fake_message_sent_fake.arg1_val, nullptr);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(QuicPingTest, ConnectionFailsIfAlpnDoesNotMatch) {
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
  ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  const char* alpn_strings = "complicated-ping";
  ct_security_parameters_add_alpn(security_parameters, alpn_strings);

  ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_security_parameters_free(security_parameters);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_and_receive,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ct_start_event_loop();

  ASSERT_EQ(per_connection_messages.size(), 0);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(QuicPingTest, SuccessfullyPingsQuicServerEvenIfFirstAlpnDoesNotMatch) {
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
  ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  const char* alpn_strings[2] = { "non-matching-alpn", "simple-ping" };
  ct_security_parameters_add_alpn(security_parameters, alpn_strings[0]);
  ct_security_parameters_add_alpn(security_parameters, alpn_strings[1]);

  ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_security_parameters_free(security_parameters);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_and_receive,
    .sent = fake_message_sent,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ct_connection_t* connection = test_context.client_connections[0];

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
  ASSERT_FALSE(ct_connection_sent_early_data(connection));

  ASSERT_EQ(fake_message_sent_fake.call_count, 1);
  ASSERT_EQ(fake_message_sent_fake.arg0_val, connection);
  ASSERT_NE(fake_message_sent_fake.arg1_val, nullptr);

  ct_remote_endpoint_free(remote_endpoint);
  ct_preconnection_free(preconnection);
  ct_transport_properties_free(transport_properties);
}

TEST_F(QuicPingTest, successfullyPingsQuicServerWith0Rtt) {
  // 1st Connection to establish session ticket
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
  ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  const char* alpn_strings = "simple-ping";

  ct_security_parameters_add_alpn(security_parameters, alpn_strings);
  ct_security_parameters_set_server_name_identification(security_parameters, "localhost");
  ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");
  ct_security_parameters_set_ticket_store_path(security_parameters, TEST_CLIENT_TICKET_STORE);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_security_parameters_free(security_parameters);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_and_receive,
    .sent = fake_message_sent,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ct_connection_t* connection = test_context.client_connections[0];

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
  ASSERT_FALSE(ct_connection_sent_early_data(connection));

  ASSERT_EQ(fake_message_sent_fake.call_count, 1);
  ASSERT_EQ(fake_message_sent_fake.arg0_val, connection);
  ASSERT_NE(fake_message_sent_fake.arg1_val, nullptr);

  ct_remote_endpoint_free(remote_endpoint);
  ct_transport_properties_free(transport_properties);

  // --- 2nd Connection with 0-RTT ---
  //
  log_info("Starting 2nd connection with early data");

  ASSERT_NE(preconnection, nullptr);

  connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = receive_on_ready,
    .sent = fake_message_sent,
    .user_connection_context = &test_context,
  };

  ct_message_t* message = ct_message_new_with_content("ping", strlen("ping") + 1);
  ct_message_context_t* message_context = ct_message_context_new();
  ct_message_context_set_safely_replayable(message_context, true);

  rc = ct_preconnection_initiate_with_send(preconnection, connection_callbacks, message, message_context);
  ct_message_context_free(message_context);

  ct_message_free(message);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  connection = test_context.client_connections[1];

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_EQ(per_connection_messages.size(), 2);
  ASSERT_EQ(per_connection_messages[connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
  ASSERT_TRUE(ct_connection_sent_early_data(connection));

  ASSERT_EQ(fake_message_sent_fake.call_count, 2);
  ASSERT_EQ(fake_message_sent_fake.arg0_history[1], connection);
  ASSERT_NE(fake_message_sent_fake.arg1_history[1], nullptr);

  ct_preconnection_free(preconnection);
}

TEST_F(QuicPingTest, doesNotUse0rttWithNormalInitiate) {
  // 1st Connection to establish session ticket
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
  ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  const char* alpn_strings = "simple-ping";
  ct_security_parameters_add_alpn(security_parameters, alpn_strings);

  ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

  ct_security_parameters_set_ticket_store_path(security_parameters, TEST_CLIENT_TICKET_STORE);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_security_parameters_free(security_parameters);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_and_receive,
    .sent = fake_message_sent,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ct_connection_t* connection = test_context.client_connections[0];

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
  ASSERT_FALSE(ct_connection_sent_early_data(connection));

  ASSERT_EQ(fake_message_sent_fake.call_count, 1);
  ASSERT_NE(fake_message_sent_fake.arg0_val, nullptr);

  ct_remote_endpoint_free(remote_endpoint);
  ct_transport_properties_free(transport_properties);

  // --- 2nd Connection with 0-RTT ---

  rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  connection = test_context.client_connections[1];

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_EQ(per_connection_messages.size(), 2);
  ASSERT_EQ(per_connection_messages[connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
  ASSERT_FALSE(ct_connection_sent_early_data(connection));
  ASSERT_EQ(fake_message_sent_fake.call_count, 2);
  ASSERT_NE(fake_message_sent_fake.arg0_history[1], nullptr);

  ct_preconnection_free(preconnection);
}

TEST_F(QuicPingTest, doesNotUse0rttWhenReplayableNotSet) {
  // 1st Connection to establish session ticket
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_transport_properties_set_reliability(transport_properties, REQUIRE);
  ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
  ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  const char* alpn_strings = "simple-ping";
  ct_security_parameters_add_alpn(security_parameters, alpn_strings);

  ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

  ct_security_parameters_set_ticket_store_path(security_parameters, TEST_CLIENT_TICKET_STORE);

  ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_security_parameters_free(security_parameters);

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = send_message_and_receive,
    .user_connection_context = &test_context,
  };

  int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ct_connection_t* connection = test_context.client_connections[0];

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_EQ(per_connection_messages.size(), 1);
  ASSERT_EQ(per_connection_messages[connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
  ASSERT_FALSE(ct_connection_sent_early_data(connection));

  ct_remote_endpoint_free(remote_endpoint);
  ct_transport_properties_free(transport_properties);

  // --- 2nd Connection with 0-RTT ---

  ASSERT_NE(preconnection, nullptr);

  connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = receive_on_ready,
    .user_connection_context = &test_context,
  };

  ct_message_t* message = ct_message_new_with_content("ping", strlen("ping") + 1);
  ct_message_context_t* message_context = ct_message_context_new();
  ct_message_context_set_safely_replayable(message_context, false);

  rc = ct_preconnection_initiate_with_send(preconnection, connection_callbacks, message, message_context);
  ct_message_context_free(message_context);

  ct_message_free(message);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  connection = test_context.client_connections[1];

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_EQ(per_connection_messages.size(), 2);
  ASSERT_EQ(per_connection_messages[connection].size(), 1);
  ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
  ASSERT_FALSE(ct_connection_sent_early_data(connection));

  ct_preconnection_free(preconnection);
}
