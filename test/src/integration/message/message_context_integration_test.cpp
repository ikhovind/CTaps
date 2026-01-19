#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include <logging/log.h>
#include "fixtures/awaiting_fixture.cpp"
}

class MessageContextIntegrationTests : public CTapsGenericFixture {};

TEST_F(MessageContextIntegrationTests, messageContextContainsValidEndpointsOnReceiveForUdp) {
    log_info("Starting test: messageContextContainsValidEndpointsOnReceive");

    // Setup remote endpoint to UDP ping server
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, UDP_PING_PORT);
    log_info("Using UDP PING PORT: %d", UDP_PING_PORT);

    // Setup transport properties for UDP
    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    ASSERT_NE(transport_properties, nullptr);
    ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, CONGESTION_CONTROL, PROHIBIT);

    // Create preconnection
    ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, nullptr);
    ASSERT_NE(preconnection, nullptr);

    // Set expected remote port for verification
    test_context.expected_server_port = UDP_PING_PORT;

    // Setup connection callbacks
    ct_connection_callbacks_t connection_callbacks = {
        .establishment_error = on_establishment_error,
        .ready = send_message_and_verify_context_on_receive,
        .user_connection_context = &test_context,
    };

    // Initiate connection
    int rc = ct_preconnection_initiate(preconnection, connection_callbacks);
    ASSERT_EQ(rc, 0);

    // Run event loop
    ct_start_event_loop();

    ASSERT_EQ(per_connection_messages.size(), 1);

    // Cleanup
    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
    log_info("Completed test: messageContextContainsValidEndpointsOnReceive");
}

TEST_F(MessageContextIntegrationTests, messageContextContainsValidEndpointsOnReceiveForTcp) {
    log_info("Starting test: messageContextContainsValidEndpointsOnReceive");

    // Setup remote endpoint to UDP ping server
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, TCP_PING_PORT);

    // Setup transport properties for UDP
    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    ASSERT_NE(transport_properties, nullptr);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_MSG_BOUNDARIES, PROHIBIT); // force tcp

    // Create preconnection
    ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, nullptr);
    ASSERT_NE(preconnection, nullptr);

    // Set expected remote port for verification
    test_context.expected_server_port = TCP_PING_PORT;

    // Setup connection callbacks
    ct_connection_callbacks_t connection_callbacks = {
        .establishment_error = on_establishment_error,
        .ready = send_message_and_verify_context_on_receive,
        .user_connection_context = &test_context,
    };

    // Initiate connection
    int rc = ct_preconnection_initiate(preconnection, connection_callbacks);
    ASSERT_EQ(rc, 0);

    // Run event loop
    ct_start_event_loop();

    // assert one message was received
    ASSERT_EQ(per_connection_messages.size(), 1);

    // Cleanup
    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}

TEST_F(MessageContextIntegrationTests, messageContextContainsValidEndpointsOnReceiveForQuic) {
    log_info("Starting test: messageContextContainsValidEndpointsOnReceive");

    // Setup remote endpoint to UDP ping server
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);

    // Setup transport properties for UDP
    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    ASSERT_NE(transport_properties, nullptr);
    ct_tp_set_sel_prop_preference(transport_properties, MULTISTREAMING, REQUIRE); // force quic
    //

    ct_security_parameters_t* security_parameters = ct_security_parameters_new();
    char* alpn_strings = "simple-ping";
    ct_sec_param_set_property_string_array(security_parameters, ALPN, &alpn_strings, 1);

    ct_certificate_bundles_t* client_bundles = ct_certificate_bundles_new();
    ct_certificate_bundles_add_cert(client_bundles, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");
    ct_sec_param_set_property_certificate_bundles(security_parameters, CLIENT_CERTIFICATE, client_bundles);
    ct_certificate_bundles_free(client_bundles);

    // Create preconnection
    ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, security_parameters);
    ASSERT_NE(preconnection, nullptr);

    // Set expected remote port for verification
    test_context.expected_server_port = QUIC_PING_PORT;

    // Setup connection callbacks
    ct_connection_callbacks_t connection_callbacks = {
        .establishment_error = on_establishment_error,
        .ready = send_message_and_verify_context_on_receive,
        .user_connection_context = &test_context,
    };

    // Initiate connection
    int rc = ct_preconnection_initiate(preconnection, connection_callbacks);
    ASSERT_EQ(rc, 0);

    // Run event loop
    ct_start_event_loop();

    ASSERT_EQ(per_connection_messages.size(), 1);

    // Cleanup
    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}

TEST_F(MessageContextIntegrationTests, messageContextContainsValidEndpointsOnReceiveForTcpListner) {
    ct_listener_t listener;

    // Set expected remote port for verification
    test_context.expected_server_port = QUIC_PING_PORT;

    ct_local_endpoint_t* listener_endpoint = ct_local_endpoint_new();
    ASSERT_NE(listener_endpoint, nullptr);

    ct_local_endpoint_with_interface(listener_endpoint, "lo");
    ct_local_endpoint_with_port(listener_endpoint, QUIC_PING_PORT);

    ct_remote_endpoint_t* listener_remote = ct_remote_endpoint_new();
    ASSERT_NE(listener_remote, nullptr);
    ct_remote_endpoint_with_hostname(listener_remote, "127.0.0.1");

    ct_transport_properties_t* listener_props = ct_transport_properties_new();
    ASSERT_NE(listener_props, nullptr);

    ct_tp_set_sel_prop_preference(listener_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(listener_props, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
    ct_tp_set_sel_prop_preference(listener_props, MULTISTREAMING, PROHIBIT);

    ct_preconnection_t* listener_precon = ct_preconnection_new(listener_remote, 1, listener_props, NULL);
    ASSERT_NE(listener_precon, nullptr);
    ct_preconnection_set_local_endpoint(listener_precon, listener_endpoint);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = receive_message_verify_and_close_listener_on_connection_received,
        .user_listener_context = &test_context
    };

    int listen_res = ct_preconnection_listen(listener_precon, &listener, listener_callbacks);

    ASSERT_EQ(listen_res, 0);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t* client_remote = ct_remote_endpoint_new();
    ASSERT_NE(client_remote, nullptr);
    ct_remote_endpoint_with_hostname(client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(client_remote, QUIC_PING_PORT);

    ct_transport_properties_t* client_props = ct_transport_properties_new();
    ASSERT_NE(client_props, nullptr);

    ct_tp_set_sel_prop_preference(client_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(client_props, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
    ct_tp_set_sel_prop_preference(client_props, MULTISTREAMING, PROHIBIT);

    ct_preconnection_t* client_precon = ct_preconnection_new(client_remote, 1, client_props, NULL);
    ASSERT_NE(client_precon, nullptr);

    // Custom ready callback that saves connection and calls original ready

    ct_connection_callbacks_t client_callbacks {
        .ready = send_message_and_verify_context_on_receive,
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
    ct_local_endpoint_free(listener_endpoint);
    ct_remote_endpoint_free(listener_remote);
    ct_remote_endpoint_free(client_remote);
    ct_preconnection_free(listener_precon);
    ct_transport_properties_free(listener_props);
    ct_preconnection_free(client_precon);
    ct_transport_properties_free(client_props);
}
