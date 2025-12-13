#include <gmock/gmock-matchers.h>
#include "gtest/gtest.h"

extern "C" {
#include "ctaps.h"
#include "fixtures/awaiting_fixture.cpp"
}

#include <cstring>

// =============================================================================
// Test Framer 1: Prepend Length on Send, Passthrough on Receive
// =============================================================================

static int length_prepend_encode(ct_connection_t* connection,
                                   ct_message_t* message,
                                   ct_message_context_t* context,
                                   ct_framer_done_encoding_callback callback) {
    // Prepend the message length as a single byte
    // First byte is the length
    message->content = (char*)realloc(message->content, message->length + 1);
    memmove(message->content + 1, message->content, message->length);
    message->content[0] = '0' + (char)(message->length);
    message->length += 1;

    return callback(connection, message, context);
}

static void passthrough_decode(struct ct_connection_s* connection,
                     const void* data,
                     size_t len,
                     ct_framer_done_decoding_callback callback) {
    // Just pass through - deliver as-is
    ct_message_t* msg = (ct_message_t*)malloc(sizeof(ct_message_t));
    msg->content = (char*)malloc(len);
    msg->length = len;
    memcpy(msg->content, data, len);

    callback(connection, msg, NULL);
}

static ct_framer_impl_t length_prepend_framer = {
    .encode_message = length_prepend_encode,
    .decode_data = passthrough_decode
};

// =============================================================================
// Test Framer 2: Passthrough on Send, Remove First Char on Receive
// =============================================================================

static int passthrough_encode(ct_connection_t* connection,
                                ct_message_t* message,
                                ct_message_context_t* context,
                                ct_framer_done_encoding_callback callback) {
    // Just pass through - send as-is
    return callback(connection, message, context);
}

static void strip_first_char_decode(ct_connection_t* connection,
                                     const void* data,
                                     size_t len,
                                     ct_framer_done_decoding_callback callback
                                    ) {
    // Remove first character
    if (len <= 1) {
        // If message is 1 byte or empty, deliver empty message
        ct_message_t* msg = (ct_message_t*)malloc(sizeof(ct_message_t));
        msg->content = nullptr;
        msg->length = 0;
        callback(connection, msg, NULL);
        return;
    }

    ct_message_t* msg = (ct_message_t*)malloc(sizeof(ct_message_t));
    msg->content = (char*)malloc(len - 1);
    msg->length = len - 1;
    // Copy everything except first character
    memcpy(msg->content, (const char*)data + 1, len - 1);

    callback(connection, msg, NULL);
}

static ct_framer_impl_t strip_first_char_framer = {
    .encode_message = passthrough_encode,
    .decode_data = strip_first_char_decode
};

// =============================================================================
// Test Framer 3: Async Encoding - Defers callback using uv_timer
// =============================================================================

// Structure to hold state for async encoding
typedef struct {
    ct_connection_t* connection;
    ct_message_t* message;
    ct_message_context_t* context;
    ct_framer_done_encoding_callback callback;
    uv_timer_t timer;
} async_framer_state_t;

static void async_encode_timer_callback(uv_timer_t* timer) {
    async_framer_state_t* state = (async_framer_state_t*)timer->data;

    // Now invoke the actual callback after the delay
    state->callback(state->connection, state->message, state->context);

    // Cleanup
    uv_timer_stop(timer);
    uv_close((uv_handle_t*)timer, NULL);
    free(state);
}

static int async_encode(ct_connection_t* connection,
                        ct_message_t* message,
                        ct_message_context_t* context,
                        ct_framer_done_encoding_callback callback) {
    // Allocate state to persist after this function returns
    async_framer_state_t* state = (async_framer_state_t*)malloc(sizeof(async_framer_state_t));
    state->connection = connection;
    state->callback = callback;
    // No need to copy message - the library already did it for us!
    state->message = (ct_message_t*)message;
    state->context = context;

    // Set up a timer to invoke the callback after 10ms
    uv_timer_init(event_loop, &state->timer);
    state->timer.data = state;
    uv_timer_start(&state->timer, async_encode_timer_callback, 10, 0);

    // Return immediately - callback will be invoked later from event loop
    return 0;
}

static ct_framer_impl_t async_framer = {
    .encode_message = async_encode,
    .decode_data = passthrough_decode
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

    ct_connection_callbacks_t connection_callbacks = {
      .ready = send_message_and_receive,
      .user_connection_context = &test_context,
    };

    int rc = ct_preconnection_initiate(&preconnection, connection_callbacks);
    ASSERT_EQ(rc, 0);
    ct_start_event_loop();

    // Verify we got a response
    ASSERT_EQ(per_connection_messages.size(), 1);

    ct_connection_t* conn = test_context.client_connections[0];
    ct_message_t* response = per_connection_messages[conn][0];

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



    ct_connection_callbacks_t connection_callbacks = {
      .ready = send_message_and_receive,
      .user_connection_context = &test_context,
    };

    int rc = ct_preconnection_initiate(&preconnection, connection_callbacks);
    ASSERT_EQ(rc, 0);
    ct_start_event_loop();

    // Wait for response


    // Verify we got a response
    ASSERT_EQ(per_connection_messages.size(), 1);
    ct_connection_t* conn = test_context.client_connections[0];
    ASSERT_EQ(per_connection_messages[conn].size(), 1);
    ct_message_t* response = per_connection_messages[conn][0];

    std::string response_str((char*)response->content, response->length);

    ASSERT_STREQ(response_str.c_str(), "ong: ping");
    ct_preconnection_free(&preconnection);
}

TEST_F(FramingTest, AsyncFramerDefersSendCallback) {
    // Test that async framer properly defers the callback invocation
    // This verifies that encode_message can return before calling the callback
    // and the message remains valid until the callback is invoked
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
    // Use the async framer which defers the callback by 10ms
    ct_preconnection_build_ex(&preconnection, transport_properties,
                              &remote_endpoint, 1, nullptr,
                              &async_framer);

    ct_connection_callbacks_t connection_callbacks = {
      .ready = send_message_and_receive,
      .user_connection_context = &test_context,
    };

    int rc = ct_preconnection_initiate(&preconnection, connection_callbacks);
    ASSERT_EQ(rc, 0);
    ct_start_event_loop();

    // Verify we got a response (message was successfully sent even though callback was deferred)
    ASSERT_EQ(per_connection_messages.size(), 1);
    ct_connection_t* conn = test_context.client_connections[0];
    ASSERT_EQ(per_connection_messages[conn].size(), 1);
    ct_message_t* response = per_connection_messages[conn][0];

    std::string response_str((char*)response->content, response->length);

    // Should get normal response since async_framer doesn't modify the message
    ASSERT_STREQ(response_str.c_str(), "Pong: ping");
    ct_preconnection_free(&preconnection);
}
