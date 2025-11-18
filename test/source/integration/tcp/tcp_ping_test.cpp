#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
#include "util/util.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

#define TCP_PING_PORT 5006
#define INVALID_TCP_PORT 5007

extern "C" {
  int mark_connection_as_success_and_close(struct ct_connection_t* connection, void* udata) {
    log_info("ct_connection_t is ready");
    // close the connection
    bool* connection_succeeded = (bool*)udata;
    *connection_succeeded = true;
    ct_connection_close(connection);
    return 0;
  }

  int tcp_send_message_on_connection_ready(struct ct_connection_t* connection, void* udata) {
    log_info("ct_connection_t is ready, sending message");
    // --- Action ---
    ct_message_t message;

    ct_message_build_with_content(&message, "hello world", strlen("hello world") + 1);
    int rc = ct_send_message(connection, &message);
    EXPECT_EQ(rc, 0);

    ct_message_free_content(&message);

    return 0;
  }

  int on_establishment_error(struct ct_connection_t* connection, void* udata) {
    log_error("ct_connection_t error occurred");
    bool* connection_succeeded = (bool*)udata;
    *connection_succeeded = false;
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

TEST(TcpGenericTests, successfullyConnectsToTcpServer) {
  log_info("Starting test: successfullyConnectsToTcpServer");
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, ACTIVE_READ_BEFORE_SEND, REQUIRE);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  bool connection_succeeded = false;
  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = mark_connection_as_success_and_close,
    .user_data = &connection_succeeded,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_TRUE(connection_succeeded);
}

TEST(TcpGenericTests, connectionErrorCalledWhenNoServer) {
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, INVALID_TCP_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, ACTIVE_READ_BEFORE_SEND, REQUIRE);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  // Set to true, since only on_connection_error will set it to false
  bool connection_succeeded = true;
  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = mark_connection_as_success_and_close,
    .user_data = &connection_succeeded,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  ASSERT_FALSE(connection_succeeded);
  // assert state of connection is closed
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
}

TEST(TcpGenericTests, sendsSingleTcpMessage) {
  int rc;
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  ct_tp_set_sel_prop_preference(&transport_properties, ACTIVE_READ_BEFORE_SEND, REQUIRE);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  // Set to true, since only on_connection_error will set it to false
  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = tcp_send_message_on_connection_ready,
    .user_data = NULL,
  };

  rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_message_t* msg_received = nullptr;

  ct_receive_callbacks_t receive_req = { .receive_callback = on_msg_received, .user_data = &msg_received };

  rc = ct_receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // assert state of connection is closed
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_NE(msg_received, nullptr);
  ASSERT_STREQ((const char*)msg_received->content, "Pong: hello world");
  ct_message_free_content(msg_received);
}
