#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
#include "util/util.h"
#include <logging/log.h>
}

#define UDP_PING_PORT 5005

extern "C" {
  int udp_send_message_on_connection_ready(struct Connection* connection, void* udata) {
    log_info("Connection is ready, sending message");
    // --- Action ---
    Message message;

    message_build_with_content(&message, "hello world", strlen("hello world") + 1);
    int rc = send_message(connection, &message);
    EXPECT_EQ(rc, 0);

    message_free_content(&message);

    return 0;
  }

  int udp_on_establishment_error(struct Connection* connection, void* udata) {
    log_error("Connection error occurred");
    bool* connection_succeeded = (bool*)udata;
    *connection_succeeded = false;
    return 0;
  }

  int udp_on_msg_received(struct Connection* connection, Message** received_message, MessageContext* ctx, void* user_data) {
    log_info("Message received");
    // set user data to received message
    Message** output_addr = (Message**)user_data;
    *output_addr = *received_message;

    connection_close(connection);
    return 0;
  }
}

TEST(UdpGenericTests, sendsSingleUdpPacket) {
  log_info("Starting test: sendsSingleUdpPacket");
  // --- Setup ---
  ctaps_initialize(NULL,NULL);
  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

  TransportProperties transport_properties;

  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
  tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  Connection connection;

  ConnectionCallbacks connection_callbacks = {
    .establishment_error = udp_on_establishment_error,
    .ready = udp_send_message_on_connection_ready,
    .user_data = NULL,
  };

  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  Message* msg_received = nullptr;

  ReceiveCallbacks receive_req = { .receive_callback = udp_on_msg_received, .user_data = &msg_received };

  rc = receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // assert state of connection is closed
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_NE(msg_received, nullptr);
  ASSERT_STREQ((const char*)msg_received->content, "Pong: hello world");
  message_free_content(msg_received);
}

// Context for tests that need to track multiple messages
struct UdpTestContext {
  std::vector<Message*> messages;
  size_t expected_count;
};

extern "C" {
  int udp_send_two_messages_on_ready(struct Connection* connection, void* udata) {
    log_info("Connection is ready, sending two messages");

    Message message1;
    char* hello1 = "hello 1";
    message_build_with_content(&message1, hello1, strlen(hello1) + 1);
    int rc = send_message(connection, &message1);
    EXPECT_EQ(rc, 0);
    message_free_content(&message1);

    Message message2;
    char* hello2 = "hello 2";
    message_build_with_content(&message2, hello2, strlen(hello2) + 1);
    rc = send_message(connection, &message2);
    EXPECT_EQ(rc, 0);
    message_free_content(&message2);

    return 0;
  }

  int udp_on_msg_received_multiple(struct Connection* connection, Message** received_message, MessageContext* ctx, void* user_data) {
    log_info("Message received (multiple test)");
    UdpTestContext* test_ctx = (UdpTestContext*)user_data;

    test_ctx->messages.push_back(*received_message);

    log_info("We have now received %d out of %d expected messages", test_ctx->messages.size(), test_ctx->expected_count);
    // Close connection when we've received all expected messages
    if (test_ctx->messages.size() >= test_ctx->expected_count) {
      log_info("Received all expected messages, closing connection");
      connection_close(connection);
    }

    return 0;
  }
}

TEST(UdpGenericTests, packetsAreReadInOrder) {
  log_info("Starting test: packetsAreReadInOrder");
  // --- Setup ---
  ctaps_initialize(NULL,NULL);
  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

  TransportProperties transport_properties;

  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
  tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  Connection connection;

  UdpTestContext test_ctx;
  test_ctx.expected_count = 2;

  ConnectionCallbacks connection_callbacks = {
    .establishment_error = udp_on_establishment_error,
    .ready = udp_send_two_messages_on_ready,
    .user_data = &test_ctx,
  };

  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  // Post two receive requests
  ReceiveCallbacks receive_req = { .receive_callback = udp_on_msg_received_multiple, .user_data = &test_ctx };
  rc = receive_message(&connection, receive_req);
  ASSERT_EQ(rc, 0);

  rc = receive_message(&connection, receive_req);
  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // --- Assertions ---
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_EQ(test_ctx.messages.size(), 2);
  EXPECT_STREQ((const char*)test_ctx.messages[0]->content, "Pong: hello 1");
  EXPECT_STREQ((const char*)test_ctx.messages[1]->content, "Pong: hello 2");

  // Clean up messages
  for (Message* msg : test_ctx.messages) {
    message_free_content(msg);
  }
}

extern "C" {
  int udp_send_bytes_on_ready(struct Connection* connection, void* udata) {
    log_info("Connection is ready, sending arbitrary bytes");

    Message message;
    char bytes_to_send[] = {0, 1, 2, 3, 4, 5};
    message_build_with_content(&message, bytes_to_send, sizeof(bytes_to_send));

    int rc = send_message(connection, &message);
    EXPECT_EQ(rc, 0);
    message_free_content(&message);

    return 0;
  }
}

TEST(UdpGenericTests, canPingArbitraryBytes) {
  log_info("Starting test: canPingArbitraryBytes");
  // --- Setup ---
  ctaps_initialize(NULL,NULL);
  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

  TransportProperties transport_properties;

  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
  tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  Connection connection;

  ConnectionCallbacks connection_callbacks = {
    .establishment_error = udp_on_establishment_error,
    .ready = udp_send_bytes_on_ready,
    .user_data = NULL,
  };

  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  Message* msg_received = nullptr;

  ReceiveCallbacks receive_req = { .receive_callback = udp_on_msg_received, .user_data = &msg_received };

  rc = receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  // --- Assertions ---
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_NE(msg_received, nullptr);

  char expected_output[] = {'P', 'o', 'n', 'g', ':', ' ', 0, 1, 2, 3, 4, 5};
  ASSERT_EQ(msg_received->length, sizeof(expected_output));
  EXPECT_EQ(memcmp(expected_output, msg_received->content, sizeof(expected_output)), 0);

  message_free_content(msg_received);
}
