#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
}

TEST(NetworkingTest, SendsUdpPacket) {

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

    preconnection_initiate(&preconnection, &connection);

    Message message;

    message_build_with_content(&message, "hello world");

    send_message(&connection, &message);

    ctaps_start_event_loop();

    message_free_content(&message);

    Message* received_message = receive_message(&connection);

    connection_close(&connection);

    ASSERT_THAT(received_message->content, testing::NotNull());
    EXPECT_STREQ(received_message->content, "Pong: hello world");
    message_free_all(received_message);
}