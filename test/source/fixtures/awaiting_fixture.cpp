#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "state/ctaps_state.h"
#include <logging/log.h>
}

#include <mutex>
#include <condition_variable>
#include <chrono>

class CallbackAwaiter {
public:
    // Wakes up the waiting thread
    void signal() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signal_count_++;
            printf("Signaling, new signal count is: %d\n", signal_count_);
        }
        cond_.notify_one();
    }

    // Waits until signal() has been called the expected number of times
    void await(size_t expected_count, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        printf("Awaiting for %zu signals...\n", expected_count);
        std::unique_lock<std::mutex> lock(mutex_);
        bool const success = cond_.wait_for(lock, timeout, [this, expected_count] {
            return signal_count_ >= expected_count;
        });

        // --- THE FIX ---
        // Use an assertion to fail the test immediately if the wait timed out.
        ASSERT_TRUE(success) << "Test timed out after " << timeout.count()
                             << "ms waiting for " << expected_count
                             << " signals, but only received " << signal_count_ << ".";
    }

    size_t get_signal_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return signal_count_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cond_;
    size_t signal_count_ = 0;
};


struct CallbackContext {
    CallbackAwaiter* awaiter;
    std::vector<ct_message_t*>* messages;
    std::vector<ct_connection_t*> server_connections; // created by the listener callback
    std::vector<ct_connection_t*> client_connections;
    std::function<void(CallbackContext*)>* closing_function; // To close any connections/listeners etc.
    size_t total_expected_signals;
    size_t total_expected_messages;
    ct_listener_t* listener;
};

class CTapsGenericFixture : public ::testing::Test {
protected:
    // Each test gets its own awaiter and message vector
    CallbackAwaiter awaiter;
    std::vector<ct_message_t*> received_messages;
    std::vector<ct_connection_t*> received_connections;
    std::vector<ct_connection_t*> client_connections;

    void SetUp() override {
        int rc = ct_initialize(TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");
        ASSERT_EQ(rc, 0);
    }

    void TearDown() override {
        // Clean up any messages the test didn't
        for (ct_message_t* msg : received_messages) {
            ct_message_free_all(msg);
        }
    }

    // --- C-style callbacks that bridge to our C++ object ---

    static int on_connection_ready(ct_connection_t* connection, void* user_data) {
        printf("ct_callback_t: ct_connection_t is ready.\n");
        auto* context = static_cast<CallbackContext*>(user_data);
        context->awaiter->signal();
        return 0;
    }

    static int send_message_and_close_on_connection_ready(ct_connection_t* connection, void* user_data) {
        log_info("ct_callback_t: ct_connection_t is ready.");
        ct_message_t message;
        ct_message_build_with_content(&message, "ping", strlen("ping") + 1);
        ct_send_message(connection, &message);
        ct_message_free_content(&message);

        ct_connection_close(connection);
        return 0;
    }

    static int send_message_on_connection_ready(ct_connection_t* connection, void* user_data) {
        printf("ct_callback_t: ct_connection_t is ready, sending message.\n");
        auto* context = static_cast<CallbackContext*>(user_data);

        // Send the message now that the client is ready
        ct_message_t message;
        ct_message_build_with_content(&message, "ping", strlen("ping") + 1);
        ct_send_message(connection, &message);
        ct_message_free_content(&message);

        context->awaiter->signal();
        return 0;
    }

    static int send_message_and_wait_for_response_on_connection_ready(ct_connection_t* connection, void* user_data) {
        printf("ct_callback_t: ct_connection_t is ready, sending message.\n");
        auto* context = static_cast<CallbackContext*>(user_data);

        // Send the message now that the client is ready
        ct_message_t message;
        ct_message_build_with_content(&message, "ping", strlen("ping") + 1);
        ct_send_message(connection, &message);
        ct_message_free_content(&message);

        ct_receive_message(connection, {
            .receive_callback = on_message_received,
            .user_data = user_data,
        });
        context->awaiter->signal();
        return 0;
    }

    static int on_connection_received(ct_listener_t* listener, ct_connection_t* new_connection) {
        printf("ct_callback_t: New connection received.\n");
        auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_data);
        context->server_connections.push_back(new_connection);
        context->awaiter->signal();
        return 0;
    }

    static int on_message_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context, void* user_data) {
        printf("ct_callback_t: on_message_received.\n");
        auto* ctx = static_cast<CallbackContext*>(user_data);

        // Store the message and signal the awaiter
        ctx->messages->push_back(*received_message);
        ctx->awaiter->signal();

        printf("Signal count is now %zu / %zu\n", ctx->awaiter->get_signal_count(), ctx->total_expected_signals);
        if (ctx->awaiter->get_signal_count() >= ctx->total_expected_signals) {
            printf("ct_callback_t: Final message received, closing connection.\n");
            // This will cause ct_start_event_loop() to unblock and return.
            if (ctx->closing_function) {
                (*ctx->closing_function)(ctx);
            }
        }
        return 0;
    }


    static int respond_on_message_received2(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context, void* user_data) {
        log_info("ct_callback_t: respond_on_message_received2.\n");

        log_trace("Received message with content: %s", (*received_message)->content);
        CallbackContext* ctx = static_cast<CallbackContext*>(user_data);
        ctx->messages->push_back(*received_message);

        log_info("Sending pong response from respond_on_message_received2.");
        ct_message_t message;
        ct_message_build_with_content(&message, "pong", strlen("pong") + 1);
        ct_send_message(connection, &message);

        ct_message_free_content(&message);

        // Don't close the server connection - let the client close after receiving response
        // This ensures queued QUIC data is transmitted before close

        return 0;
    }

    static int close_on_message_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context, void* user_data) {
        log_info("ct_callback_t: close_on_message_received.\n");
        log_trace("Received message with content: %s", (*received_message)->content);
        auto* ctx = static_cast<CallbackContext*>(user_data);

        ctx->messages->push_back(*received_message);

        ct_connection_close(connection);
        return 0;
    }

    static int respond_on_message_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context, void* user_data) {
        printf("ct_callback_t: respond_on_message_received.\n");
        auto* ctx = static_cast<CallbackContext*>(user_data);

        // Store the message and signal the awaiter
        ctx->messages->push_back(*received_message);
        ctx->awaiter->signal();

        ct_message_t message;
        ct_message_build_with_content(&message, "pong", strlen("pong") + 1);
        int send_rc = ct_send_message(connection, &message);
        printf("Send rc is: %d\n", send_rc);
        ct_message_free_content(&message);

        return 0;
    }


    static int on_message_receive_send_new_message_and_receive(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context, void* user_data) {
        printf("ct_callback_t: on_message_receive_send_new_message_and_receive.\n");
        auto* ctx = static_cast<CallbackContext*>(user_data);

        // this is the received connection -> that is wrong
        ct_connection_t* sending_connection = ctx->client_connections.at(0);

        ct_message_t message;
        ct_message_build_with_content(&message, "ping2", strlen("ping2") + 1);
        ct_send_message(sending_connection, &message);
        ct_message_free_content(&message);

        // Store the message and signal the awaiter
        ctx->messages->push_back(*received_message);
        ctx->awaiter->signal();

        ct_receive_callbacks_t receive_message_request = {
          .receive_callback = on_message_received,
          .user_data = user_data,
        };

        ct_receive_message(connection, receive_message_request);

        return 0;
    }

    static int receive_message_and_respond_on_connection_received(ct_listener_t* listener, ct_connection_t* new_connection) {
        printf("ct_callback_t: receive_message_on_connection_received.\n");
        auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_data);
        context->server_connections.push_back(new_connection);
        context->awaiter->signal();

        ct_receive_callbacks_t receive_message_request = {
          .receive_callback = respond_on_message_received,
          .user_data = listener->listener_callbacks.user_data,
        };

        ct_receive_message(new_connection, receive_message_request);
        return 0;
    }

    static int receive_message_respond_and_close_listener_on_connection_received(ct_listener_t* listener, ct_connection_t* new_connection, void* user_data) {
        log_trace("ct_connection_t received callback from listener");
        ct_receive_callbacks_t receive_message_request = {
          .receive_callback = respond_on_message_received2,
          .user_data = listener->listener_callbacks.user_data,
        };

        ct_listener_close(listener);

        log_trace("Adding receive callback from ct_listener_t");
        ct_receive_message(new_connection, receive_message_request);
        return 0;
    }

    static int send_message_and_receive(struct ct_connection_t* connection, void* udata) {
        log_trace("ct_callback_t: Ready - send_message_and_receive");
        ct_message_t message;
        ct_message_build_with_content(&message, "ping", strlen("ping") + 1);
        ct_send_message(connection, &message);
        ct_message_free_content(&message);

        ct_receive_callbacks_t receive_message_request = {
          .receive_callback = close_on_message_received,
          .user_data = udata,
        };

        log_trace("Adding receive callback from ct_connection_t");
        ct_receive_message(connection, receive_message_request);
        return 0;
    }

    static int on_connection_received_receive_message_close_listener_and_send_new_message(ct_listener_t* listener, ct_connection_t* new_connection) {
        printf("ct_callback_t: on_connection_received_receive_message_close_listener_and_send_new_message\n");
        auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_data);
        ct_listener_close(listener);
        context->server_connections.push_back(new_connection);
        context->awaiter->signal();

        ct_receive_callbacks_t receive_message_request = {
          .receive_callback = on_message_receive_send_new_message_and_receive,
          .user_data = listener->listener_callbacks.user_data,
        };

        ct_receive_message(new_connection, receive_message_request);
        return 0;
    }

};
