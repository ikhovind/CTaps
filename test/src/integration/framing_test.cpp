#include <gmock/gmock-matchers.h>
#include "gtest/gtest.h"

extern "C" {
#include "ctaps.h"
#include "util/util.h"
#include "fixtures/awaiting_fixture.cpp"
}

#include <cstring>

// =============================================================================
// Test Framer 1: Prepend Length on Send, Passthrough on Receive
// =============================================================================

static void length_prepend_encode(ct_connection_t* connection,
                                   const ct_message_t* message,
                                   ct_message_context_t* context) {
    // Prepend the message length as a single byte
    size_t framed_len = 1 + message->length;
    ct_message_t framed_msg;
    framed_msg.content = (char*)malloc(framed_len);
    framed_msg.length = framed_len;

    // First byte is the length
    ((char*)framed_msg.content)[0] = '0' + (char)message->length;
    // Copy original message after length byte
    memcpy((char*)framed_msg.content + 1, message->content, message->length);

    // Send to protocol
    ct_connection_send_to_protocol(connection, &framed_msg);

    free(framed_msg.content);
}

static void passthrough_decode(ct_connection_t* connection,
                                const void* data,
                                size_t len) {
    // Just pass through - deliver as-is
    ct_message_t* msg = (ct_message_t*)malloc(sizeof(ct_message_t));
    msg->content = (char*)malloc(len);
    msg->length = len;
    memcpy(msg->content, data, len);

    ct_connection_deliver_to_app(connection, msg, NULL);
}

static ct_framer_impl_t length_prepend_framer = {
    .encode_message = length_prepend_encode,
    .decode_data = passthrough_decode
};

// =============================================================================
// Test Framer 2: Passthrough on Send, Remove First Char on Receive
// =============================================================================

static void passthrough_encode(ct_connection_t* connection,
                                const ct_message_t* message,
                                ct_message_context_t* context) {
    // Just pass through - send as-is
    ct_message_t pass_msg;
    pass_msg.content = message->content;
    pass_msg.length = message->length;

    ct_connection_send_to_protocol(connection, &pass_msg);
}

static void strip_first_char_decode(ct_connection_t* connection,
                                     const void* data,
                                     size_t len) {
    // Remove first character
    if (len <= 1) {
        // If message is 1 byte or empty, deliver empty message
        ct_message_t* msg = (ct_message_t*)malloc(sizeof(ct_message_t));
        msg->content = (char*)malloc(1);
        msg->length = 0;
        ct_connection_deliver_to_app(connection, msg, NULL);
        return;
    }

    ct_message_t* msg = (ct_message_t*)malloc(sizeof(ct_message_t));
    msg->content = (char*)malloc(len - 1);
    msg->length = len - 1;
    // Copy everything except first character
    memcpy(msg->content, (const char*)data + 1, len - 1);

    ct_connection_deliver_to_app(connection, msg, NULL);
}

static ct_framer_impl_t strip_first_char_framer = {
    .encode_message = passthrough_encode,
    .decode_data = strip_first_char_decode
};

// =============================================================================
// Framing Tests
// =============================================================================

class FramingTest : public CTapsGenericFixture {};

TEST_F(FramingTest, LengthPrependFramerSendsCorrectFormat) {
    // Test that prepending length byte results in correct format being sent
    // Application sends "ping" (4 bytes)
    // Framer should send "4ping" (5 bytes) - length byte + original message
    // TCP echo server should respond with "Pong: 4ping"

    ct_transport_properties_t transport_properties;
    ct_transport_properties_build(&transport_properties);
    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, REQUIRE);
    ct_tp_set_sel_prop_preference(&transport_properties, ACTIVE_READ_BEFORE_SEND, REQUIRE);

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");
    ct_remote_endpoint_with_port(&remote_endpoint, 5006);

    ct_preconnection_t preconnection;
    // Use the extended build function to set the framer
    ct_preconnection_build_ex(&preconnection, transport_properties,
                              &remote_endpoint, 1, nullptr,
                              &length_prepend_framer);

    ct_connection_t connection;
    CallbackContext context = {
        .messages = &received_messages,
        .server_connections = received_connections,
        .client_connections = client_connections,
        .closing_function = nullptr,
        .total_expected_messages = 1,
        .listener = nullptr
    };

    ct_connection_callbacks_t connection_callbacks = {
      .ready = send_message_and_receive,
      .user_connection_context = &context,
    };

    int rc = ct_preconnection_initiate(&preconnection,
                                   &connection,
                                   connection_callbacks);
    ASSERT_EQ(rc, 0);
    ct_start_event_loop();

    // Wait for response


    // Verify we got a response
    ASSERT_EQ(received_messages.size(), 1);
    ct_message_t* response = received_messages[0];

    std::string response_str((char*)response->content, response->length);

    ASSERT_STREQ(response_str.c_str(), "Pong: 5ping");
    ct_preconnection_free(&preconnection);
}

TEST_F(FramingTest, StripFirstCharFramerReceivesStrippedMessage) {
    ct_transport_properties_t transport_properties;
    ct_transport_properties_build(&transport_properties);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, REQUIRE);
    ct_tp_set_sel_prop_preference(&transport_properties, ACTIVE_READ_BEFORE_SEND, REQUIRE);

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");
    ct_remote_endpoint_with_port(&remote_endpoint, 5006);

    ct_preconnection_t preconnection;
    // Use the extended build function to set the framer
    ct_preconnection_build_ex(&preconnection, transport_properties,
                              &remote_endpoint, 1, nullptr,
                              &strip_first_char_framer);

    ct_connection_t connection;
    CallbackContext context = {
        .messages = &received_messages,
        .server_connections = received_connections,
        .client_connections = client_connections,
        .closing_function = nullptr,
        .total_expected_messages = 1,
        .listener = nullptr
    };

    ct_connection_callbacks_t connection_callbacks = {
      .ready = send_message_and_receive,
      .user_connection_context = &context,
    };

    int rc = ct_preconnection_initiate(&preconnection,
                                   &connection,
                                   connection_callbacks);
    ASSERT_EQ(rc, 0);
    ct_start_event_loop();

    // Wait for response


    // Verify we got a response
    ASSERT_EQ(received_messages.size(), 1);
    ct_message_t* response = received_messages[0];

    std::string response_str((char*)response->content, response->length);

    ASSERT_STREQ(response_str.c_str(), "ong: ping");
    ct_preconnection_free(&preconnection);
}
