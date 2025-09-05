#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
#include "util/util.h"
}

#include <mutex>
#include <condition_variable>
#include "fixtures/awaiting_fixture.cpp"

TEST_F(CTapsGenericFixture, sendsSingleUdpPacket) {
    // --- Setup ---
    RemoteEndpoint remote_endpoint;
    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);
    Connection connection;

    auto cleanup_logic = [&](CallbackContext* ctx) {
        printf("Cleanup: Closing connection.\n");
        connection_close(&connection);
    };
    std::function closing_func = cleanup_logic;

    CallbackContext callback_context = {
        .awaiter = &awaiter,
        .messages = &received_messages,
        .closing_function = &closing_func,
        .total_expected_signals = 2
    };

    InitDoneCb init_done_cb = {
        .init_done_callback = on_connection_ready,
        .user_data = &callback_context
    };

    preconnection_initiate(&preconnection, &connection, init_done_cb, nullptr);

    // "Await" the connection to be ready (1 signal expected)
    awaiter.await(1);
    ASSERT_EQ(awaiter.get_signal_count(), 1);

    // --- Action ---
    Message message;
    message_build_with_content(&message, "hello world", strlen("hello world") + 1);
    send_message(&connection, &message);
    message_free_content(&message);

    ReceiveMessageRequest receive_req = { .receive_cb = on_message_received, .user_data = &callback_context };
    receive_message(&connection, receive_req);

    ctaps_start_event_loop();

    // "Await" the receive callback (total signals now expected: 2)
    awaiter.await(2);
    ASSERT_EQ(awaiter.get_signal_count(), 2);

    // --- Assertions ---
    ASSERT_EQ(received_messages.size(), 1);
    EXPECT_STREQ(received_messages[0]->content, "Pong: hello world");
}


TEST_F(CTapsGenericFixture, packetsAreReadInOrder) {
    const size_t TOTAL_EXPECTED_SIGNALS = 3; // 1 ready + 2 receives

    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);

    Connection connection;


    auto cleanup_logic = [&](CallbackContext* ctx) {
        printf("Cleanup: Closing connection.\n");
        connection_close(&connection);
    };
    std::function cleanup_func = cleanup_logic;

    CallbackContext callback_context = {
        .awaiter = &awaiter,
        .messages = &received_messages,
        .closing_function = &cleanup_func,
        .total_expected_signals = TOTAL_EXPECTED_SIGNALS
    };

    InitDoneCb init_done_cb = { .init_done_callback = on_connection_ready, .user_data = &callback_context };
    preconnection_initiate(&preconnection, &connection, init_done_cb, nullptr);
    /*
    awaiter.await(1);

    // --- Action ---
    // ... build and send message1 and message2 ...
    char* hello1 = "hello 1";
    Message message1;
    message_build_with_content(&message1, hello1, strlen(hello1) + 1);
    send_message(&connection, &message1);

    char* hello2 = "hello 2";
    Message message2;
    message_build_with_content(&message2, hello2, strlen(hello2) + 1);
    send_message(&connection, &message2);

    ReceiveMessageRequest receive_req = { .receive_cb = on_message_received, .user_data = &callback_context };

    // Post two receive requests
    receive_message(&connection, receive_req);
    receive_message(&connection, receive_req);

    // --- Run Event Loop ---
    ctaps_start_event_loop();

    // --- Assertions ---
    ASSERT_EQ(awaiter.get_signal_count(), TOTAL_EXPECTED_SIGNALS);
    ASSERT_EQ(received_messages.size(), 2);
    EXPECT_STREQ(received_messages[0]->content, "Pong: hello 1");
    EXPECT_STREQ(received_messages[1]->content, "Pong: hello 2");
    */
}

TEST_F(CTapsGenericFixture, canPingArbitraryBytes) {
    // Total signals: 1 for the connection being ready, 1 for the message being received.
    const size_t TOTAL_EXPECTED_SIGNALS = 2;

    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);

    Connection connection;


    auto cleanup_logic = [&](CallbackContext* ctx) {
        printf("Cleanup: Closing connection.\n");
        connection_close(&connection);
    };
    std::function closing_func = cleanup_logic;

    CallbackContext callback_context = {
        .awaiter = &awaiter,
        .messages = &received_messages,
        .closing_function = &closing_func,
        .total_expected_signals = TOTAL_EXPECTED_SIGNALS
    };

    InitDoneCb init_done_cb = {
        .init_done_callback = on_connection_ready,
        .user_data = &callback_context
    };
    preconnection_initiate(&preconnection, &connection, init_done_cb, nullptr);

    // "Await" the connection to be ready before we send anything.
    awaiter.await(1);
    ASSERT_EQ(awaiter.get_signal_count(), 1) << "Test timed out waiting for connection to be ready.";

    // --- Action ---
    Message message;
    char bytes_to_send[] = {0, 1, 2, 3, 4, 5};
    message_build_with_content(&message, bytes_to_send, sizeof(bytes_to_send));

    send_message(&connection, &message);
    message_free_content(&message);

    ReceiveMessageRequest receive_req = {
        .receive_cb = on_message_received,
        .user_data = &callback_context
    };

    // Post the receive request
    receive_message(&connection, receive_req);

    // --- Run Event Loop ---
    // This blocks until the on_message_received callback closes the connection.
    ctaps_start_event_loop();

    // --- Assertions ---
    ASSERT_EQ(awaiter.get_signal_count(), TOTAL_EXPECTED_SIGNALS) << "Test timed out waiting for message.";
    ASSERT_EQ(received_messages.size(), 1);

    char expected_output[] = {'P', 'o', 'n', 'g', ':', ' ', 0, 1, 2, 3, 4, 5};
    ASSERT_EQ(received_messages[0]->length, sizeof(expected_output));
    EXPECT_EQ(memcmp(expected_output, received_messages[0]->content, sizeof(expected_output)), 0);
}