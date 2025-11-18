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
  int quic_mark_connection_as_success_and_close(struct ct_connection_t* connection, void* udata) {
    log_info("ct_connection_t is ready");
    // close the connection
    bool* quic_connection_succeeded = (bool*)udata;
    *quic_connection_succeeded = true;
    ct_connection_close(connection);
    return 0;
  }

  int receive_and_close_on_message_received(struct ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* ctx, void* udata) {
    log_info("ct_message_t received");
    // set user data to received message
    ct_message_t** output_addr = (ct_message_t**)udata;
    *output_addr = *received_message;

    ct_connection_close(connection);
    return 0;
  }

  int quic_send_message_on_connection_ready(struct ct_connection_t* connection, void* udata) {
    log_info("ct_connection_t is ready, sending message");
    // --- Action ---
    ct_message_t message;

    ct_message_build_with_content(&message, "hello world", strlen("hello world") + 1);
    int rc = ct_send_message(connection, &message);
    EXPECT_EQ(rc, 0);

    ct_message_free_content(&message);

    return 0;
  }

  int quic_establishment_error(struct ct_connection_t* connection, void* udata) {
    log_error("ct_connection_t error occurred");
    bool* quic_connection_succeeded = (bool*)udata;
    *quic_connection_succeeded = false;
    return 0;
  }

  int on_msg_received(struct ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* ctx, void* user_data) {
    log_info("ct_message_t received");
    // set user data to received message
    ct_message_t** output_addr = (ct_message_t**)user_data;
    *output_addr = *received_message;

    ct_connection_close(connection);
    return 0;
  }
}

TEST(QuicGenericTests, successfullyConnectsToQuicServer) {
  // --- Setup ---
  int rc = ct_initialize(TEST_RESOURCE_DIR "/cert.pem",TEST_RESOURCE_DIR "/key.pem");
  ASSERT_EQ(rc, 0);
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

  bool quic_connection_succeeded = false;
  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = quic_establishment_error,
    .ready = quic_send_message_on_connection_ready,
    .user_data = &quic_connection_succeeded,
  };

  rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_message_t* msg_received = nullptr;

  ct_receive_callbacks_t receive_req = { .receive_callback = on_msg_received, .user_data = &msg_received };

  rc = ct_receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_NE(msg_received, nullptr);
  ASSERT_STREQ((const char*)msg_received->content, "Pong: hello world");
  ct_message_free_all(msg_received);
  ct_free_security_parameter_content(&security_parameters);
  ct_preconnection_free(&preconnection);
}
