#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "util/util.h"
#include <logging/log.h>
}

#define UDP_PING_PORT 5005

extern "C" {
  int udp_send_message_on_connection_ready(struct ct_connection_s* connection) {
    log_info("ct_connection_t is ready, sending message");
    // --- Action ---
    ct_message_t message;

    ct_message_build_with_content(&message, "hello world", strlen("hello world") + 1);
    int rc = ct_send_message(connection, &message);
    EXPECT_EQ(rc, 0);

    ct_message_free_content(&message);

    return 0;
  }

  int udp_on_establishment_error(struct ct_connection_s* connection) {
    log_error("ct_connection_t error occurred");
    bool* connection_succeeded = (bool*)connection->connection_callbacks.user_connection_context;
    *connection_succeeded = false;
    return 0;
  }

  int udp_on_msg_received(struct ct_connection_s* connection, ct_message_t** received_message, ct_message_context_t* ctx) {
    log_info("ct_message_t received");
    // set user data to received message
    ct_message_t** output_addr = (ct_message_t**)ctx->user_receive_context;
    *output_addr = *received_message;

    ct_connection_close(connection);
    return 0;
  }
}

TEST(UdpGenericTests, sendsSingleUdpPacket) {
  log_info("Starting test: sendsSingleUdpPacket");
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = udp_on_establishment_error,
    .ready = udp_send_message_on_connection_ready,
    .user_connection_context = NULL,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_message_t* msg_received = nullptr;

  ct_receive_callbacks_t receive_req = { .receive_callback = udp_on_msg_received, .user_receive_context = &msg_received };

  rc = ct_receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // assert state of connection is closed
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_NE(msg_received, nullptr);
  ASSERT_STREQ((const char*)msg_received->content, "Pong: hello world");
  ct_message_free_content(msg_received);
}

// Context for tests that need to track multiple messages
struct UdpTestContext {
  std::vector<ct_message_t*> messages;
  size_t expected_count;
};

extern "C" {
  int udp_send_two_messages_on_ready(struct ct_connection_s* connection) {
    log_info("ct_connection_t is ready, sending two messages");

    ct_message_t message1;
    char* hello1 = "hello 1";
    ct_message_build_with_content(&message1, hello1, strlen(hello1) + 1);
    int rc = ct_send_message(connection, &message1);
    EXPECT_EQ(rc, 0);
    ct_message_free_content(&message1);

    ct_message_t message2;
    char* hello2 = "hello 2";
    ct_message_build_with_content(&message2, hello2, strlen(hello2) + 1);
    rc = ct_send_message(connection, &message2);
    EXPECT_EQ(rc, 0);
    ct_message_free_content(&message2);

    return 0;
  }

  int udp_on_msg_received_multiple(struct ct_connection_s* connection, ct_message_t** received_message, ct_message_context_t* ctx) {
    log_info("ct_message_t received (multiple test)");
    UdpTestContext* test_ctx = (UdpTestContext*)ctx->user_receive_context;

    test_ctx->messages.push_back(*received_message);

    log_info("We have now received %d out of %d expected messages", test_ctx->messages.size(), test_ctx->expected_count);
    // Close connection when we've received all expected messages
    if (test_ctx->messages.size() >= test_ctx->expected_count) {
      log_info("Received all expected messages, closing connection");
      ct_connection_close(connection);
    }

    return 0;
  }
}

TEST(UdpGenericTests, packetsAreReadInOrder) {
  log_info("Starting test: packetsAreReadInOrder");
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  UdpTestContext test_ctx;
  test_ctx.expected_count = 2;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = udp_on_establishment_error,
    .ready = udp_send_two_messages_on_ready,
    .user_connection_context = &test_ctx,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  // Post two receive requests
  ct_receive_callbacks_t receive_req = { .receive_callback = udp_on_msg_received_multiple, .user_receive_context = &test_ctx };
  rc = ct_receive_message(&connection, receive_req);
  ASSERT_EQ(rc, 0);

  rc = ct_receive_message(&connection, receive_req);
  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // --- Assertions ---
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_EQ(test_ctx.messages.size(), 2);
  EXPECT_STREQ((const char*)test_ctx.messages[0]->content, "Pong: hello 1");
  EXPECT_STREQ((const char*)test_ctx.messages[1]->content, "Pong: hello 2");

  // Clean up messages
  for (ct_message_t* msg : test_ctx.messages) {
    ct_message_free_content(msg);
  }
}

extern "C" {
  int udp_send_bytes_on_ready(struct ct_connection_s* connection) {
    log_info("ct_connection_t is ready, sending arbitrary bytes");

    ct_message_t message;
    char bytes_to_send[] = {0, 1, 2, 3, 4, 5};
    ct_message_build_with_content(&message, bytes_to_send, sizeof(bytes_to_send));

    int rc = ct_send_message(connection, &message);
    EXPECT_EQ(rc, 0);
    ct_message_free_content(&message);

    return 0;
  }
}

TEST(UdpGenericTests, canPingArbitraryBytes) {
  log_info("Starting test: canPingArbitraryBytes");
  // --- Setup ---
  ct_initialize(NULL,NULL);
  ct_remote_endpoint_t remote_endpoint;
  ct_remote_endpoint_build(&remote_endpoint);
  ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  ct_remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

  ct_transport_properties_t transport_properties;

  ct_transport_properties_build(&transport_properties);

  ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
  ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

  ct_preconnection_t preconnection;
  ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);
  ct_connection_t connection;

  ct_connection_callbacks_t connection_callbacks = {
    .establishment_error = udp_on_establishment_error,
    .ready = udp_send_bytes_on_ready,
    .user_connection_context = NULL,
  };

  int rc = ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ct_message_t* msg_received = nullptr;

  ct_receive_callbacks_t receive_req = { .receive_callback = udp_on_msg_received, .user_receive_context = &msg_received };

  rc = ct_receive_message(&connection, receive_req);

  ASSERT_EQ(rc, 0);

  ct_start_event_loop();

  // --- Assertions ---
  ASSERT_EQ(connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
  ASSERT_NE(msg_received, nullptr);

  char expected_output[] = {'P', 'o', 'n', 'g', ':', ' ', 0, 1, 2, 3, 4, 5};
  ASSERT_EQ(msg_received->length, sizeof(expected_output));
  EXPECT_EQ(memcmp(expected_output, msg_received->content, sizeof(expected_output)), 0);

  ct_message_free_content(msg_received);
}
