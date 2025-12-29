#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include <logging/log.h>
}

struct CallbackContext {
    std::map<ct_connection_t*, std::vector<ct_message_t*>>* per_connection_messages;
    std::vector<ct_connection_t*> server_connections; // created by the listener callback
    std::vector<ct_connection_t*> client_connections;
    std::function<void(CallbackContext*)>* closing_function; // To close any connections/listeners etc.
    size_t total_expected_messages;
    ct_listener_t* listener;
    bool connection_succeeded = false;
};

class CTapsGenericFixture : public ::testing::Test {
protected:
    // Each test gets its own per-connection message map
    std::map<ct_connection_t*, std::vector<ct_message_t*>> per_connection_messages;
    std::vector<ct_connection_t*> received_connections;
    std::vector<ct_connection_t*> client_connections;
    CallbackContext test_context;


    void SetUp() override {
        int rc = ct_initialize(TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");
        ct_set_log_level(CT_LOG_TRACE);
        ASSERT_EQ(rc, 0);

        test_context.per_connection_messages = &per_connection_messages;
        test_context.server_connections = received_connections;
        test_context.client_connections = client_connections;
        test_context.closing_function = nullptr;
        test_context.total_expected_messages = 1;
        test_context.listener = nullptr;
        test_context.connection_succeeded = false;
    }

    void TearDown() override {
        // Clean up any messages the test didn't
        for (auto& pair : per_connection_messages) {
            for (ct_message_t* msg : pair.second) {
                ct_message_free_all(msg);
            }
        }
    }


};

// --- C-style callbacks that bridge to our C++ object ---

int on_connection_ready(ct_connection_t* connection) {
    printf("ct_callback_t: ct_connection_t is ready.\n");
    auto* context = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    context->client_connections.push_back(connection);
    return 0;
}

int send_message_on_connection_ready(ct_connection_t* connection) {
    printf("ct_callback_t: ct_connection_t is ready, sending message.\n");
    auto* ctx = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    ctx->client_connections.push_back(connection);

    // Send the message now that the client is ready
    ct_message_t message;
    ct_message_build_with_content(&message, "ping", strlen("ping") + 1);
    ct_send_message(connection, &message);
    ct_message_free_content(&message);

    return 0;
}

int on_message_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    printf("ct_callback_t: on_message_received.\n");
    auto* ctx = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));

    // Store the message per connection
    (*ctx->per_connection_messages)[connection].push_back(*received_message);
    return 0;
}

int close_on_message_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    log_info("ct_callback_t: close_on_message_received.\n");
    auto* ctx = static_cast<CallbackContext*>(message_context->user_receive_context);

    (*ctx->per_connection_messages)[connection].push_back(*received_message);

    ct_connection_close(connection);
    return 0;
}

int close_on_expected_num_messages_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    log_info("ct_callback_t: close_on_expected_num_messages_received.\n");
    auto* ctx = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));

    (*ctx->per_connection_messages)[connection].push_back(*received_message);

    // Count total messages across all connections
    size_t total_messages = 0;
    for (const auto& pair : *ctx->per_connection_messages) {
        total_messages += pair.second.size();
    }

    if (total_messages >= ctx->total_expected_messages) {
        log_info("Received all expected messages, closing connection.");
        ct_connection_close(connection);
    }
    return 0;
}

// Callback that stores received message and sends "pong" response (for listener tests)
int respond_on_message_received_inline(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    log_info("ct_callback_t: respond_on_message_received.\n");
    auto* ctx = static_cast<CallbackContext*>(message_context->user_receive_context);
    (*ctx->per_connection_messages)[connection].push_back(*received_message);

    ct_message_t message;
    ct_message_build_with_content(&message, "pong", strlen("pong") + 1);
    ct_send_message(connection, &message);
    ct_message_free_content(&message);
    return 0;
}

int receive_message_and_respond_on_connection_received(ct_listener_t* listener, ct_connection_t* new_connection) {
    printf("ct_callback_t: receive_message_on_connection_received.\n");
    auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_listener_context);
    context->server_connections.push_back(new_connection);

    ct_receive_callbacks_t receive_message_request = {
      .receive_callback = respond_on_message_received_inline,
      .user_receive_context = ct_connection_get_callback_context(new_connection),
    };

    ct_receive_message(new_connection, receive_message_request);
    return 0;
}

int receive_message_respond_and_close_listener_on_connection_received(ct_listener_t* listener, ct_connection_t* new_connection) {
    log_trace("ct_connection_t received callback from listener");
    auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_listener_context);
    context->server_connections.push_back(new_connection);

    ct_receive_callbacks_t receive_message_request = {
      .receive_callback = respond_on_message_received_inline,
      .user_receive_context = listener->listener_callbacks.user_listener_context,
    };

    ct_listener_close(listener);

    log_trace("Adding receive callback from ct_listener_t");
    ct_receive_message(new_connection, receive_message_request);
    return 0;
}

int send_message_and_receive(struct ct_connection_s* connection) {
    log_trace("ct_callback_t: Ready - send_message_and_receive");
    auto* context = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    context->client_connections.push_back(connection);

    ct_message_t message;
    ct_message_build_with_content(&message, "ping", strlen("ping") + 1);
    ct_send_message(connection, &message);
    ct_message_free_content(&message);

    ct_receive_callbacks_t receive_message_request = {
      .receive_callback = close_on_message_received,
      .user_receive_context = ct_connection_get_callback_context(connection),
    };

    log_trace("Adding receive callback from ct_connection_t");
    ct_receive_message(connection, receive_message_request);
    return 0;
}

// Complex callback for multi-message listener tests
int on_message_receive_send_new_message_and_receive_inline(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    printf("ct_callback_t: on_message_receive_send_new_message_and_receive.\n");
    auto* ctx = static_cast<CallbackContext*>(message_context->user_receive_context);

    ct_connection_t* sending_connection = ctx->client_connections.at(0);

    ct_message_t message;
    ct_message_build_with_content(&message, "ping2", strlen("ping2") + 1);
    ct_send_message(sending_connection, &message);
    ct_message_free_content(&message);

    (*ctx->per_connection_messages)[connection].push_back(*received_message);

    ct_receive_callbacks_t receive_message_request = {
      .receive_callback = on_message_received,
      .user_receive_context = ct_connection_get_callback_context(connection),
    };

    ct_receive_message(connection, receive_message_request);
    return 0;
}

int on_connection_received_receive_message_close_listener_and_send_new_message(ct_listener_t* listener, ct_connection_t* new_connection) {
    printf("ct_callback_t: on_connection_received_receive_message_close_listener_and_send_new_message\n");
    auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_listener_context);
    ct_listener_close(listener);
    context->server_connections.push_back(new_connection);

    ct_receive_callbacks_t receive_message_request = {
      .receive_callback = on_message_receive_send_new_message_and_receive_inline,
      .user_receive_context = ct_connection_get_callback_context(new_connection),
    };

    ct_receive_message(new_connection, receive_message_request);
    return 0;
}

// --- Common callbacks for simple success/error testing ---

static int on_establishment_error(struct ct_connection_s* connection) {
    log_error("ct_connection_t error occurred");
    if (connection == nullptr) {
        log_error("No successful connection could be created on establishment error");
        return 0;
    }
    auto* context = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    context->connection_succeeded = false;
    return 0;
}

static int mark_connection_as_success_and_close(ct_connection_t* connection) {
    log_info("ct_connection_t is ready");
    auto* context = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    context->connection_succeeded = true;
    ct_connection_close(connection);
    return 0;
}

static int send_bytes_on_ready(struct ct_connection_s* connection) {
    log_info("ct_connection_t is ready, sending arbitrary bytes");
    auto* ctx = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    ctx->client_connections.push_back(connection);

    ct_message_t message;
    char bytes_to_send[] = {0, 1, 2, 3, 4, 5};
    ct_message_build_with_content(&message, bytes_to_send, sizeof(bytes_to_send));

    int rc = ct_send_message(connection, &message);
    EXPECT_EQ(rc, 0);
    ct_message_free_content(&message);

    ct_receive_message(connection, {
      .receive_callback = close_on_message_received,
      .user_receive_context = ct_connection_get_callback_context(connection),
    });

    return 0;
}

int send_two_messages_on_ready(struct ct_connection_s* connection) {
    log_info("ct_connection_t is ready, sending two messages");

    auto* ctx = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    ctx->client_connections.push_back(connection);

    ct_message_t message1;
    char* hello1 = "hello 1";
    ct_message_build_with_content(&message1, hello1, strlen(hello1) + 1);
    int rc = ct_send_message(connection, &message1);
    EXPECT_EQ(rc, 0);
    ct_message_free_content(&message1);

    ct_message_t message2;
    char* hello2 = "hello 2";
    ct_message_build_with_content(&message2, hello2, strlen(hello2) + 1);
    rc = ct_send_message(connection, &message2);
    EXPECT_EQ(rc, 0);
    ct_message_free_content(&message2);

    ct_receive_message(connection, {
      .receive_callback = close_on_expected_num_messages_received,
      .user_receive_context = ct_connection_get_callback_context(connection),
    });

    ct_receive_message(connection, {
      .receive_callback = close_on_expected_num_messages_received,
      .user_receive_context = ct_connection_get_callback_context(connection),
    });

    return 0;
}

// --- Callbacks for server-initiated stream tests ---

// Callback for server to send first message when connection is received and wait for response
int server_sends_first_and_waits_for_response(ct_listener_t* listener, ct_connection_t* new_connection) {
    log_info("Server: Connection received, sending first and waiting for response");
    
    auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_listener_context);
    context->server_connections.push_back(new_connection);

    // Server sends first message
    ct_message_t message;
    ct_message_build_with_content(&message, "server-hello", strlen("server-hello") + 1);
    int rc = ct_send_message(new_connection, &message);
    ct_message_free_content(&message);
    EXPECT_EQ(rc, 0);
    if (rc != 0) {
        log_error("Server failed to send initial message: %d", rc);
        ct_connection_close(new_connection);
        ct_listener_close(listener);
        return rc;
        // Close all connections from context
    }

    // Set up receive for client's response
    ct_receive_callbacks_t receive_req = {
        .receive_callback = close_on_message_received,
        .user_receive_context = listener->listener_callbacks.user_listener_context
    };

    ct_receive_message(new_connection, receive_req);
    ct_listener_close(listener);
    return 0;
}

// Callback for client to wait for server's message and respond
int client_waits_and_responds(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    log_info("Client: Received server-initiated message");
    auto* ctx = static_cast<CallbackContext*>(message_context->user_receive_context);
    (*ctx->per_connection_messages)[connection].push_back(*received_message);

    // Client responds to server's message
    ct_message_t response;
    ct_message_build_with_content(&response, "client-ack", strlen("client-ack") + 1);
    ct_send_message(connection, &response);
    ct_message_free_content(&response);

    return 0;
}

// Client ready callback - just set up receive callback, don't send
int client_ready_wait_for_server(ct_connection_t* connection) {
    log_info("Client: Connection ready, waiting for server to initiate stream");
    auto* ctx = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    ctx->client_connections.push_back(connection);

    ct_receive_callbacks_t receive_req = {
        .receive_callback = client_waits_and_responds,
        .user_receive_context = ct_connection_get_callback_context(connection)
    };

    ct_receive_message(connection, receive_req);
    return 0;
}

// --- Callbacks for connection abort tests ---

// Callback: Abort connection immediately when ready
int abort_on_ready(ct_connection_t* connection) {
    log_info("Connection ready, aborting immediately");
    EXPECT_EQ(ct_connection_is_established(connection), true);
    auto* ctx = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    ctx->client_connections.push_back(connection);

    // Abort the connection
    ct_connection_abort(connection);
    return 0;
}

// Callback: Clone connection, then abort the clone
int clone_and_abort_on_ready(ct_connection_t* connection) {
    auto* ctx = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    ctx->client_connections.push_back(connection);

    uint64_t num_active = ct_connection_get_num_grouped_connections(connection);
    log_info("clone_and_abort_on_ready, num_active=%llu", (unsigned long long)num_active);

    // Send message just to make sure stream is initialized
    ct_message_t message;
    ct_message_build_with_content(&message, "hello", strlen("hello") + 1);
    ct_send_message(connection, &message);
    ct_message_free_content(&message);

    if (num_active == 1) {
        // Original connection - clone it
        log_info("Original connection ready (num_active=%llu), cloning", (unsigned long long)num_active);


        int rc = ct_connection_clone(connection);
        if (rc < 0) {
            log_error("Failed to clone connection: %d", rc);
            ct_connection_abort(connection);
            return rc;
        }
        log_info("Successfully cloned connection");
    } else {
        // Cloned connection - abort this one (simulates multi-stream abort)
        log_info("Cloned connection ready (num_active=%llu), aborting clone", (unsigned long long)num_active);
        for (ct_connection_t* conn : ctx->client_connections) {
            log_info("Client connection in context: %p", (void*)conn);
            ct_connection_abort(conn);
        }
    }

    return 0;
}

// --- Callbacks for connection cloning tests ---

// Client ready callback: Clone connection, send on both, set up receives
int clone_send_and_setup_receive_on_both(ct_connection_t* connection) {
    auto* ctx = static_cast<CallbackContext*>(ct_connection_get_callback_context(connection));
    ctx->client_connections.push_back(connection);

    // Check connection group size to determine if this is original or cloned connection
    uint64_t num_active = ct_connection_get_num_grouped_connections(connection);

    const char* message_content;
    if (num_active == 1) {
        // This is the original connection - clone it
        log_info("Original connection %p ready, cloning", (void*)connection);

        int rc = ct_connection_clone(connection);
        if (rc < 0) {
            log_error("Failed to clone connection: %d", rc);
            ct_connection_close(connection);
            return rc;
        }

        log_info("Successfully cloned: original=%s", ct_connection_get_uuid(connection));
        message_content = "ping-original";
    } else {
        // This is the cloned connection
        log_info("Cloned connection %p ready", (void*)connection);
        message_content = "ping-cloned";
    }

    // Send and set up receive (same for both original and clone)
    ct_message_t message;
    ct_message_build_with_content(&message, message_content, strlen(message_content) + 1);
    ct_send_message(connection, &message);
    ct_message_free_content(&message);

    ct_receive_callbacks_t receive_req = {
        .receive_callback = close_on_message_received,
        .user_receive_context = ctx
    };
    ct_receive_message(connection, receive_req);

    log_info("Sent message '%s' and set up receive on connection %p", message_content, (void*)connection);
    return 0;
}

// Server callback: Receives message, responds with "Response: " prefix
int server_receive_and_respond_with_prefix(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    log_info("Server: Received message from connection %p: %s", (void*)connection, (*received_message)->content);
    auto* ctx = static_cast<CallbackContext*>(message_context->user_receive_context);

    // Store the received message per connection
    (*ctx->per_connection_messages)[connection].push_back(*received_message);

    // Send response with "Response: " prefix
    std::string response = std::string("Response: ") + std::string((char*)(*received_message)->content);
    ct_message_t response_msg;
    ct_message_build_with_content(&response_msg, response.c_str(), response.length() + 1);
    ct_send_message(connection, &response_msg);
    ct_message_free_content(&response_msg);

    log_info("Server: Sent response: %s", response.c_str());

    // Close listener after receiving both messages (original + clone)
    // Count total messages across all server connections
    size_t total_server_messages = 0;
    for (const auto& pair : *ctx->per_connection_messages) {
        total_server_messages += pair.second.size();
    }
    if (total_server_messages >= 2 && ctx->listener) {
        log_info("Server: Received all expected messages, closing listener");
        ct_listener_close(ctx->listener);
        ctx->listener = nullptr;
    }

    // Set up to receive next message (in case there are more)
    ct_receive_callbacks_t receive_req = {
        .receive_callback = server_receive_and_respond_with_prefix,
        .user_receive_context = ctx
    };
    ct_receive_message(connection, receive_req);

    return 0;
}

// Server listener callback for cloning: Set up receive on new connection
int server_on_connection_received_for_cloning(ct_listener_t* listener, ct_connection_t* new_connection) {
    log_info("Server: New connection received %p", (void*)new_connection);
    auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_listener_context);
    context->server_connections.push_back(new_connection);

    // Set up receive callback
    ct_receive_callbacks_t receive_req = {
        .receive_callback = server_receive_and_respond_with_prefix,
        .user_receive_context = listener->listener_callbacks.user_listener_context
    };

    ct_receive_message(new_connection, receive_req);
    return 0;
}

