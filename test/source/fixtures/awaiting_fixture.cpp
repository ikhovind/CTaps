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
    std::vector<Connection*> connections; // created by the listener callback
    std::function<void()>* closing_function; // To close any connections/listeners etc.
    size_t total_expected_signals;
};

class CTapsGenericFixture : public ::testing::Test {
protected:
    // Each test gets its own awaiter and message vector
    CallbackAwaiter awaiter;
    std::vector<Message*> received_messages;
    std::vector<Connection*> received_connections;

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

    static int on_connection_received(Listener* listener, Connection* new_connection) {
        printf("Callback: New connection received.\n");
        auto* context = static_cast<CallbackContext*>(listener->user_data);
        context->connections.push_back(new_connection);
        context->awaiter->signal();
        return 0;
    }

    static int on_message_received(Connection* connection, Message** received_message, void* user_data) {
        printf("Callback: Message received.\n");
        auto* ctx = static_cast<CallbackContext*>(user_data);

        // Store the message and signal the awaiter
        ctx->messages->push_back(*received_message);
        ctx->awaiter->signal();

        if (ctx->awaiter->get_signal_count() >= ctx->total_expected_signals) {
            printf("Callback: Final message received, closing connection.\n");
            // This will cause ctaps_start_event_loop() to unblock and return.
            if (ctx->closing_function) {
                (*ctx->closing_function)();
            }
        }
        return 0;
    }

    static int receive_message_on_connection_received(Listener* listener, Connection* new_connection) {
        printf("Callback: New connection received, trying to receive message.\n");
        auto* context = static_cast<CallbackContext*>(listener->user_data);
        context->connections.push_back(new_connection);
        context->awaiter->signal();

        ReceiveMessageRequest receive_message_request = {
          .receive_cb = on_message_received,
          .user_data = listener->user_data,
        };

        receive_message(new_connection, receive_message_request);
        return 0;
    }

};
