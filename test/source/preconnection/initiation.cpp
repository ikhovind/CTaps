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

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, uv_udp_bind, uv_udp_t*, const struct sockaddr*, unsigned int)
FAKE_VALUE_FUNC(int, uv_udp_recv_start, uv_udp_t*, uv_alloc_cb, uv_udp_recv_cb)

TEST(InitiationTests, respectsLocalEndpoint) {
    uv_udp_bind_fake.return_val = 0;
    uv_udp_recv_start_fake.return_val = 0;

    ct_initialize(NULL,NULL);
    printf("Sending UDP packet...\n");

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);

    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, 5005);

    ct_local_endpoint_t local_endpoint;
    ct_local_endpoint_build(&local_endpoint);

    ct_local_endpoint_with_port(&local_endpoint, 1234);

    ct_transport_properties_t transport_properties;

    ct_transport_properties_build(&transport_properties);

    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_preconnection_t preconnection;
    ct_preconnection_build_with_local(&preconnection, transport_properties, &remote_endpoint, 1, NULL, local_endpoint);

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

    // Check that the given local port is actually the one used
    // cast resolved_address to ipvf
    sockaddr_in* addr = (sockaddr_in*)&connection.local_endpoint.data.address;
    EXPECT_EQ(1234, htons(addr->sin_port));
    EXPECT_EQ(1234, connection.local_endpoint.port);
    EXPECT_EQ(1, uv_udp_bind_fake.call_count);
    EXPECT_EQ(1234, htons(((struct sockaddr_in*)uv_udp_bind_fake.arg1_val)->sin_port));
}