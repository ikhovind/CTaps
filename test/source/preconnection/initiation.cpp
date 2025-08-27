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

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, uv_udp_bind, uv_udp_t*, const struct sockaddr*, unsigned int)
FAKE_VALUE_FUNC(int, uv_udp_recv_start, uv_udp_t*, uv_alloc_cb, uv_udp_recv_cb)

TEST(InitiationTests, respectsLocalEndpoint) {
    uv_udp_bind_fake.return_val = 0;
    uv_udp_recv_start_fake.return_val = 0;

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
    preconnection_build_with_local(&preconnection, transport_properties, &remote_endpoint, 1,  local_endpoint);

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

    // Check that the given local port is actually the one used
    // cast address to ipvf
    sockaddr_in* addr = (sockaddr_in*)&connection.local_endpoint.data.address;
    EXPECT_EQ(1234, htons(addr->sin_port));
    EXPECT_EQ(1234, connection.local_endpoint.port);
    EXPECT_EQ(LOCAL_ENDPOINT_TYPE_ADDRESS, connection.local_endpoint.type);
    EXPECT_EQ(1, uv_udp_bind_fake.call_count);
    EXPECT_EQ(1234, htons(((struct sockaddr_in*)uv_udp_bind_fake.arg1_val)->sin_port));
}