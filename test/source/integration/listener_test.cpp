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

// 1. A state struct to coordinate the test
typedef struct {
    void* received_content;
    bool is_done;
    Connection* client_connection; // Pointer to the client to close it later
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    Listener* listener;
} TestState;

int receive_callback(struct Connection* connection, Message** received_message, void* user_data) {
    TestState* state = (TestState*)user_data;
    state->received_content = *received_message;
    printf("Received message on server connection.\n");
}

// 2. Callbacks for both listener and client

// This is the main success callback for the test
int on_server_connection_received(Listener* source, Connection* new_server_conn) {
    TestState* state = (TestState*)source->user_data;
    printf("State poiner is: %p\n", source->user_data);
    printf("Listener pointer is: %p\n", source);

    // We can now post a receive request on this new connection
    // and wait for the message that the client is about to send.
    // (This part requires a more complex state machine to fully test,
    // but for now, we can confirm this callback was hit).

    ReceiveMessageRequest receive_message_request = {
      .receive_cb = receive_callback,
      .user_data = state,
    };
    receive_message(new_server_conn, receive_message_request);

    state->is_done = true; // Signal that the main goal of the test was achieved

    // Clean up to allow the event loop to exit
    listener_close(state->listener);

    connection_close(new_server_conn);
    connection_close(state->client_connection);

    return 0;
}

// Callback for when the CLIENT is ready to send
int on_client_ready(Connection* client_conn, void* user_data) {
    TestState* state = (TestState*)user_data;
    printf("Client: Connection is ready. Sending message...\n");

    Message message;
    message_build_with_content(&message, "ping", strlen("ping") + 1);
    send_message(client_conn, &message);
    message_free_content(&message);
    connection_close(client_conn); // Close after sending
}

// Use a test fixture for clean SetUp/TearDown
class ListenerTest : public ::testing::Test {
protected:
    TestState test_state = {};

    void SetUp() override {
        ctaps_initialize();
        printf("Setting up test state\n");
        memset(&test_state, 0, sizeof(TestState));
        pthread_mutex_init(&test_state.mutex, NULL);
        pthread_cond_init(&test_state.cond, NULL);
    }
    void TearDown() override {
        pthread_mutex_destroy(&test_state.mutex);
        pthread_cond_destroy(&test_state.cond);
        free(test_state.received_content);
    }
};


TEST_F(ListenerTest, ReceivesPacketFromLocalClient) {
    printf("test_state address is: %p\n", (void*)&test_state);
    // --- SETUP LISTENER ---
    LocalEndpoint listener_endpoint;

    local_endpoint_with_ipv4(&listener_endpoint, inet_addr("127.0.0.1"));
    local_endpoint_with_port(&listener_endpoint, 1234);

    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);
    remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");

    TransportProperties listener_props;
    transport_properties_build(&listener_props);

    Preconnection listener_precon;
    preconnection_build_with_local(&listener_precon, listener_props, &remote_endpoint, 1, listener_endpoint);

    Listener* listener = (Listener*)malloc(sizeof(Listener));
    // The user_data for the listener is our test state
    preconnection_listen(&listener_precon, listener, on_server_connection_received, &test_state);

    test_state.listener = listener;
    printf("Listener pointer is: %p\n", listener->user_data);

    // --- SETUP CLIENT ---
    RemoteEndpoint client_remote;
    remote_endpoint_with_hostname(&client_remote, "127.0.0.1");
    remote_endpoint_with_port(&client_remote, 1234); // Point to the listener

    TransportProperties client_props;
    transport_properties_build(&client_props);

    Preconnection client_precon;
    preconnection_build(&client_precon, client_props, &client_remote, 1);

    Connection client_connection;
    test_state.client_connection = &client_connection; // Save pointer for cleanup

    InitDoneCb client_ready = {
        .init_done_callback = on_client_ready,
        .user_data = &test_state
    };
    preconnection_initiate(&client_precon, &client_connection, client_ready, nullptr);

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    ctaps_start_event_loop();

    Message* received_message = (Message*) test_state.received_content;

    // --- ASSERTIONS ---
    // We don't need to "await" with a cond_wait here because ctaps_start_event_loop
    // only returns after the callbacks have run and closed the connections.
    ASSERT_TRUE(test_state.is_done);
    EXPECT_STREQ(received_message->content, "ping");
}






