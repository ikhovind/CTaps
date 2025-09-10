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
    ctaps_initialize();
    printf("Sending UDP packet...\n");

    RemoteEndpoint remote_endpoint;

    remote_endpoint_build(&remote_endpoint);
    remote_endpoint_with_hostname(&remote_endpoint, "google.com");
    remote_endpoint_with_port(&remote_endpoint, 1234);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);
    tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);

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

    InitDoneCb init_done_cb = {
        .init_done_callback = connection_ready_cb,
        .user_data = (void*)&cb_waiter
    };

    preconnection_initiate(&preconnection, &connection, init_done_cb,NULL);

    wait_for_callback(&cb_waiter);

    printf("Connection port is: %d\n", connection.remote_endpoint.port);
    EXPECT_EQ(1234, connection.remote_endpoint.port);
}
