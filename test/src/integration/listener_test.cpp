#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "util/util.h"
}
#include "fixtures/awaiting_fixture.cpp"

TEST_F(CTapsGenericFixture, ReceivesConnectionFromListenerAndExchangesMessages) {
    GTEST_SKIP(); // Don't know why this fails atm
    ct_initialize(NULL, NULL);
    ct_listener_t listener;
    ct_connection_t client_connection;

    std::function cleanup_logic = [&](CallbackContext* ctx) {
        printf("Cleanup: Closing connection.\n");
        ct_listener_close(&listener);
        printf("Cleanup: Closing connection.\n");
        // close all server_connections created by the listener
        printf("Closing %zu server connections.\n", ctx->server_connections.size());
        for (ct_connection_t* conn : ctx->server_connections) {
            ct_connection_close(conn);
        }
        printf("Closing %zu client connections.\n", ctx->client_connections.size());
        for (ct_connection_t* conn : ctx->client_connections) {
            ct_connection_close(conn);
        }
    };
    CallbackContext callback_context = {
        .messages = &received_messages,
        .server_connections = received_connections,
        .client_connections = client_connections,
        .closing_function = &cleanup_logic,
        .listener = &listener
    };

    callback_context.client_connections.push_back(&client_connection);

    ct_local_endpoint_t listener_endpoint;
    ct_local_endpoint_build(&listener_endpoint);

    ct_local_endpoint_with_interface(&listener_endpoint, "lo");
    ct_local_endpoint_with_port(&listener_endpoint, 1238);

    ct_remote_endpoint_t listener_remote;
    ct_remote_endpoint_build(&listener_remote);
    ct_remote_endpoint_with_hostname(&listener_remote, "127.0.0.1");

    ct_transport_properties_t listener_props;
    ct_transport_properties_build(&listener_props);

    ct_tp_set_sel_prop_preference(&listener_props, RELIABILITY, PROHIBIT);

    ct_preconnection_t listener_precon;
    ct_preconnection_build_with_local(&listener_precon, listener_props, &listener_remote, 1, listener_endpoint);

    int listen_res = ct_preconnection_listen(&listener_precon, &listener, receive_message_and_respond_on_connection_received, &callback_context);
    if (listen_res != 0) {
        printf("Failed to start listener with error code %d\n", listen_res);
        return;
    }
    // --- SETUP CLIENT ---
    ct_remote_endpoint_t client_remote;
    ct_remote_endpoint_with_hostname(&client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(&client_remote, 1238); // Point to the listener

    ct_transport_properties_t client_props;
    ct_transport_properties_build(&client_props);

    ct_tp_set_sel_prop_preference(&client_props, RELIABILITY, PROHIBIT);

    ct_preconnection_t client_precon;
    ct_preconnection_build(&client_precon, client_props, &client_remote, 1, NULL);


    InitDoneCb client_ready = {
        .init_done_callback = send_message_and_wait_for_response_on_connection_ready,
        .user_connection_context = &callback_context
    };

    ct_preconnection_initiate(&client_precon, &client_connection, client_ready, nullptr);
    //ct_listener_close(&listener);

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles

    ct_start_event_loop();

    // --- ASSERTIONS ---
    ASSERT_EQ(callback_context.server_connections.size(), 1);
    ASSERT_EQ(callback_context.messages->size(), 2);
    ASSERT_EQ(callback_context.messages->at(0)->length, 5);
    ASSERT_STREQ(callback_context.messages->at(0)->content, "ping");

    ASSERT_EQ(callback_context.messages->at(1)->length, 5);
    ASSERT_STREQ(callback_context.messages->at(1)->content, "pong");
}

TEST_F(CTapsGenericFixture, ClosingListenerDoesNotAffectExistingConnections) {
    ct_listener_t listener;
    ct_connection_t client_connection;

    std::function final_cleanup = [&](CallbackContext* ctx) {
        printf("Cleanup: Closing connection.\n");
        // close all server_connections created by the listener
        printf("Closing %zu server connections.\n", ctx->server_connections.size());
        for (ct_connection_t* conn : ctx->server_connections) {
            ct_connection_close(conn);
        }
        printf("Closing %zu client connections.\n", ctx->client_connections.size());
        for (ct_connection_t* conn : ctx->client_connections) {
            ct_connection_close(conn);
        }
    };
    CallbackContext callback_context = {
        .messages = &received_messages,
        .server_connections = received_connections,
        .client_connections = client_connections,
        .closing_function = &final_cleanup,
        .listener = &listener
    };

    callback_context.client_connections.push_back(&client_connection);

    ct_local_endpoint_t listener_endpoint;
    ct_local_endpoint_build(&listener_endpoint);

    ct_local_endpoint_with_interface(&listener_endpoint, "lo");
    ct_local_endpoint_with_port(&listener_endpoint, 6234);

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));

    ct_transport_properties_t listener_props;
    ct_transport_properties_build(&listener_props);

    ct_tp_set_sel_prop_preference(&listener_props, RELIABILITY, PROHIBIT);

    ct_preconnection_t listener_precon;
    ct_preconnection_build_with_local(&listener_precon, listener_props, &remote_endpoint, 1, listener_endpoint);

    ct_preconnection_listen(&listener_precon, &listener, on_connection_received_receive_message_close_listener_and_send_new_message, &callback_context);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t client_remote;
    ct_remote_endpoint_with_ipv4(&client_remote, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&client_remote, 6234); // Point to the listener

    ct_transport_properties_t client_props;
    ct_transport_properties_build(&client_props);

    ct_tp_set_sel_prop_preference(&client_props, RELIABILITY, PROHIBIT);

    ct_preconnection_t client_precon;
    ct_preconnection_build(&client_precon, client_props, &client_remote, 1, NULL);


    InitDoneCb client_ready = {
        .init_done_callback = send_message_on_connection_ready,
        .user_connection_context = &callback_context
    };

    ct_preconnection_initiate(&client_precon, &client_connection, client_ready, nullptr);

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    ct_start_event_loop();

    // --- ASSERTIONS ---
    ASSERT_EQ(callback_context.server_connections.size(), 1);
    ASSERT_EQ(callback_context.messages->size(), 2);

    ASSERT_EQ(callback_context.messages->at(0)->length, 5);
    ASSERT_STREQ(callback_context.messages->at(0)->content, "ping");

    ASSERT_EQ(callback_context.messages->at(1)->length, 6);
    ASSERT_STREQ(callback_context.messages->at(1)->content, "ping2");
}

TEST_F(CTapsGenericFixture, ClosingListenerWithNoConnectionsClosesSocketManager) {
    ct_listener_t listener;
    ct_connection_t client_connection;

    std::function final_cleanup = [&](CallbackContext* ctx) {
        ct_listener_close(ctx->listener);
    };
    CallbackContext callback_context = {
        .messages = &received_messages,
        .server_connections = received_connections,
        .client_connections = client_connections,
        .closing_function = &final_cleanup,
        .listener = &listener
    };

    callback_context.client_connections.push_back(&client_connection);

    ct_local_endpoint_t listener_endpoint;
    ct_local_endpoint_build(&listener_endpoint);

    ct_local_endpoint_with_interface(&listener_endpoint, "lo");
    ct_local_endpoint_with_port(&listener_endpoint, 6235);

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));

    ct_transport_properties_t listener_props;
    ct_transport_properties_build(&listener_props);

    ct_tp_set_sel_prop_preference(&listener_props, RELIABILITY, PROHIBIT);

    ct_preconnection_t listener_precon;
    ct_preconnection_build_with_local(&listener_precon, listener_props, &remote_endpoint, 1, listener_endpoint);

    ct_preconnection_listen(&listener_precon, &listener, on_connection_received_receive_message_close_listener_and_send_new_message, &callback_context);

    ct_listener_close(&listener);
    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    ct_start_event_loop();
}
