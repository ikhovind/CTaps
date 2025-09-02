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
struct MessageReceiverCtx {
    CallbackAwaiter* awaiter;
    std::vector<Message*>* messages;
    Connection* connection_to_close; // Handle to the connection
    size_t total_expected_signals;
};

class SimpleUdpTests : public ::testing::Test {
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
        auto* awaiter = static_cast<CallbackAwaiter*>(user_data);
        awaiter->signal();
        return 0;
    }

    static int on_message_received(Connection* connection, Message** received_message, void* user_data) {
        printf("Callback: Message received.\n");
        auto* ctx = static_cast<MessageReceiverCtx*>(user_data);

        // Store the message and signal the awaiter
        ctx->messages->push_back(*received_message);
        ctx->awaiter->signal();

        printf("The number of signals is now: %zu\n", ctx->awaiter->get_signal_count());

        if (ctx->awaiter->get_signal_count() >= ctx->total_expected_signals) {
            printf("Callback: Final message received, closing connection.\n");
            // This will cause ctaps_start_event_loop() to unblock and return.
            connection_close(ctx->connection_to_close);
        }

        return 0;
    }
};


TEST_F(SimpleUdpTests, sendsSingleUdpPacket) {
    // --- Setup ---
    RemoteEndpoint remote_endpoint;
    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);
    Connection connection;

    InitDoneCb init_done_cb = {
        .init_done_callback = on_connection_ready,
        .user_data = &awaiter
    };

    preconnection_initiate(&preconnection, &connection, init_done_cb, nullptr);

    // "Await" the connection to be ready (1 signal expected)
    awaiter.await(1);
    ASSERT_EQ(awaiter.get_signal_count(), 1);

    // --- Action ---
    Message message;
    message_build_with_content(&message, "hello world", strlen("hello world") + 1);
    send_message(&connection, &message);
    message_free_content(&message);

    MessageReceiverCtx receiver_ctx = {
        .awaiter = &awaiter,
        .messages = &received_messages,
        .connection_to_close = &connection,
        .total_expected_signals = 2
    };

    ReceiveMessageRequest receive_req = { .receive_cb = on_message_received, .user_data = &receiver_ctx };
    receive_message(&connection, receive_req);

    ctaps_start_event_loop();

    // "Await" the receive callback (total signals now expected: 2)
    awaiter.await(2);
    ASSERT_EQ(awaiter.get_signal_count(), 2);

    // --- Assertions ---
    ASSERT_EQ(received_messages.size(), 1);
    EXPECT_STREQ(received_messages[0]->content, "Pong: hello world");
}


TEST_F(SimpleUdpTests, packetsAreReadInOrder) {
    const size_t TOTAL_EXPECTED_SIGNALS = 3; // 1 ready + 2 receives

    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);

    Connection connection;

    InitDoneCb init_done_cb = { .init_done_callback = on_connection_ready, .user_data = &awaiter };
    preconnection_initiate(&preconnection, &connection, init_done_cb, nullptr);
    awaiter.await(1);

    // --- Action ---
    // ... build and send message1 and message2 ...
    char* hello1 = "hello 1";
    Message message1;
    message_build_with_content(&message1, hello1, strlen(hello1) + 1);
    send_message(&connection, &message1);

    char* hello2 = "hello 2";
    Message message2;
    message_build_with_content(&message2, hello2, strlen(hello2) + 1);
    send_message(&connection, &message2);


    MessageReceiverCtx receiver_ctx = {
        .awaiter = &awaiter,
        .messages = &received_messages,
        .connection_to_close = &connection,
        .total_expected_signals = TOTAL_EXPECTED_SIGNALS
    };

    ReceiveMessageRequest receive_req = { .receive_cb = on_message_received, .user_data = &receiver_ctx };

    // Post two receive requests
    receive_message(&connection, receive_req);
    receive_message(&connection, receive_req);

    // --- Run Event Loop ---
    ctaps_start_event_loop();

    // --- Assertions ---
    ASSERT_EQ(awaiter.get_signal_count(), TOTAL_EXPECTED_SIGNALS);
    ASSERT_EQ(received_messages.size(), 2);
    EXPECT_STREQ(received_messages[0]->content, "Pong: hello 1");
    EXPECT_STREQ(received_messages[1]->content, "Pong: hello 2");
}

TEST_F(SimpleUdpTests, canPingArbitraryBytes) {
    // Total signals: 1 for the connection being ready, 1 for the message being received.
    const size_t TOTAL_EXPECTED_SIGNALS = 2;

    // --- Setup ---


    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);

    Connection connection;

    InitDoneCb init_done_cb = {
        .init_done_callback = on_connection_ready,
        .user_data = &awaiter
    };
    preconnection_initiate(&preconnection, &connection, init_done_cb, nullptr);

    // "Await" the connection to be ready before we send anything.
    awaiter.await(1);
    ASSERT_EQ(awaiter.get_signal_count(), 1) << "Test timed out waiting for connection to be ready.";

    // --- Action ---
    Message message;
    char bytes_to_send[] = {0, 1, 2, 3, 4, 5};
    message_build_with_content(&message, bytes_to_send, sizeof(bytes_to_send));

    send_message(&connection, &message);
    message_free_content(&message);

    // Setup the context for the receive callback
    MessageReceiverCtx receiver_ctx = {
        .awaiter = &awaiter,
        .messages = &received_messages,
        .connection_to_close = &connection,
        .total_expected_signals = TOTAL_EXPECTED_SIGNALS
    };
    ReceiveMessageRequest receive_req = {
        .receive_cb = on_message_received,
        .user_data = &receiver_ctx
    };

    // Post the receive request
    receive_message(&connection, receive_req);

    // --- Run Event Loop ---
    // This blocks until the on_message_received callback closes the connection.
    ctaps_start_event_loop();

    // --- Assertions ---
    ASSERT_EQ(awaiter.get_signal_count(), TOTAL_EXPECTED_SIGNALS) << "Test timed out waiting for message.";
    ASSERT_EQ(received_messages.size(), 1);

    char expected_output[] = {'P', 'o', 'n', 'g', ':', ' ', 0, 1, 2, 3, 4, 5};
    ASSERT_EQ(received_messages[0]->length, sizeof(expected_output));
    EXPECT_EQ(memcmp(expected_output, received_messages[0]->content, sizeof(expected_output)), 0);
}