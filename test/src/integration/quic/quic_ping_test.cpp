#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

class QuicPingTest : public CTapsGenericFixture {};

TEST_F(QuicPingTest, successfullyPingsQuicServer) {
  // --- Setup ---
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ASSERT_NE(remote_endpoint, nullptr);
  ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);

  ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_MSG_BOUNDARIES, REQUIRE);
  ct_tp_set_sel_prop_preference(transport_properties, MULTISTREAMING, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  char* alpn_strings = "simple-ping";
  ct_sec_param_set_property_string_array(security_parameters, ALPN, &alpn_strings, 1);

  ct_certificate_bundles_t* client_bundles = ct_certificate_bundles_new();
  ct_certificate_bundles_add_cert(client_bundles, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");
  ct_sec_param_set_property_certificate_bundles(security_parameters, CLIENT_CERTIFICATE, client_bundles);
  ct_certificate_bundles_free(client_bundles);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_sec_param_free(security_parameters);

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
  ASSERT_FALSE(ct_connection_used_0rtt(connection));

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

  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_MSG_BOUNDARIES, REQUIRE);
  ct_tp_set_sel_prop_preference(transport_properties, MULTISTREAMING, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  char* alpn_strings = "simple-ping";
  ct_sec_param_set_property_string_array(security_parameters, ALPN, &alpn_strings, 1);

  ct_certificate_bundles_t* client_bundles = ct_certificate_bundles_new();
  ct_certificate_bundles_add_cert(client_bundles, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");
  ct_sec_param_set_property_certificate_bundles(security_parameters, CLIENT_CERTIFICATE, client_bundles);
  ct_certificate_bundles_free(client_bundles);

  ct_sec_param_set_ticket_store_path(security_parameters, TEST_TICKET_STORE);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_sec_param_free(security_parameters);

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
  ASSERT_FALSE(ct_connection_used_0rtt(connection));

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
  ASSERT_TRUE(ct_connection_used_0rtt(connection));

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

  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_MSG_BOUNDARIES, REQUIRE);
  ct_tp_set_sel_prop_preference(transport_properties, MULTISTREAMING, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  char* alpn_strings = "simple-ping";
  ct_sec_param_set_property_string_array(security_parameters, ALPN, &alpn_strings, 1);

  ct_certificate_bundles_t* client_bundles = ct_certificate_bundles_new();
  ct_certificate_bundles_add_cert(client_bundles, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");
  ct_sec_param_set_property_certificate_bundles(security_parameters, CLIENT_CERTIFICATE, client_bundles);
  ct_certificate_bundles_free(client_bundles);

  ct_sec_param_set_ticket_store_path(security_parameters, TEST_TICKET_STORE);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_sec_param_free(security_parameters);

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
  ASSERT_FALSE(ct_connection_used_0rtt(connection));

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
  ASSERT_FALSE(ct_connection_used_0rtt(connection));

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

  ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_MSG_BOUNDARIES, REQUIRE);
  ct_tp_set_sel_prop_preference(transport_properties, MULTISTREAMING, REQUIRE); // force QUIC

  ct_security_parameters_t* security_parameters = ct_security_parameters_new();
  ASSERT_NE(security_parameters, nullptr);
  char* alpn_strings = "simple-ping";
  ct_sec_param_set_property_string_array(security_parameters, ALPN, &alpn_strings, 1);

  ct_certificate_bundles_t* client_bundles = ct_certificate_bundles_new();
  ct_certificate_bundles_add_cert(client_bundles, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");
  ct_sec_param_set_property_certificate_bundles(security_parameters, CLIENT_CERTIFICATE, client_bundles);
  ct_certificate_bundles_free(client_bundles);

  ct_sec_param_set_ticket_store_path(security_parameters, TEST_TICKET_STORE);

  ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, security_parameters);

  ASSERT_NE(preconnection, nullptr);
  ct_sec_param_free(security_parameters);

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
  ASSERT_FALSE(ct_connection_used_0rtt(connection));

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
  ASSERT_FALSE(ct_connection_used_0rtt(connection));

  ct_preconnection_free(preconnection);
}
