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

#include <mutex>
#include <condition_variable>

#define TCP_PING_PORT 5006
#define INVALID_TCP_PORT 5007

extern "C" {
  int mark_connection_as_success_and_close(struct Connection* connection, void* udata) {
    log_info("Connection is ready");
    // close the connection
    bool* connection_succeeded = (bool*)udata;
    *connection_succeeded = true;
    connection_close(connection);
    return 0;
  }

  int tcp_send_message_on_connection_ready(struct Connection* connection, void* udata) {
    log_info("Connection is ready, sending message");
    // --- Action ---
    Message message;

    message_build_with_content(&message, "hello world", strlen("hello world") + 1);
    int rc = send_message(connection, &message);
    EXPECT_EQ(rc, 0);

    message_free_content(&message);

    return 0;
  }

  int on_establishment_error(struct Connection* connection, void* udata) {
    log_error("Connection error occurred");
    bool* connection_succeeded = (bool*)udata;
    *connection_succeeded = false;
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

TEST(TcpGenericTests, successfullyConnectsToTcpServer) {
  // --- Setup ---
  ctaps_initialize(NULL,NULL);
  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  TransportProperties transport_properties;

  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  tp_set_sel_prop_preference(&transport_properties, ACTIVE_READ_BEFORE_SEND, REQUIRE);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  Connection connection;

  bool connection_succeeded = false;
  ConnectionCallbacks connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = mark_connection_as_success_and_close,
    .user_data = &connection_succeeded,
  };

  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  ASSERT_TRUE(connection_succeeded);
}

TEST(TcpGenericTests, connectionErrorCalledWhenNoServer) {
  // --- Setup ---
  ctaps_initialize(NULL,NULL);
  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, INVALID_TCP_PORT);

  TransportProperties transport_properties;

  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  tp_set_sel_prop_preference(&transport_properties, ACTIVE_READ_BEFORE_SEND, REQUIRE);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  Connection connection;

  // Set to true, since only on_connection_error will set it to false
  bool connection_succeeded = true;
  ConnectionCallbacks connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = mark_connection_as_success_and_close,
    .user_data = &connection_succeeded,
  };

  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  ASSERT_FALSE(connection_succeeded);
  // assert state of connection is closed
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
}

TEST(TcpGenericTests, sendsSingleTcpMessage) {
  int rc;
  // --- Setup ---
  ctaps_initialize(NULL,NULL);
  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

  TransportProperties transport_properties;

  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  tp_set_sel_prop_preference(&transport_properties, ACTIVE_READ_BEFORE_SEND, REQUIRE);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  Connection connection;

  // Set to true, since only on_connection_error will set it to false
  ConnectionCallbacks connection_callbacks = {
    .establishment_error = on_establishment_error,
    .ready = tcp_send_message_on_connection_ready,
    .user_data = NULL,
  };

  rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  Message* msg_received = nullptr;

  ReceiveCallbacks receive_req = { .receive_callback = on_msg_received, .user_data = &msg_received };

  rc = receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // assert state of connection is closed
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_NE(msg_received, nullptr);
  ASSERT_STREQ((const char*)msg_received->content, "Pong: hello world");
  message_free_content(msg_received);
}
