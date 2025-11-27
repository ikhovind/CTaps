#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "util/util.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

#define QUIC_PING_PORT 4433
#define QUIC_CLONE_LISTENER_PORT 4434

int receive_and_close_connection(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    log_info("Connection %p received message", (void*)connection);
    auto* ctx = static_cast<CallbackContext*>(message_context->user_receive_context);

    (*ctx->per_connection_messages)[connection].push_back(*received_message);

    ct_connection_close(connection);
    return 0;
}

int clone_send_and_setup_receive_on_both(ct_connection_t* connection) {
    log_info("Connection ready, cloning");
    auto* ctx = static_cast<CallbackContext*>(connection->connection_callbacks.user_connection_context);

    ct_connection_t* cloned = ct_connection_clone(connection);
    if (!cloned) {
        log_error("Failed to clone connection");
        ct_connection_close(connection);
        return -1;
    }

    log_info("Successfully cloned: original=%p, cloned=%p", (void*)connection, (void*)cloned);

    ctx->client_connections.push_back(connection);
    ctx->client_connections.push_back(cloned);

    ct_message_t message1;
    ct_message_build_with_content(&message1, "ping-original", strlen("ping-original") + 1);
    ct_send_message(connection, &message1);
    ct_message_free_content(&message1);

    ct_message_t message2;
    ct_message_build_with_content(&message2, "ping-cloned", strlen("ping-cloned") + 1);
    ct_send_message(cloned, &message2);
    ct_message_free_content(&message2);

    ct_receive_callbacks_t receive_req = {
        .receive_callback = receive_and_close_connection,
        .user_receive_context = ctx
    };

    ct_receive_message(connection, receive_req);
    ct_receive_message(cloned, receive_req);

    log_info("Sent messages and set up receives on both connections");
    return 0;
}

class ConnectionCloneTest : public CTapsGenericFixture {};

TEST_F(ConnectionCloneTest, clonesConnectionSendsOnBothAndReceivesIndividualResponses) {
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, QUIC_PING_PORT);

    ct_transport_properties_t transport_properties;
    ct_transport_properties_build(&transport_properties);
    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, REQUIRE);

    ct_security_parameters_t security_parameters;
    ct_security_parameters_build(&security_parameters);
    char* alpn_strings = "simple-ping";
    ct_sec_param_set_property_string_array(&security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t preconnection;
    ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, &security_parameters);
    ct_connection_t client_connection;

    ct_connection_callbacks_t connection_callbacks = {
        .establishment_error = on_establishment_error,
        .ready = clone_send_and_setup_receive_on_both,
        .user_connection_context = &test_context,
    };

    int rc = ct_preconnection_initiate(&preconnection, &client_connection, connection_callbacks);
    log_info("Created client connection: %p", (void*)&client_connection);
    ASSERT_EQ(rc, 0);

    ct_start_event_loop();

    log_info("Event loop completed, checking results");

    ASSERT_EQ(client_connection.transport_properties.connection_properties.list[STATE].value.enum_val, CONN_STATE_CLOSED);
    ASSERT_EQ(per_connection_messages.size(), 2);

    ct_connection_t* original = test_context.client_connections[0];
    ct_connection_t* cloned = test_context.client_connections[1];

    ASSERT_EQ(per_connection_messages[original].size(), 1);
    ASSERT_EQ(per_connection_messages[cloned].size(), 1);
    ASSERT_STREQ(per_connection_messages[original][0]->content, "Pong: ping-original");
    ASSERT_STREQ(per_connection_messages[cloned][0]->content, "Pong: ping-cloned");

    ct_free_security_parameter_content(&security_parameters);
    ct_preconnection_free(&preconnection);
}

// --- Callbacks for listener-based clone test ---

// Server callback: Receives a message from client, echoes it back with "Response: " prefix
int server_receive_and_respond(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    log_info("Server: Received message from connection %p: %s", (void*)connection, (*received_message)->content);
    auto* ctx = static_cast<CallbackContext*>(message_context->user_receive_context);

    // Store the received message
    ctx->messages->push_back(*received_message);

    // Send response with "Response: " prefix
    std::string response = std::string("Response: ") + std::string((char*)(*received_message)->content);
    ct_message_t response_msg;
    ct_message_build_with_content(&response_msg, response.c_str(), response.length() + 1);
    ct_send_message(connection, &response_msg);
    ct_message_free_content(&response_msg);

    log_info("Server: Sent response: %s", response.c_str());

    // Close listener after receiving both messages (original + clone)
    if (ctx->messages->size() >= 2 && ctx->listener) {
        log_info("Server: Received all expected messages, closing listener");
        ct_listener_close(ctx->listener);
        ctx->listener = nullptr;
    }

    // Set up to receive next message (in case there are more)
    ct_receive_callbacks_t receive_req = {
        .receive_callback = server_receive_and_respond,
        .user_receive_context = ctx
    };
    ct_receive_message(connection, receive_req);

    return 0;
}

// Server listener callback: When a connection is received, set up receive
int server_on_connection_received(ct_listener_t* listener, ct_connection_t* new_connection) {
    log_info("Server: New connection received %p", (void*)new_connection);
    auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_listener_context);
    context->server_connections.push_back(new_connection);

    // Don't close listener yet - need to receive all streams (original + cloned)
    // Listener will be cleaned up when test ends

    // Set up receive callback
    ct_receive_callbacks_t receive_req = {
        .receive_callback = server_receive_and_respond,
        .user_receive_context = listener->listener_callbacks.user_listener_context
    };

    ct_receive_message(new_connection, receive_req);
    return 0;
}

// Client receive callback: Store the response and close the connection
int client_receive_response_and_close(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    log_info("Client connection %p received message: %s", (void*)connection, (*received_message)->content);
    auto* ctx = static_cast<CallbackContext*>(message_context->user_receive_context);

    // Store message per connection
    (*ctx->per_connection_messages)[connection].push_back(*received_message);

    // Close this connection
    ct_connection_close(connection);

    return 0;
}

// Client ready callback for listener test: Clone connection, send messages on both, set up receives
int client_ready_clone_and_send_to_listener(ct_connection_t* connection) {
    log_info("Client: Connection ready, cloning");
    auto* ctx = static_cast<CallbackContext*>(connection->connection_callbacks.user_connection_context);

    // Clone the connection
    ct_connection_t* cloned = ct_connection_clone(connection);
    if (!cloned) {
        log_error("Failed to clone connection");
        ct_connection_close(connection);
        return -1;
    }

    log_info("Client: Successfully cloned: original=%p, cloned=%p", (void*)connection, (void*)cloned);

    // Store both connections
    ctx->client_connections.push_back(connection);
    ctx->client_connections.push_back(cloned);

    // Send message from original connection
    ct_message_t message1;
    ct_message_build_with_content(&message1, "Message from original", strlen("Message from original") + 1);
    ct_send_message(connection, &message1);
    ct_message_free_content(&message1);
    log_info("Client: Original sent message");

    // Send message from cloned connection
    ct_message_t message2;
    ct_message_build_with_content(&message2, "Message from clone", strlen("Message from clone") + 1);
    ct_send_message(cloned, &message2);
    ct_message_free_content(&message2);
    log_info("Client: Clone sent message");

    // Set up receives on both connections
    ct_receive_callbacks_t receive_req = {
        .receive_callback = client_receive_response_and_close,
        .user_receive_context = ctx
    };

    ct_receive_message(connection, receive_req);
    ct_receive_message(cloned, receive_req);

    log_info("Client: Set up receives on both connections");
    return 0;
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

    ct_transport_properties_t listener_props;
    ct_transport_properties_build(&listener_props);
    ct_tp_set_sel_prop_preference(&listener_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(&listener_props, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(&listener_props, MULTISTREAMING, REQUIRE); // Force QUIC

    ct_security_parameters_t server_security_parameters;
    ct_security_parameters_build(&server_security_parameters);
    char* alpn_strings = "simple-ping";
    ct_sec_param_set_property_string_array(&server_security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t listener_precon;
    ct_preconnection_build_with_local(&listener_precon, listener_props, &listener_remote, 1,
                                      &server_security_parameters, listener_endpoint);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = server_on_connection_received,
        .user_listener_context = &test_context
    };

    int listen_res = ct_preconnection_listen(&listener_precon, &listener, listener_callbacks);
    ASSERT_EQ(listen_res, 0);
    log_info("Listener created on port %d", QUIC_CLONE_LISTENER_PORT);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t client_remote;
    ct_remote_endpoint_build(&client_remote);
    ct_remote_endpoint_with_hostname(&client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(&client_remote, QUIC_CLONE_LISTENER_PORT);

    ct_transport_properties_t client_props;
    ct_transport_properties_build(&client_props);
    ct_tp_set_sel_prop_preference(&client_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(&client_props, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(&client_props, MULTISTREAMING, REQUIRE);

    ct_security_parameters_t client_security_parameters;
    ct_security_parameters_build(&client_security_parameters);
    ct_sec_param_set_property_string_array(&client_security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t client_precon;
    ct_preconnection_build(&client_precon, client_props, &client_remote, 1, &client_security_parameters);

    ct_connection_t client_connection;
    ct_connection_callbacks_t client_callbacks = {
        .establishment_error = on_establishment_error,
        .ready = client_ready_clone_and_send_to_listener,
        .user_connection_context = &test_context,
    };

    int rc = ct_preconnection_initiate(&client_precon, &client_connection, client_callbacks);
    log_info("Client connection initiated: %p", (void*)&client_connection);
    ASSERT_EQ(rc, 0);

    // --- RUN EVENT LOOP ---
    log_info("Starting event loop");
    ct_start_event_loop();
    log_info("Event loop completed");

    // --- ASSERTIONS ---
    // Server should have received 2 messages (one from original, one from clone)
    ASSERT_EQ(received_messages.size(), 2);

    // Clients should have received 2 responses total (one per connection)
    ASSERT_EQ(per_connection_messages.size(), 2);

    ct_connection_t* original = test_context.client_connections[0];
    ct_connection_t* cloned = test_context.client_connections[1];

    // Each client connection should have received exactly 1 response
    ASSERT_EQ(per_connection_messages[original].size(), 1);
    ASSERT_EQ(per_connection_messages[cloned].size(), 1);

    // Verify message contents
    ASSERT_STREQ(per_connection_messages[original][0]->content, "Response: Message from original");
    ASSERT_STREQ(per_connection_messages[cloned][0]->content, "Response: Message from clone");

    log_info("Test completed successfully");

    // --- CLEANUP ---
    ct_free_security_parameter_content(&server_security_parameters);
    ct_free_security_parameter_content(&client_security_parameters);
    ct_preconnection_free(&client_precon);
    ct_preconnection_free(&listener_precon);
    ct_free_remote_endpoint_strings(&client_remote);
    ct_free_remote_endpoint_strings(&listener_remote);
    ct_close();
}
