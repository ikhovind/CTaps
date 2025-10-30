#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "util/util.h"
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
}


TEST(RemoteEndpointUnitTests, CanDnsLookupHostName) {
    ctaps_initialize(NULL,NULL);
    printf("Sending UDP packet...\n");

    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);

    remote_endpoint_with_hostname(&remote_endpoint, "google.com");
    remote_endpoint_with_port(&remote_endpoint, 1234);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);
    tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);

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

    ConnectionCallbacks connection_callbacks = {
        .ready = connection_ready_cb,
        .user_data = (void*)&cb_waiter
    };

    preconnection_initiate(&preconnection, &connection, connection_callbacks);

    wait_for_callback(&cb_waiter);

    // check address port
    if (connection.remote_endpoint.data.resolved_address.ss_family == AF_INET) {
        struct sockaddr_in* addr = (struct sockaddr_in*)&connection.remote_endpoint.data.resolved_address;
        EXPECT_EQ(1234, ntohs(addr->sin_port));
    }
    else {
        struct sockaddr_in6* addr = (struct sockaddr_in6*)&connection.remote_endpoint.data.resolved_address;

        EXPECT_EQ(1234, ntohs(addr->sin6_port));
    }
    EXPECT_EQ(1234, connection.remote_endpoint.port);
}
