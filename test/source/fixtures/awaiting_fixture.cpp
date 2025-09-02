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
        }
        cond_.notify_one();
    }

    // Waits until signal() has been called the expected number of times
    void await(size_t expected_count, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait_for(lock, timeout, [this, expected_count] {
            return signal_count_ >= expected_count;
        });
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


// A struct to pass context to our receive callback
struct CallbackContext {
    CallbackAwaiter* awaiter;
    std::vector<Message*>* messages;
    std::function<void()>* closing_function; // To close any connections/listeners etc.
    size_t total_expected_signals;
};

class CTapsGenericFixture : public ::testing::Test {
protected:
    // Each test gets its own awaiter and message vector
    CallbackAwaiter awaiter;
    std::vector<Message*> received_messages;

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

    static int on_message_received(Connection* connection, Message** received_message, void* user_data) {
        printf("Callback: Message received.\n");
        auto* ctx = static_cast<CallbackContext*>(user_data);

        // Store the message and signal the awaiter
        ctx->messages->push_back(*received_message);
        ctx->awaiter->signal();

        printf("The number of signals is now: %zu\n", ctx->awaiter->get_signal_count());

        if (ctx->awaiter->get_signal_count() >= ctx->total_expected_signals) {
            printf("Callback: Final message received, closing connection.\n");
            // This will cause ctaps_start_event_loop() to unblock and return.
            if (ctx->closing_function) {
                (*ctx->closing_function)();
            }
        }

        return 0;
    }
};
