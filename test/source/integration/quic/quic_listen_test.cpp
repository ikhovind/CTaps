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

TEST_F(CTapsGenericFixture, QuicReceivesConnectionFromListenerAndExchangesMessages) {
    CallbackContext callback_context = {
        .awaiter = &awaiter,
        .messages = &received_messages,
        .server_connections = received_connections,
        .client_connections = client_connections,
    };

    ctaps_initialize();
    Listener listener;
    Connection client_connection;

    LocalEndpoint listener_endpoint;
    local_endpoint_build(&listener_endpoint);

    local_endpoint_with_interface(&listener_endpoint, "lo");
    local_endpoint_with_port(&listener_endpoint, 1239);

    RemoteEndpoint listener_remote;
    remote_endpoint_build(&listener_remote);
    remote_endpoint_with_hostname(&listener_remote, "127.0.0.1");

    TransportProperties listener_props;
    transport_properties_build(&listener_props);

    tp_set_sel_prop_preference(&listener_props, RELIABILITY, REQUIRE);
    tp_set_sel_prop_preference(&listener_props, MULTISTREAMING, REQUIRE); // force QUIC

    Preconnection listener_precon;
    preconnection_build_with_local(&listener_precon, listener_props, &listener_remote, 1, listener_endpoint);

    ListenerCallbacks listener_callbacks = {
        .connection_received = receive_message_respond_and_close_listener_on_connection_received,
        .user_data = &callback_context
    };

    int listen_res = preconnection_listen(&listener_precon, &listener, listener_callbacks);

    ASSERT_EQ(listen_res, 0);

    // --- SETUP CLIENT ---
    RemoteEndpoint client_remote;
    remote_endpoint_build(&client_remote);
    remote_endpoint_with_hostname(&client_remote, "127.0.0.1");
    remote_endpoint_with_port(&client_remote, 1239);

    TransportProperties client_props;
    transport_properties_build(&client_props);

    tp_set_sel_prop_preference(&client_props, RELIABILITY, REQUIRE);
    tp_set_sel_prop_preference(&client_props, MULTISTREAMING, REQUIRE); //

    Preconnection client_precon;
    preconnection_build(&client_precon, client_props, &client_remote, 1);

    ConnectionCallbacks client_callbacks {
        .ready = send_message_and_receive,
        .user_data = &callback_context
    };

    preconnection_initiate(&client_precon, &client_connection, client_callbacks);
  /*

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    */
    ctaps_start_event_loop();

    // --- ASSERTIONS ---
    ASSERT_EQ(callback_context.messages->size(), 2);
    ASSERT_EQ(callback_context.messages->at(0)->length, 5);
    ASSERT_STREQ(callback_context.messages->at(0)->content, "ping");
    ASSERT_EQ(callback_context.messages->at(1)->length, 5);
    ASSERT_STREQ(callback_context.messages->at(1)->content, "pong");
}
