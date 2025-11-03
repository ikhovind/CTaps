#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
#include "security_parameters/security_parameters.h"
#include "util/util.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

#define QUIC_PING_PORT 4433

extern "C" {
  int quic_mark_connection_as_success_and_close(struct Connection* connection, void* udata) {
    log_info("Connection is ready");
    // close the connection
    bool* quic_connection_succeeded = (bool*)udata;
    *quic_connection_succeeded = true;
    connection_close(connection);
    return 0;
  }

  int receive_and_close_on_message_received(struct Connection* connection, Message** received_message, MessageContext* ctx, void* udata) {
    log_info("Message received");
    // set user data to received message
    Message** output_addr = (Message**)udata;
    *output_addr = *received_message;

    connection_close(connection);
    return 0;
  }

  int quic_send_message_on_connection_ready(struct Connection* connection, void* udata) {
    log_info("Connection is ready, sending message");
    // --- Action ---
    Message message;

    message_build_with_content(&message, "hello world", strlen("hello world") + 1);
    int rc = send_message(connection, &message);
    EXPECT_EQ(rc, 0);

    message_free_content(&message);

    return 0;
  }

  int quic_establishment_error(struct Connection* connection, void* udata) {
    log_error("Connection error occurred");
    bool* quic_connection_succeeded = (bool*)udata;
    *quic_connection_succeeded = false;
    return 0;
  }

  int on_msg_received(struct Connection* connection, Message** received_message, MessageContext* ctx, void* user_data) {
    log_info("Message received");
    // set user data to received message
    Message** output_addr = (Message**)user_data;
    *output_addr = *received_message;

    connection_close(connection);
    return 0;
  }
}

TEST(QuicGenericTests, successfullyConnectsToQuicServer) {
  // --- Setup ---
  int rc = ctaps_initialize(TEST_RESOURCE_DIR "/cert.pem",TEST_RESOURCE_DIR "/key.pem");
  ASSERT_EQ(rc, 0);
  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, QUIC_PING_PORT);

  TransportProperties transport_properties;

  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, REQUIRE); // force QUIC

  SecurityParameters security_parameters;
  security_parameters_build(&security_parameters);
  char* alpn_strings = "simple-ping";
  sec_param_set_property_string_array(&security_parameters, ALPN, &alpn_strings, 1);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, &security_parameters);
  Connection connection;

  bool quic_connection_succeeded = false;
  ConnectionCallbacks connection_callbacks = {
    .establishment_error = quic_establishment_error,
    .ready = quic_send_message_on_connection_ready,
    .user_data = &quic_connection_succeeded,
  };

  rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  Message* msg_received = nullptr;

  ReceiveCallbacks receive_req = { .receive_callback = on_msg_received, .user_data = &msg_received };

  rc = receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_NE(msg_received, nullptr);
  ASSERT_STREQ((const char*)msg_received->content, "Pong: hello world");
  message_free_all(msg_received);
  free_security_parameter_content(&security_parameters);
  preconnection_free(&preconnection);
}
