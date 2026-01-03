#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

#define QUIC_PING_PORT 4433
#define UDP_PING_PORT 5005
#define TCP_PING_PORT 5006
#define QUIC_CLONE_LISTENER_PORT 4434

class ConnectionCloneTest : public CTapsGenericFixture {};

TEST_F(ConnectionCloneTest, clonesConnectionSendsOnBothAndReceivesIndividualResponses) {
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, QUIC_PING_PORT);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);
    // Allocated with ct_transport_properties_new()
    ct_tp_set_sel_prop_preference(transport_properties, MULTISTREAMING, REQUIRE);

    ct_security_parameters_t security_parameters;
    ct_security_parameters_build(&security_parameters);
    char* alpn_strings = "simple-ping";
    ct_sec_param_set_property_string_array(&security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t* preconnection = ct_preconnection_new(&remote_endpoint, 1, transport_properties, &security_parameters);
    ASSERT_NE(preconnection, nullptr);

    ct_connection_callbacks_t connection_callbacks = {
        .establishment_error = on_establishment_error,
        .ready = clone_send_and_setup_receive_on_both,
        .user_connection_context = &test_context,
    };

    int rc = ct_preconnection_initiate(preconnection, connection_callbacks);
    ASSERT_EQ(rc, 0);

    ct_start_event_loop();

    log_info("Event loop completed, checking results");

    ct_connection_t* original = test_context.client_connections[0];
    ct_connection_t* cloned = test_context.client_connections[1];

    ASSERT_EQ(per_connection_messages.size(), 2);
    ASSERT_TRUE(ct_connection_is_closed(original));
    ASSERT_TRUE(ct_connection_is_closed(cloned));

    ASSERT_EQ(per_connection_messages[original].size(), 1);
    ASSERT_EQ(per_connection_messages[cloned].size(), 1);
    ASSERT_STREQ(per_connection_messages[original][0]->content, "Pong: ping-original");
    ASSERT_STREQ(per_connection_messages[cloned][0]->content, "Pong: ping-cloned");

    ct_free_security_parameter_content(&security_parameters);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}

TEST_F(ConnectionCloneTest, cloneWithListenerBothClientsSendAndReceiveResponses) {
    // --- SETUP SERVER/LISTENER ---
    ct_listener_t listener;
    test_context.listener = &listener;

    ct_local_endpoint_t listener_endpoint;
    ct_local_endpoint_build(&listener_endpoint);
    ct_local_endpoint_with_interface(&listener_endpoint, "lo");
    ct_local_endpoint_with_port(&listener_endpoint, QUIC_CLONE_LISTENER_PORT);

    ct_remote_endpoint_t listener_remote;
    ct_remote_endpoint_build(&listener_remote);
    ct_remote_endpoint_with_hostname(&listener_remote, "127.0.0.1");

    ct_transport_properties_t* listener_props = ct_transport_properties_new();
  ASSERT_NE(listener_props, nullptr);
    // Allocated with ct_transport_properties_new()
    ct_tp_set_sel_prop_preference(listener_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(listener_props, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(listener_props, MULTISTREAMING, REQUIRE); // Force QUIC

    ct_security_parameters_t server_security_parameters;
    ct_security_parameters_build(&server_security_parameters);
    char* alpn_strings = "simple-ping";
    ct_sec_param_set_property_string_array(&server_security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t* listener_precon = ct_preconnection_new(&listener_remote, 1, listener_props, &server_security_parameters);
    ASSERT_NE(listener_precon, nullptr);
    ct_preconnection_set_local_endpoint(listener_precon, &listener_endpoint);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = server_on_connection_received_for_cloning,
        .user_listener_context = &test_context
    };

    int listen_res = ct_preconnection_listen(listener_precon, &listener, listener_callbacks);
    ASSERT_EQ(listen_res, 0);
    log_info("Listener created on port %d", QUIC_CLONE_LISTENER_PORT);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t client_remote;
    ct_remote_endpoint_build(&client_remote);
    ct_remote_endpoint_with_hostname(&client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(&client_remote, QUIC_CLONE_LISTENER_PORT);

    ct_transport_properties_t* client_props = ct_transport_properties_new();
  ASSERT_NE(client_props, nullptr);
    // Allocated with ct_transport_properties_new()
    ct_tp_set_sel_prop_preference(client_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(client_props, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(client_props, MULTISTREAMING, REQUIRE);

    ct_security_parameters_t client_security_parameters;
    ct_security_parameters_build(&client_security_parameters);
    ct_sec_param_set_property_string_array(&client_security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t* client_precon = ct_preconnection_new(&client_remote, 1, client_props, &client_security_parameters);
    ASSERT_NE(client_precon, nullptr);

    ct_connection_callbacks_t client_callbacks = {
        .establishment_error = on_establishment_error,
        .ready = clone_send_and_setup_receive_on_both,
        .user_connection_context = &test_context,
    };

    int rc = ct_preconnection_initiate(client_precon, client_callbacks);
    ASSERT_EQ(rc, 0);

    // --- RUN EVENT LOOP ---
    log_info("Starting event loop");
    ct_start_event_loop();
    log_info("Event loop completed");

    // --- ASSERTIONS ---
    // Server connections: Could be 1 connection with 2 messages, or 2 connections with 1 each
    // Check total messages received by all server connections
    ASSERT_EQ(test_context.server_connections.size(), 2); // QUIC creates separate streams
    ASSERT_EQ(per_connection_messages[test_context.server_connections[0]].size(), 1);
    ASSERT_EQ(per_connection_messages[test_context.server_connections[1]].size(), 1);

    // Clients should have received 2 responses total (one per connection)
    // The map should have 4 entries: 2 client connections + 2 server connections
    ASSERT_EQ(per_connection_messages.size(), 4);

    ct_connection_t* original = test_context.client_connections[0];
    ct_connection_t* cloned = test_context.client_connections[1];

    // Each client connection should have received exactly 1 response
    ASSERT_EQ(per_connection_messages[original].size(), 1);
    ASSERT_EQ(per_connection_messages[cloned].size(), 1);

    // Verify message contents
    ASSERT_STREQ(per_connection_messages[original][0]->content, "Response: ping-original");
    ASSERT_STREQ(per_connection_messages[cloned][0]->content, "Response: ping-cloned");

    log_info("Test completed successfully");

    // --- CLEANUP ---
    ct_free_security_parameter_content(&server_security_parameters);
    ct_free_security_parameter_content(&client_security_parameters);
    ct_preconnection_free(client_precon);
    ct_transport_properties_free(client_props);
    ct_preconnection_free(listener_precon);
    ct_transport_properties_free(listener_props);
    ct_free_remote_endpoint_strings(&client_remote);
    ct_free_remote_endpoint_strings(&listener_remote);
    ct_close();
}

TEST_F(ConnectionCloneTest, clonesUdpConnectionSendsOnBothAndReceivesIndividualResponses) {
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, UDP_PING_PORT);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);
    // Allocated with ct_transport_properties_new()
    ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, CONGESTION_CONTROL, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(&remote_endpoint, 1, transport_properties, NULL);
    ASSERT_NE(preconnection, nullptr);

    ct_connection_callbacks_t connection_callbacks = {
        .establishment_error = on_establishment_error,
        .ready = clone_send_and_setup_receive_on_both,
        .user_connection_context = &test_context,
    };

    int rc = ct_preconnection_initiate(preconnection, connection_callbacks);
    ASSERT_EQ(rc, 0);

    ct_start_event_loop();

    log_info("Event loop completed, checking results");


    ct_connection_t* original = test_context.client_connections[0];
    ct_connection_t* cloned = test_context.client_connections[1];
    ASSERT_TRUE(ct_connection_is_closed(original));
    ASSERT_TRUE(ct_connection_is_closed(cloned));
    ASSERT_EQ(per_connection_messages.size(), 2);

    ASSERT_EQ(per_connection_messages[original].size(), 1);
    ASSERT_EQ(per_connection_messages[cloned].size(), 1);
    ASSERT_STREQ(per_connection_messages[original][0]->content, "Pong: ping-original");
    ASSERT_STREQ(per_connection_messages[cloned][0]->content, "Pong: ping-cloned");

    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}

TEST_F(ConnectionCloneTest, clonesTcpConnectionSendsOnBothAndReceivesIndividualResponses) {
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, TCP_PING_PORT);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);
    // Allocated with ct_transport_properties_new()
    // Force TCP selection
    ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, MULTISTREAMING, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(&remote_endpoint, 1, transport_properties, NULL);
    ASSERT_NE(preconnection, nullptr);

    ct_connection_callbacks_t connection_callbacks = {
        .establishment_error = on_establishment_error,
        .ready = clone_send_and_setup_receive_on_both,
        .user_connection_context = &test_context,
    };

    int rc = ct_preconnection_initiate(preconnection, connection_callbacks);
    ASSERT_EQ(rc, 0);

    ct_start_event_loop();

    log_info("Event loop completed, checking results");

    ct_connection_t* original = test_context.client_connections[0];
    ct_connection_t* cloned = test_context.client_connections[1];

    ASSERT_TRUE(ct_connection_is_closed(original));
    ASSERT_TRUE(ct_connection_is_closed(cloned));
    ASSERT_EQ(per_connection_messages.size(), 2);


    ASSERT_EQ(per_connection_messages[original].size(), 1);
    ASSERT_EQ(per_connection_messages[cloned].size(), 1);
    ASSERT_STREQ(per_connection_messages[original][0]->content, "Pong: ping-original");
    ASSERT_STREQ(per_connection_messages[cloned][0]->content, "Pong: ping-cloned");

    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}
