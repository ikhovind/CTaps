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


TEST(SimpleUdpTests, sendsSingleUdpPacket) {
    ctaps_initialize();
    printf("Sending UDP packet...\n");

    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_family(&remote_endpoint, AF_INET);
    remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, remote_endpoint);

    Connection connection;

    pthread_mutex_t waiting_mutex;
    pthread_cond_t waiting_cond;
    int num_reads = 0;
    pthread_mutex_init(&waiting_mutex, NULL);
    pthread_cond_init(&waiting_cond, NULL);

    CallBackWaiter cb_waiter = (CallBackWaiter) {
        .waiting_mutex = &waiting_mutex,
        .waiting_cond = &waiting_cond,
        .num_reads = &num_reads,
        .expected_num_reads = 0,
    };

    InitDoneCb init_done_cb = {
        .init_done_callback = connection_ready_cb,
        .user_data = (void*)&cb_waiter
    };

    preconnection_initiate(&preconnection, &connection, init_done_cb);

    wait_for_callback(&cb_waiter);

    Message message;

    message_build_with_content(&message, "hello world");

    send_message(&connection, &message);

    message_free_content(&message);

    Message* output_message;

    cb_waiter.expected_num_reads = 1;
    *cb_waiter.num_reads = 0;

    MessageReceiver message_receiver = (MessageReceiver) {
        .message = &output_message,
        .cb_waiter = &cb_waiter
    };

    ReceiveMessageRequest receive_message_request = {
        .receive_cb = receive_message_cb,
        .user_data = &message_receiver
    };

    receive_message(&connection, receive_message_request);

    ctaps_start_event_loop();

    wait_for_callback(&cb_waiter);

    EXPECT_STREQ(output_message->content, "Pong: hello world");
    message_free_all(output_message);
}

TEST(SimpleUdpTests, packetsAreReadInOrder) {
    printf("receiving messages\n");
    ctaps_initialize();

    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_family(&remote_endpoint, AF_INET);
    remote_endpoint_with_hostname(&remote_endpoint, "127.0.0.1");
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, remote_endpoint);

    Connection connection;

    pthread_mutex_t waiting_mutex;
    pthread_cond_t waiting_cond;
    int num_reads = 0;
    pthread_mutex_init(&waiting_mutex, NULL);
    pthread_cond_init(&waiting_cond, NULL);

    CallBackWaiter cb_waiter = (CallBackWaiter) {
        .waiting_mutex = &waiting_mutex,
        .waiting_cond = &waiting_cond,
        .num_reads = &num_reads,
        .expected_num_reads = 1,
    };

    InitDoneCb init_done_cb = {
        .init_done_callback = connection_ready_cb,
        .user_data = (void*)&cb_waiter
    };

    preconnection_initiate(&preconnection, &connection, init_done_cb);

    Message message1;
    message_build_with_content(&message1, "hello 1");

    send_message(&connection, &message1);

    Message message2;
    message_build_with_content(&message2, "hello 2");
    send_message(&connection, &message2);

    Message* received_message1;

    pthread_mutex_t waiting_mutex1;
    pthread_cond_t waiting_cond1;
    num_reads = 0;
    pthread_mutex_init(&waiting_mutex1, NULL);
    pthread_cond_init(&waiting_cond1, NULL);

    CallBackWaiter cb_waiter1 = (CallBackWaiter) {
        .waiting_mutex = &waiting_mutex1,
        .waiting_cond = &waiting_cond1,
        .num_reads = &num_reads,
        .expected_num_reads = 2,
    };


    MessageReceiver message_receiver = (MessageReceiver) {
        .message = &received_message1,
        .cb_waiter = &cb_waiter1,
    };

    ReceiveMessageRequest receive_message_cb1 = {
        .receive_cb = receive_message_cb,
        .user_data = &message_receiver
    };

    Message* received_message2;

    MessageReceiver message_receiver2 = (MessageReceiver) {
        .message = &received_message2,
        .cb_waiter = &cb_waiter1,
    };

    ReceiveMessageRequest receive_message_cb2 = {
        .receive_cb = receive_message_cb,
        .user_data = &message_receiver2,
    };

    receive_message(&connection, receive_message_cb1);
    receive_message(&connection, receive_message_cb2);

    ctaps_start_event_loop();

    wait_for_callback(&cb_waiter1);

    EXPECT_STREQ(received_message1->content, "Pong: hello 1");
    EXPECT_STREQ(received_message2->content, "Pong: hello 2");
    message_free_all(received_message1);
    message_free_content(received_message2);
}
