#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "connections/preconnection/preconnection.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
}

TEST(NetworkingTest, SendsUdpPacket) {

    ctaps_initialize();
    printf("Sending UDP packet...\n");
    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_endpoint(&remote_endpoint, "localhost");
    remote_endpoint_with_port(&remote_endpoint, 4001);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties);

    Connection connection;

    preconnection_initiate(&preconnection, &connection);

    Message message;

    message_build_with_content(&message, "hello world");

    send_message(&connection, &message);

    message_free(&message);

    Message recv;

    message_build_without_content(&recv);

    receive_message(&connection, &recv);

    connection_close(&connection);

    ASSERT_THAT(recv.content, testing::NotNull());
    EXPECT_STREQ("hello world", recv.content);
}