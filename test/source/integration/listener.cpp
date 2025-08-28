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

int receive_message_cb2(Connection* connection, Message** received_message, void* user_data) {
    printf("Received message callback!\n");
    return 0;
}

int connection_receeived_cb(Connection* connection, void* user_data) {
    printf("Received connection callback!\n");


    ReceiveMessageRequest receive_message_request = {
        .receive_cb = receive_message_cb2,
        .user_data = NULL
    };

    receive_message(connection, receive_message_request);
}

TEST(SimpleUdpTests, canListenForUdp) {
    GTEST_SKIP();
    ctaps_initialize();
    printf("Sending UDP packet...\n");

    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    LocalEndpoint local_endpoint;

    local_endpoint_with_ipv4(&local_endpoint, inet_addr("127.0.0.1"));
    local_endpoint_with_port(&local_endpoint, 1234);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    selection_properties_set_selection_property(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build_with_local(&preconnection, transport_properties, &remote_endpoint, 1, local_endpoint);

    Listener listener;
    int res = preconnection_listen(&preconnection, &listener, connection_receeived_cb);
    printf("Res of listen: %d\n", res);
    ctaps_start_event_loop();
}