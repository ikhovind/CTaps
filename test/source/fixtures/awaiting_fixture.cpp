#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
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
    std::vector<Message*>* messages;
    std::vector<Connection*> server_connections; // created by the listener callback
    std::vector<Connection*> client_connections;
    std::function<void(CallbackContext*)>* closing_function; // To close any connections/listeners etc.
    size_t total_expected_signals;
    size_t total_expected_messages;
    Listener* listener;
};

class CTapsGenericFixture : public ::testing::Test {
protected:
    // Each test gets its own awaiter and message vector
    CallbackAwaiter awaiter;
    std::vector<Message*> received_messages;
    std::vector<Connection*> received_connections;
    std::vector<Connection*> client_connections;

    void SetUp() override {
        ctaps_initialize();
    }

    void TearDown() override {
        // Clean up any messages the test didn't
        for (Message* msg : received_messages) {
            message_free_all(msg);
        }
    }

    // --- C-style callbacks that bridge to our C++ object ---

    static int on_connection_ready(Connection* connection, void* user_data) {
        printf("Callback: Connection is ready.\n");
        auto* context = static_cast<CallbackContext*>(user_data);
        context->awaiter->signal();
        return 0;
    }

    static int send_message_on_connection_ready(Connection* connection, void* user_data) {
        printf("Callback: Connection is ready, sending message.\n");
        auto* context = static_cast<CallbackContext*>(user_data);

        // Send the message now that the client is ready
        Message message;
        message_build_with_content(&message, "ping", strlen("ping") + 1);
        send_message(connection, &message);
        message_free_content(&message);

        context->awaiter->signal();
        return 0;
    }

    static int send_message_and_wait_for_response_on_connection_ready(Connection* connection, void* user_data) {
        printf("Callback: Connection is ready, sending message.\n");
        auto* context = static_cast<CallbackContext*>(user_data);

        // Send the message now that the client is ready
        Message message;
        message_build_with_content(&message, "ping", strlen("ping") + 1);
        send_message(connection, &message);
        message_free_content(&message);

        receive_message(connection, {
            .receive_callback = on_message_received,
            .user_data = user_data,
        });
        context->awaiter->signal();
        return 0;
    }

    static int on_connection_received(Listener* listener, Connection* new_connection) {
        printf("Callback: New connection received.\n");
        auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_data);
        context->server_connections.push_back(new_connection);
        context->awaiter->signal();
        return 0;
    }

    static int on_message_received(Connection* connection, Message** received_message, MessageContext* message_context, void* user_data) {
        printf("Callback: on_message_received.\n");
        auto* ctx = static_cast<CallbackContext*>(user_data);

        // Store the message and signal the awaiter
        ctx->messages->push_back(*received_message);
        ctx->awaiter->signal();

        printf("Signal count is now %zu / %zu\n", ctx->awaiter->get_signal_count(), ctx->total_expected_signals);
        if (ctx->awaiter->get_signal_count() >= ctx->total_expected_signals) {
            printf("Callback: Final message received, closing connection.\n");
            // This will cause ctaps_start_event_loop() to unblock and return.
            if (ctx->closing_function) {
                (*ctx->closing_function)(ctx);
            }
        }
        return 0;
    }

    static int respond_on_message_received(Connection* connection, Message** received_message, MessageContext* message_context, void* user_data) {
        printf("Callback: respond_on_message_received.\n");
        auto* ctx = static_cast<CallbackContext*>(user_data);

        // Store the message and signal the awaiter
        ctx->messages->push_back(*received_message);
        ctx->awaiter->signal();

        Message message;
        message_build_with_content(&message, "pong", strlen("pong") + 1);
        int send_rc = send_message(connection, &message);
        printf("Send rc is: %d\n", send_rc);
        message_free_content(&message);

        return 0;
    }


    static int on_message_receive_send_new_message_and_receive(Connection* connection, Message** received_message, MessageContext* message_context, void* user_data) {
        printf("Callback: on_message_receive_send_new_message_and_receive.\n");
        auto* ctx = static_cast<CallbackContext*>(user_data);

        // this is the received connection -> that is wrong
        Connection* sending_connection = ctx->client_connections.at(0);

        Message message;
        message_build_with_content(&message, "ping2", strlen("ping2") + 1);
        send_message(sending_connection, &message);
        message_free_content(&message);

        // Store the message and signal the awaiter
        ctx->messages->push_back(*received_message);
        ctx->awaiter->signal();

        ReceiveCallbacks receive_message_request = {
          .receive_callback = on_message_received,
          .user_data = user_data,
        };

        receive_message(connection, receive_message_request);

        return 0;
    }

    static int receive_message_and_respond_on_connection_received(Listener* listener, Connection* new_connection) {
        printf("Callback: receive_message_on_connection_received.\n");
        auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_data);
        context->server_connections.push_back(new_connection);
        context->awaiter->signal();

        ReceiveCallbacks receive_message_request = {
          .receive_callback = respond_on_message_received,
          .user_data = listener->listener_callbacks.user_data,
        };

        receive_message(new_connection, receive_message_request);
        return 0;
    }

    static int on_connection_received_receive_message_close_listener_and_send_new_message(Listener* listener, Connection* new_connection) {
        printf("Callback: on_connection_received_receive_message_close_listener_and_send_new_message\n");
        auto* context = static_cast<CallbackContext*>(listener->listener_callbacks.user_data);
        listener_close(listener);
        context->server_connections.push_back(new_connection);
        context->awaiter->signal();

        ReceiveCallbacks receive_message_request = {
          .receive_callback = on_message_receive_send_new_message_and_receive,
          .user_data = listener->listener_callbacks.user_data,
        };

        receive_message(new_connection, receive_message_request);
        return 0;
    }

};
