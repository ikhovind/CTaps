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
