#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
}
#include "fixtures/awaiting_fixture.cpp"

TEST_F(CTapsGenericFixture, ClosingListenerDoesNotAffectExistingConnections) {
    ct_listener_t listener;

    ct_local_endpoint_t* listener_endpoint = ct_local_endpoint_new();
    ASSERT_NE(listener_endpoint, nullptr);

    ct_local_endpoint_with_interface(listener_endpoint, "lo");
    ct_local_endpoint_with_port(listener_endpoint, 6234);

    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));

    ct_transport_properties_t* listener_props = ct_transport_properties_new();
  ASSERT_NE(listener_props, nullptr);
    // Allocated with ct_transport_properties_new()

    ct_tp_set_sel_prop_preference(listener_props, RELIABILITY, PROHIBIT);

    ct_preconnection_t* listener_precon = ct_preconnection_new(remote_endpoint, 1, listener_props, NULL);
    ASSERT_NE(listener_precon, nullptr);
    ct_preconnection_set_local_endpoint(listener_precon, listener_endpoint);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = on_connection_received_receive_message_close_listener_and_send_new_message,
        .user_listener_context = &test_context
    };

    ct_preconnection_listen(listener_precon, &listener, listener_callbacks);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t* client_remote = ct_remote_endpoint_new();
    ASSERT_NE(client_remote, nullptr);
    ct_remote_endpoint_with_ipv4(client_remote, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(client_remote, 6234); // Point to the listener

    ct_transport_properties_t* client_props = ct_transport_properties_new();
  ASSERT_NE(client_props, nullptr);
    // Allocated with ct_transport_properties_new()

    ct_tp_set_sel_prop_preference(client_props, RELIABILITY, PROHIBIT);

    ct_preconnection_t* client_precon = ct_preconnection_new(client_remote, 1, client_props, NULL);
    ASSERT_NE(client_precon, nullptr);

    ct_connection_callbacks_t client_callbacks = {
        .ready = send_message_on_connection_ready,
        .user_connection_context = &test_context
    };

    ct_preconnection_initiate(client_precon, client_callbacks);

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    ct_start_event_loop();

    // --- ASSERTIONS ---
    ASSERT_EQ(test_context.server_connections.size(), 1);
    ASSERT_EQ(test_context.per_connection_messages[test_context.server_connections[0]]->size(), 1);
    ASSERT_EQ(test_context.per_connection_messages[test_context.server_connections[0]][0]->length, 5);
    ASSERT_EQ(test_context.per_connection_messages[test_context.server_connections[0]][0]->content, "ping");


    ASSERT_EQ(test_context.per_connection_messages[test_context.server_connections[1]]->size(), 1);
    ASSERT_EQ(test_context.per_connection_messages[test_context.server_connections[1]][0]->length, 6);
    ASSERT_EQ(test_context.per_connection_messages[test_context.server_connections[1]][0]->content, "ping2");

    ct_free_local_endpoint(listener_endpoint);
    ct_remote_endpoint_free(remote_endpoint);
    ct_remote_endpoint_free(client_remote);
    ct_preconnection_free(client_precon);
    ct_transport_properties_free(client_props);
    ct_preconnection_free(listener_precon);
    ct_transport_properties_free(listener_props);
}

TEST_F(CTapsGenericFixture, ClosingListenerWithNoConnectionsClosesSocketManager) {
    ct_listener_t listener;
    ct_connection_t client_connection;

    test_context.client_connections.push_back(&client_connection);

    ct_local_endpoint_t* listener_endpoint = ct_local_endpoint_new();
    ASSERT_NE(listener_endpoint, nullptr);

    ct_local_endpoint_with_interface(listener_endpoint, "lo");
    ct_local_endpoint_with_port(listener_endpoint, 6235);

    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));

    ct_transport_properties_t* listener_props = ct_transport_properties_new();
  ASSERT_NE(listener_props, nullptr);
    // Allocated with ct_transport_properties_new()

    ct_tp_set_sel_prop_preference(listener_props, RELIABILITY, PROHIBIT);

    ct_preconnection_t* listener_precon = ct_preconnection_new(remote_endpoint, 1, listener_props, NULL);
    ASSERT_NE(listener_precon, nullptr);
    ct_preconnection_set_local_endpoint(listener_precon, listener_endpoint);

    ct_preconnection_listen(listener_precon, &listener, on_connection_received_receive_message_close_listener_and_send_new_message);

    ct_listener_close(&listener);
    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    ct_start_event_loop();

    ct_free_local_endpoint(listener_endpoint);
    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(listener_precon);
    ct_transport_properties_free(listener_props);
}
