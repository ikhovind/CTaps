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
#include "fixtures/awaiting_fixture.cpp"

TEST_F(CTapsGenericFixture, ReceivesPacketFromLocalClient) {
    Listener listener;
    Connection client_connection;

    std::function cleanup_logic = [&](CallbackContext* ctx) {
        printf("Cleanup: Closing connection.\n");
        connection_close(&client_connection);
        listener_close(&listener);
        // close all connections created by the listener
        for (Connection* conn : ctx->connections) {
            connection_close(conn);
        }
    };
    CallbackContext callback_context = {
        .awaiter = &awaiter,
        .messages = &received_messages,
        .connections = received_connections,
        .closing_function = &cleanup_logic,
        .total_expected_signals = 3,
        .total_expected_messages = 1,
    };

    LocalEndpoint listener_endpoint;

    local_endpoint_with_ipv4(&listener_endpoint, inet_addr("127.0.0.1"));
    local_endpoint_with_port(&listener_endpoint, 1234);

    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);
    remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");

    TransportProperties listener_props;
    transport_properties_build(&listener_props);

    Preconnection listener_precon;
    preconnection_build_with_local(&listener_precon, listener_props, &remote_endpoint, 1, listener_endpoint);

    preconnection_listen(&listener_precon, &listener, receive_message_on_connection_received, &callback_context);

    // --- SETUP CLIENT ---
    RemoteEndpoint client_remote;
    remote_endpoint_with_hostname(&client_remote, "127.0.0.1");
    remote_endpoint_with_port(&client_remote, 1234); // Point to the listener

    TransportProperties client_props;
    transport_properties_build(&client_props);

    Preconnection client_precon;
    preconnection_build(&client_precon, client_props, &client_remote, 1);


    InitDoneCb client_ready = {
        .init_done_callback = send_message_on_connection_ready,
        .user_data = &callback_context
    };
    preconnection_initiate(&client_precon, &client_connection, client_ready, nullptr);

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    ctaps_start_event_loop();

    // --- ASSERTIONS ---
    ASSERT_EQ(callback_context.connections.size(), 1);
    ASSERT_EQ(callback_context.messages->size(), 1);
    ASSERT_EQ(callback_context.messages->at(0)->length, 5);
    ASSERT_STREQ(callback_context.messages->at(0)->content, "ping");
}

TEST_F(CTapsGenericFixture, ClosingListenerDoesNotAffectExistingConnections) {
    Listener listener;
    Connection client_connection;


    std::function close_listener = [&](CallbackContext* ctx) {
        listener_close(&listener);
    };
    std::function final_cleanup = [&](CallbackContext* ctx) {
        printf("Cleanup: Closing connection.\n");
        connection_close(&client_connection);
        // close all connections created by the listener
        for (Connection* conn : ctx->connections) {
            connection_close(conn);
        }
    };
    CallbackContext callback_context = {
        .awaiter = &awaiter,
        .messages = &received_messages,
        .connections = received_connections,
        .closing_function = &close_listener,
        .total_expected_signals = 4,
        .listener = &listener
    };

    LocalEndpoint listener_endpoint;

    local_endpoint_with_ipv4(&listener_endpoint, inet_addr("127.0.0.1"));
    local_endpoint_with_port(&listener_endpoint, 1234);

    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);
    remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");

    TransportProperties listener_props;
    transport_properties_build(&listener_props);

    Preconnection listener_precon;
    preconnection_build_with_local(&listener_precon, listener_props, &remote_endpoint, 1, listener_endpoint);

    preconnection_listen(&listener_precon, &listener, on_connection_received_receive_message_close_listener_and_send_new_message, &callback_context);

    // --- SETUP CLIENT ---
    RemoteEndpoint client_remote;
    remote_endpoint_with_hostname(&client_remote, "127.0.0.1");
    remote_endpoint_with_port(&client_remote, 1234); // Point to the listener

    TransportProperties client_props;
    transport_properties_build(&client_props);

    Preconnection client_precon;
    preconnection_build(&client_precon, client_props, &client_remote, 1);


    InitDoneCb client_ready = {
        .init_done_callback = send_message_on_connection_ready,
        .user_data = &callback_context
    };

    preconnection_initiate(&client_precon, &client_connection, client_ready, nullptr);

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    ctaps_start_event_loop();

    // --- ASSERTIONS ---
    ASSERT_EQ(callback_context.connections.size(), 1);
    ASSERT_EQ(callback_context.messages->size(), 2);
    ASSERT_EQ(callback_context.messages->at(0)->length, 5);
    ASSERT_STREQ(callback_context.messages->at(0)->content, "ping");
    ASSERT_EQ(callback_context.messages->at(0)->length, 6);
    ASSERT_STREQ(callback_context.messages->at(0)->content, "ping2");
}
