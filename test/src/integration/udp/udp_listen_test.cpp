#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "logging/log.h"

#include "ctaps.h"
}
#include "fixtures/awaiting_fixture.cpp"

void on_listener_ready(ct_listener_t* listener) {
    log_debug("Listener is ready: %p", (void*)listener);
    auto* ctx = static_cast<CallbackContext*>(listener->listener_callbacks.user_listener_context);
    if (ctx && ctx->listener_ready_action) {
        ctx->listener_ready_action();
    }
}

class UdpListenTests : public CTapsGenericFixture {};

TEST_F(UdpListenTests, ReceivesConnectionFromListenerAndExchangesMessages) {
    ct_local_endpoint_t* listener_endpoint = ct_local_endpoint_new();
    ct_local_endpoint_with_interface(listener_endpoint, "lo");
    ct_local_endpoint_with_port(listener_endpoint, 1239);

    ct_remote_endpoint_t* listener_remote = ct_remote_endpoint_new();
    ct_remote_endpoint_with_hostname(listener_remote, "localhost");

    ct_transport_properties_t* udp_props = ct_transport_properties_new();
    ct_transport_properties_set_reliability(udp_props, PROHIBIT);
    ct_transport_properties_set_preserve_order(udp_props, PROHIBIT);
    ct_transport_properties_set_congestion_control(udp_props, PROHIBIT);

    ct_preconnection_t* listener_precon = ct_preconnection_new(listener_remote, 1, udp_props, NULL);
    ct_preconnection_set_local_endpoint(listener_precon, listener_endpoint);

    // Set up the client preconnection ahead of time, but don't initiate yet
    ct_remote_endpoint_t* client_remote = ct_remote_endpoint_new();
    ct_remote_endpoint_with_hostname(client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(client_remote, 1239);

    ct_preconnection_t* client_precon = ct_preconnection_new(client_remote, 1, udp_props, NULL);
    ASSERT_NE(client_precon, nullptr);

    ct_connection_callbacks_t client_callbacks {
        .ready = send_message_and_receive,
        .user_connection_context = &test_context
    };

    // Initiation is deferred until the listener signals it's ready
    test_context.listener_ready_action = [client_precon, client_callbacks]() {
        ct_preconnection_initiate(client_precon, client_callbacks);
    };

    ct_listener_callbacks_t listener_callbacks = {
        .listener_ready = on_listener_ready,
        .connection_received = receive_message_respond_and_close_listener_on_connection_received,
        .user_listener_context = &test_context
    };

    ct_listener_t* listener = ct_preconnection_listen(listener_precon, listener_callbacks);

    // --- RUN EVENT LOOP ---
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
    ct_transport_properties_free(udp_props);
    ct_preconnection_free(client_precon);
}
