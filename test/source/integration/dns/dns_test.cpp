#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "util/util.h"
#include "connections/preconnection/preconnection.h"
#include "state/ctaps_state.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
}


TEST(RemoteEndpointUnitTests, CanDnsLookupHostName) {
    ct_initialize(NULL,NULL);
    printf("Sending UDP packet...\n");

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);

    ct_remote_endpoint_with_hostname(&remote_endpoint, "google.com");
    ct_remote_endpoint_with_port(&remote_endpoint, 1234);

    ct_transport_properties_t transport_properties;

    ct_transport_properties_build(&transport_properties);
    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_preconnection_t preconnection;
    ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

    ct_connection_t connection;

    pthread_mutex_t waiting_mutex;
    pthread_cond_t waiting_cond;
    int num_reads = 0;
    pthread_mutex_init(&waiting_mutex, NULL);
    pthread_cond_init(&waiting_cond, NULL);

    ct_call_back_waiter_t cb_waiter = (ct_call_back_waiter_t) {
        .waiting_mutex = &waiting_mutex,
        .waiting_cond = &waiting_cond,
        .num_reads = &num_reads,
        .expected_num_reads = 0,
    };

    ct_connection_callbacks_t connection_callbacks = {
        .ready = connection_ready_cb,
        .user_data = (void*)&cb_waiter
    };

    ct_preconnection_initiate(&preconnection, &connection, connection_callbacks);

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
