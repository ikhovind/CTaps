#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
}
#include "fixtures/awaiting_fixture.cpp"

TEST_F(CTapsGenericFixture, ReceivesConnectionFromListenerAndExchangesMessages) {
    ct_listener_t listener;

    ct_local_endpoint_t listener_endpoint;
    ct_local_endpoint_build(&listener_endpoint);

    ct_local_endpoint_with_interface(&listener_endpoint, "lo");
    ct_local_endpoint_with_port(&listener_endpoint, 1239);

    ct_remote_endpoint_t listener_remote;
    ct_remote_endpoint_build(&listener_remote);
    ct_remote_endpoint_with_hostname(&listener_remote, "127.0.0.1");

    ct_transport_properties_t* listener_props = ct_transport_properties_new();
    ASSERT_NE(listener_props, nullptr);

    ct_tp_set_sel_prop_preference(listener_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(listener_props, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
    ct_tp_set_sel_prop_preference(listener_props, MULTISTREAMING, PROHIBIT);

    ct_preconnection_t* listener_precon = ct_preconnection_new(&listener_remote, 1, listener_props, NULL);
    ASSERT_NE(listener_precon, nullptr);
    ct_preconnection_set_local_endpoint(listener_precon, &listener_endpoint);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = receive_message_respond_and_close_listener_on_connection_received,
        .user_listener_context = &test_context
    };

    int listen_res = ct_preconnection_listen(listener_precon, &listener, listener_callbacks);

    ASSERT_EQ(listen_res, 0);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t client_remote;
    ct_remote_endpoint_build(&client_remote);
    ct_remote_endpoint_with_hostname(&client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(&client_remote, 1239);

    ct_transport_properties_t* client_props = ct_transport_properties_new();
    ASSERT_NE(client_props, nullptr);

    ct_tp_set_sel_prop_preference(client_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(client_props, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
    ct_tp_set_sel_prop_preference(client_props, MULTISTREAMING, PROHIBIT);

    ct_preconnection_t* client_precon = ct_preconnection_new(&client_remote, 1, client_props, NULL);
    ASSERT_NE(client_precon, nullptr);

    // Custom ready callback that saves connection and calls original ready

    ct_connection_callbacks_t client_callbacks {
        .ready = send_message_and_receive,
        .user_connection_context = &test_context
    };

    ct_preconnection_initiate(client_precon, client_callbacks);

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    ct_start_event_loop();

    ct_connection_t* client_connection = test_context.client_connections[0];

    // --- ASSERTIONS ---
    ASSERT_EQ(per_connection_messages.size(), 2); // Both client and server connections

    // Client receives "pong"
    ASSERT_EQ(per_connection_messages[client_connection].size(), 1);
    ASSERT_EQ(per_connection_messages[client_connection][0]->length, 5);
    ASSERT_STREQ(per_connection_messages[client_connection][0]->content, "pong");

    // Server receives "ping"
    ASSERT_EQ(test_context.server_connections.size(), 1);
    ct_connection_t* server_connection = test_context.server_connections[0];
    ASSERT_EQ(per_connection_messages[server_connection].size(), 1);
    ASSERT_EQ(per_connection_messages[server_connection][0]->length, 5);
    ASSERT_STREQ(per_connection_messages[server_connection][0]->content, "ping");

    // Cleanup
    ct_preconnection_free(listener_precon);
    ct_transport_properties_free(listener_props);
    ct_preconnection_free(client_precon);
    ct_transport_properties_free(client_props);
}
