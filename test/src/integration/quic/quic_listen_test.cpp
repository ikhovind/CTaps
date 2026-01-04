#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include <logging/log.h>
}
#include "fixtures/awaiting_fixture.cpp"

class QuicListenTests : public CTapsGenericFixture {};

TEST_F(QuicListenTests, QuicReceivesConnectionFromListenerAndExchangesMessages) {
    ct_listener_t listener;

    ct_local_endpoint_t* listener_endpoint = ct_local_endpoint_new();
    ASSERT_NE(listener_endpoint, nullptr);

    ct_local_endpoint_with_interface(listener_endpoint, "lo");
    ct_local_endpoint_with_port(listener_endpoint, 1239);

    ct_remote_endpoint_t* listener_remote = ct_remote_endpoint_new();
    ASSERT_NE(listener_remote, nullptr);
    ct_remote_endpoint_with_hostname(listener_remote, "127.0.0.1");

    ct_transport_properties_t* listener_props = ct_transport_properties_new();
    ASSERT_NE(listener_props, nullptr);
    ct_tp_set_sel_prop_preference(listener_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(listener_props, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(listener_props, MULTISTREAMING, REQUIRE); // force QUIC

    ct_security_parameters_t* server_security_parameters = ct_security_parameters_new();
    ASSERT_NE(server_security_parameters, nullptr);
    char* alpn_strings = "simple-ping";
    ct_sec_param_set_property_string_array(server_security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t* listener_precon = ct_preconnection_new(listener_remote, 1, listener_props, server_security_parameters);
    ASSERT_NE(listener_precon, nullptr);
    ct_security_parameters_free(server_security_parameters);
    ct_preconnection_set_local_endpoint(listener_precon, listener_endpoint);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = receive_message_respond_and_close_listener_on_connection_received,
        .user_listener_context = &test_context
    };

    int listen_res = ct_preconnection_listen(listener_precon, &listener, listener_callbacks);

    ASSERT_EQ(listen_res, 0);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t* client_remote = ct_remote_endpoint_new();
    ASSERT_NE(client_remote, nullptr);
    ct_remote_endpoint_with_hostname(client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(client_remote, 1239);

    ct_transport_properties_t* client_props = ct_transport_properties_new();
    ASSERT_NE(client_props, nullptr);

    ct_tp_set_sel_prop_preference(client_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(client_props, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(client_props, MULTISTREAMING, REQUIRE);

    ct_security_parameters_t* client_security_parameters = ct_security_parameters_new();
    ASSERT_NE(client_security_parameters, nullptr);
    ct_sec_param_set_property_string_array(client_security_parameters, ALPN, &alpn_strings, 1);


    ct_preconnection_t* client_precon = ct_preconnection_new(client_remote, 1, client_props, client_security_parameters);
    ASSERT_NE(client_precon, nullptr);
    ct_security_parameters_free(client_security_parameters);


    ct_connection_callbacks_t client_callbacks {
        .ready = send_message_and_receive,
        .user_connection_context = &test_context
    };

    ct_preconnection_initiate(client_precon, client_callbacks);
  /*

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    */
    ct_start_event_loop();

    // --- ASSERTIONS ---
    ASSERT_EQ(per_connection_messages.size(), 2); // Both client and server connections

    ct_connection_t* client_connection = test_context.client_connections[0];

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

    ct_local_endpoint_free(listener_endpoint);
    ct_remote_endpoint_free(listener_remote);
    ct_remote_endpoint_free(client_remote);
    ct_preconnection_free(client_precon);
    ct_transport_properties_free(client_props);
    ct_preconnection_free(listener_precon);
    ct_transport_properties_free(listener_props);
    ct_close();
}

TEST_F(QuicListenTests, ServerInitiatesStreamByWritingFirst) {
    ct_listener_t listener;

    test_context.listener = &listener;

    // --- SETUP SERVER/LISTENER ---
    ct_local_endpoint_t* listener_endpoint = ct_local_endpoint_new();
    ASSERT_NE(listener_endpoint, nullptr);
    ct_local_endpoint_with_interface(listener_endpoint, "lo");
    ct_local_endpoint_with_port(listener_endpoint, 1240);

    ct_remote_endpoint_t* listener_remote = ct_remote_endpoint_new();
    ASSERT_NE(listener_remote, nullptr);
    ct_remote_endpoint_with_hostname(listener_remote, "127.0.0.1");

    ct_transport_properties_t* listener_props = ct_transport_properties_new();
    ASSERT_NE(listener_props, nullptr);
    ct_tp_set_sel_prop_preference(listener_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(listener_props, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(listener_props, MULTISTREAMING, REQUIRE); // force QUIC

    ct_security_parameters_t* server_security_parameters = ct_security_parameters_new();
    ASSERT_NE(server_security_parameters, nullptr);
    char* alpn_strings = "simple-ping";
    ct_sec_param_set_property_string_array(server_security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t* listener_precon = ct_preconnection_new(listener_remote, 1, listener_props, server_security_parameters);
    ASSERT_NE(listener_precon, nullptr);
    ct_security_parameters_free(server_security_parameters);
    ct_preconnection_set_local_endpoint(listener_precon, listener_endpoint);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = server_sends_first_and_waits_for_response,
        .user_listener_context = &test_context
    };

    int listen_res = ct_preconnection_listen(listener_precon, &listener, listener_callbacks);
    ASSERT_EQ(listen_res, 0);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t* client_remote = ct_remote_endpoint_new();
    ASSERT_NE(client_remote, nullptr);
    ct_remote_endpoint_with_hostname(client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(client_remote, 1240);

    ct_transport_properties_t* client_props = ct_transport_properties_new();
    ASSERT_NE(client_props, nullptr);
    ct_tp_set_sel_prop_preference(client_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(client_props, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(client_props, MULTISTREAMING, REQUIRE);

    ct_security_parameters_t* client_security_parameters = ct_security_parameters_new();
    ASSERT_NE(client_security_parameters, nullptr);
    ct_sec_param_set_property_string_array(client_security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t* client_precon = ct_preconnection_new(client_remote, 1, client_props, client_security_parameters);
    ASSERT_NE(client_precon, nullptr);
    ct_security_parameters_free(client_security_parameters);


    ct_connection_callbacks_t client_callbacks {
        .ready = client_ready_wait_for_server,
        .user_connection_context = &test_context
    };

    ct_preconnection_initiate(client_precon, client_callbacks);

    // --- RUN EVENT LOOP ---
    ct_start_event_loop();

    // --- ASSERTIONS ---
    // Should have 2 messages: server's "server-hello" and client's "client-ack"
    ASSERT_EQ(per_connection_messages.size(), 2); // Both client and server connections

    ct_connection_t* client_connection = test_context.client_connections[0];
    // Client receives "server-hello"
    ASSERT_EQ(per_connection_messages[client_connection].size(), 1);
    ASSERT_EQ(per_connection_messages[client_connection][0]->length, strlen("server-hello") + 1);
    ASSERT_STREQ(per_connection_messages[client_connection][0]->content, "server-hello");

    // Server receives "client-ack"
    ASSERT_EQ(test_context.server_connections.size(), 1);
    ct_connection_t* server_connection = test_context.server_connections[0];
    ASSERT_EQ(per_connection_messages[server_connection].size(), 1);
    ASSERT_EQ(per_connection_messages[server_connection][0]->length, strlen("client-ack") + 1);
    ASSERT_STREQ(per_connection_messages[server_connection][0]->content, "client-ack");

    ct_local_endpoint_free(listener_endpoint);
    ct_remote_endpoint_free(listener_remote);
    ct_remote_endpoint_free(client_remote);
    ct_preconnection_free(client_precon);
    ct_transport_properties_free(client_props);
    ct_preconnection_free(listener_precon);
    ct_transport_properties_free(listener_props);
    ct_close();
}
