#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
}

pthread_mutex_t mutex;
pthread_cond_t cond;
bool test_state = false;
int num_receives = 0;


int receive_message_and_close_cb(Connection* connection, Message** received_message, void* user_data) {

    Message** output_message = (Message**) user_data;

    // want to extract actual pointer so we can later free it
    *output_message = *received_message;

    connection_close(connection);
    return 0;
}

int receive_message_cb(Connection* connection, Message** received_message, void* user_data) {

    Message** output_message = (Message**) user_data;

    // want to extract actual pointer so we can later free it
    *output_message = *received_message;

    if (++num_receives == 2) {
        connection_close(connection);
    }
    return 0;
}

int connection_ready_notify_cb(Connection* connection, void* user_data) {
    pthread_mutex_lock(&mutex);

    // Wake up the main test thread that is waiting on this condition.
    test_state = true;
    pthread_cond_signal(&cond);

    pthread_mutex_unlock(&mutex);
}

int connection_ready_cb(Connection* connection, void* user_data) {
    printf("Connection ready\n");
    Message message;

    message_build_with_content(&message, "hello world");

    send_message(connection, &message);

    message_free_content(&message);

    ReceiveMessageRequest receive_message_cb2 = {
        .receive_cb = receive_message_and_close_cb,
        .user_data = user_data
    };

    receive_message(connection, receive_message_cb2);
}


TEST(SimpleUdpTests, sendsSingleUdpPacket) {
    ctaps_initialize();
    printf("Sending UDP packet...\n");

    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_family(&remote_endpoint, AF_INET);
    remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");
    remote_endpoint_with_port(&remote_endpoint, 5005);

    LocalEndpoint local_endpoint;

    local_endpoint_with_port(&local_endpoint, 4005);
    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build_with_local(&preconnection, transport_properties, remote_endpoint, local_endpoint);

    Connection connection;

    Message* output_message = (Message*)malloc(sizeof(Message));
    InitDoneCb init_done_cb = {
        .init_done_callback = connection_ready_cb,
        .user_data = (void*)&output_message
    };

    preconnection_initiate(&preconnection, &connection, init_done_cb);

    ctaps_start_event_loop();

    ASSERT_THAT(output_message, testing::NotNull());
    EXPECT_STREQ(output_message->content, "Pong: hello world");
    message_free_all(output_message);
    ctaps_cleanup();
}

TEST(SimpleUdpTests, packetsAreReadInOrder) {
    ctaps_initialize();
    printf("Sending UDP packet...\n");

    RemoteEndpoint remote_endpoint;
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    remote_endpoint_with_family(&remote_endpoint, AF_INET);
    remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");
    remote_endpoint_with_port(&remote_endpoint, 5005);

    LocalEndpoint local_endpoint;

    local_endpoint_with_port(&local_endpoint, 4005);
    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build_with_local(&preconnection, transport_properties, remote_endpoint, local_endpoint);

    Connection connection;

    InitDoneCb init_done_cb = {
        .init_done_callback = connection_ready_notify_cb,
        .user_data = 0
    };

    preconnection_initiate(&preconnection, &connection, init_done_cb);

    pthread_mutex_lock(&mutex);
    while (!test_state) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);


    Message message1;
    message_build_with_content(&message1, "hello 1");

    send_message(&connection, &message1);

    Message message2;
    message_build_with_content(&message2, "hello 2");
    send_message(&connection, &message2);

    Message* received_message1;

    ReceiveMessageRequest receive_message_cb1 = {
        .receive_cb = receive_message_cb,
        .user_data = &received_message1
    };

    Message* received_message2;

    ReceiveMessageRequest receive_message_cb2 = {
        .receive_cb = receive_message_cb,
        .user_data = &received_message2,
    };

    receive_message(&connection, receive_message_cb1);
    receive_message(&connection, receive_message_cb2);

    ctaps_start_event_loop();

    printf("hello does this work\n");
    /*
    printf("message 1 content: %s\n", received_message1->content);
    printf("message 2 content: %s\n", received_message2->content);
    */

    EXPECT_STREQ(received_message1->content, "Pong: hello 1");
    EXPECT_STREQ(received_message2->content, "Pong: hello 2");
    //message_free_content(received_message1);
    //message_free_content(received_message2);
}
