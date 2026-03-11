#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
#include "fixtures/integration_fixture.h"
extern "C" {
#include "ctaps.h"
#include <logging/log.h>
}

class QuicListenTests : public CTapsGenericFixture {};

TEST_F(QuicListenTests, quicReceivesConnectionFromListenerAndExchangesMessages) {
    ct_local_endpoint_t* listener_endpoint = ct_local_endpoint_new();
    ASSERT_NE(listener_endpoint, nullptr);

    ct_local_endpoint_with_interface(listener_endpoint, "lo");
    ct_local_endpoint_with_port(listener_endpoint, 1239);

    ct_remote_endpoint_t* listener_remote = ct_remote_endpoint_new();
    ASSERT_NE(listener_remote, nullptr);
    ct_remote_endpoint_with_hostname(listener_remote, "127.0.0.1");

    ct_transport_properties_t* listener_props = ct_transport_properties_new();
    ASSERT_NE(listener_props, nullptr);
    ct_transport_properties_set_reliability(listener_props, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(listener_props, REQUIRE);
    ct_transport_properties_set_multistreaming(listener_props, REQUIRE); // force QUIC

    ct_security_parameters_t* server_security_parameters = ct_security_parameters_new();
    ASSERT_NE(server_security_parameters, nullptr);
    const char* alpn_strings = "simple-ping";
    ct_security_parameters_add_alpn(server_security_parameters, alpn_strings);

    ct_security_parameters_add_server_certificate(server_security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_preconnection_t* listener_precon = ct_preconnection_new(listener_endpoint, 1, listener_remote, 1, listener_props, server_security_parameters);
    ASSERT_NE(listener_precon, nullptr);
    ct_security_parameters_free(server_security_parameters);

    ct_listener_callbacks_t listener_callbacks = {
        .listener_ready = on_listener_ready_print_socket_manager_count,
        .connection_received = receive_message_respond_and_close_listener_on_connection_received,
        .user_listener_context = &test_context
    };

    int rc = ct_preconnection_listen(listener_precon, listener_callbacks, NULL);
    ASSERT_EQ(rc, 0);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t* client_remote = ct_remote_endpoint_new();
    ASSERT_NE(client_remote, nullptr);
    ct_remote_endpoint_with_hostname(client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(client_remote, 1239);

    ct_transport_properties_t* client_props = ct_transport_properties_new();
    ASSERT_NE(client_props, nullptr);

    ct_transport_properties_set_reliability(client_props, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(client_props, REQUIRE);
    ct_transport_properties_set_multistreaming(client_props, REQUIRE);

    ct_security_parameters_t* client_security_parameters = ct_security_parameters_new();
    ASSERT_NE(client_security_parameters, nullptr);
    ct_security_parameters_add_alpn(client_security_parameters, alpn_strings);

    ct_security_parameters_add_client_certificate(client_security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_preconnection_t* client_precon = ct_preconnection_new(NULL, 0, client_remote, 1, client_props, client_security_parameters);
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
}

TEST_F(QuicListenTests, ServerInitiatesStreamByWritingFirst) {
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
    ct_transport_properties_set_reliability(listener_props, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(listener_props, REQUIRE);
    ct_transport_properties_set_multistreaming(listener_props, REQUIRE); // force QUIC

    ct_security_parameters_t* server_security_parameters = ct_security_parameters_new();
    ASSERT_NE(server_security_parameters, nullptr);
    const char* alpn_strings = "simple-ping";
    ct_security_parameters_add_alpn(server_security_parameters, alpn_strings);

    ct_security_parameters_add_server_certificate(server_security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_preconnection_t* listener_precon = ct_preconnection_new(listener_endpoint, 1, listener_remote, 1, listener_props, server_security_parameters);
    ASSERT_NE(listener_precon, nullptr);
    ct_security_parameters_free(server_security_parameters);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = server_sends_first_and_waits_for_response,
        .user_listener_context = &test_context
    };

    int rc = ct_preconnection_listen(listener_precon, listener_callbacks, NULL);
    ASSERT_EQ(rc, 0);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t* client_remote = ct_remote_endpoint_new();
    ASSERT_NE(client_remote, nullptr);
    ct_remote_endpoint_with_hostname(client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(client_remote, 1240);

    ct_transport_properties_t* client_props = ct_transport_properties_new();
    ASSERT_NE(client_props, nullptr);
    ct_transport_properties_set_reliability(client_props, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(client_props, REQUIRE);
    ct_transport_properties_set_multistreaming(client_props, REQUIRE);

    ct_security_parameters_t* client_security_parameters = ct_security_parameters_new();
    ASSERT_NE(client_security_parameters, nullptr);
    ct_security_parameters_add_alpn(client_security_parameters, alpn_strings);

    ct_security_parameters_add_client_certificate(client_security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_preconnection_t* client_precon = ct_preconnection_new(NULL, 0, client_remote, 1, client_props, client_security_parameters);
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
}

TEST_F(QuicListenTests, listenerCanReceive0RttMessage) {
    ct_local_endpoint_t* listener_endpoint = ct_local_endpoint_new();
    ASSERT_NE(listener_endpoint, nullptr);

    ct_local_endpoint_with_interface(listener_endpoint, "lo");
    ct_local_endpoint_with_port(listener_endpoint, 1239);

    ct_remote_endpoint_t* listener_remote = ct_remote_endpoint_new();
    ASSERT_NE(listener_remote, nullptr);
    ct_remote_endpoint_with_hostname(listener_remote, "127.0.0.1");

    ct_transport_properties_t* listener_props = ct_transport_properties_new();
    ASSERT_NE(listener_props, nullptr);
    ct_transport_properties_set_reliability(listener_props, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(listener_props, REQUIRE);
    ct_transport_properties_set_multistreaming(listener_props, REQUIRE); // force QUIC

    ct_security_parameters_t* server_security_parameters = ct_security_parameters_new();

    const char* alpn_strings = "simple-ping";
    ct_security_parameters_add_alpn(server_security_parameters, alpn_strings);

    const uint8_t* data = (const uint8_t*)"0123456789abcdef";
    ct_security_parameters_set_session_ticket_encryption_key(server_security_parameters,
                                                             data,
                                                             16);

    ct_security_parameters_add_server_certificate(server_security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_preconnection_t* listener_precon = ct_preconnection_new(listener_endpoint, 1, listener_remote, 1, listener_props, server_security_parameters);
    ASSERT_NE(listener_precon, nullptr);
    ct_security_parameters_free(server_security_parameters);

    ct_listener_callbacks_t listener_callbacks = {
        .listener_ready = on_listener_ready_print_socket_manager_count,
        .connection_received = receive_message_respond_and_close_listener_on_connection_received,
        .user_listener_context = &test_context
    };

    int rc = ct_preconnection_listen(listener_precon, listener_callbacks, NULL);
    ASSERT_EQ(rc, 0);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t* client_remote = ct_remote_endpoint_new();
    ASSERT_NE(client_remote, nullptr);
    ct_remote_endpoint_with_hostname(client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(client_remote, 1239);

    ct_transport_properties_t* client_props = ct_transport_properties_new();
    ASSERT_NE(client_props, nullptr);

    ct_transport_properties_set_reliability(client_props, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(client_props, REQUIRE);
    ct_transport_properties_set_multistreaming(client_props, REQUIRE);

    ct_security_parameters_t* client_security_parameters = ct_security_parameters_new();
    ASSERT_NE(client_security_parameters, nullptr);
    ct_security_parameters_add_alpn(client_security_parameters, alpn_strings);
    ct_security_parameters_set_server_name_identification(client_security_parameters, "localhost");

    ct_security_parameters_add_client_certificate(client_security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_security_parameters_set_ticket_store_path(client_security_parameters, TEST_CLIENT_TICKET_STORE);

    ct_preconnection_t* client_precon = ct_preconnection_new(NULL, 0, client_remote, 1, client_props, client_security_parameters);
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

    ASSERT_EQ(test_context.client_connections.size(), 1);
    // Client receives "pong"
    ct_connection_t* client_connection = test_context.client_connections[0];
    ASSERT_EQ(per_connection_messages[client_connection].size(), 1);
    ASSERT_EQ(per_connection_messages[client_connection][0]->length, 5);
    ASSERT_STREQ(per_connection_messages[client_connection][0]->content, "pong");

    // Server receives "ping"
    ASSERT_EQ(test_context.server_connections.size(), 1);
    ct_connection_t* server_connection = test_context.server_connections[0];
    ASSERT_EQ(per_connection_messages[server_connection].size(), 1);
    ASSERT_EQ(per_connection_messages[server_connection][0]->length, 5);
    ASSERT_STREQ(per_connection_messages[server_connection][0]->content, "ping");
    ASSERT_FALSE(ct_connection_sent_early_data(client_connection));

    // ============= second round with 0-RTT =============
    rc = ct_preconnection_listen(listener_precon, listener_callbacks, NULL);
    ASSERT_EQ(rc, 0);

    // --- SETUP CLIENT ---
    ct_message_t* early_data_msg = ct_message_new();
    ct_message_set_content(early_data_msg, "ping", 5);

    ct_message_context_t* early_data_ctx = ct_message_context_new();
    ct_message_context_set_safely_replayable(early_data_ctx, true);

    ct_preconnection_initiate_with_send(client_precon, client_callbacks, early_data_msg, early_data_ctx);
    ct_message_context_free(early_data_ctx);
    ct_message_free(early_data_msg);

    ct_start_event_loop();

    // --- ASSERTIONS ---
    ASSERT_EQ(per_connection_messages.size(), 4); // two rounds of client and server connections

    ct_connection_t* client_connection2 = test_context.client_connections[1];

    // Client receives "pong"
    ASSERT_EQ(per_connection_messages[client_connection2].size(), 1);
    ASSERT_EQ(per_connection_messages[client_connection2][0]->length, 5);
    ASSERT_STREQ(per_connection_messages[client_connection2][0]->content, "pong");

    // Server receives "ping"
    ASSERT_EQ(test_context.server_connections.size(), 2);
    ct_connection_t* server_connection2 = test_context.server_connections[1];
    ASSERT_EQ(per_connection_messages[server_connection2].size(), 1);
    ASSERT_EQ(per_connection_messages[server_connection2][0]->length, 5);
    ASSERT_STREQ(per_connection_messages[server_connection2][0]->content, "ping");
    ASSERT_TRUE(ct_connection_sent_early_data(client_connection2));

    ct_local_endpoint_free(listener_endpoint);
    ct_remote_endpoint_free(listener_remote);
    ct_remote_endpoint_free(client_remote);
    ct_preconnection_free(client_precon);
    ct_transport_properties_free(client_props);
    ct_preconnection_free(listener_precon);
    ct_transport_properties_free(listener_props);
}
