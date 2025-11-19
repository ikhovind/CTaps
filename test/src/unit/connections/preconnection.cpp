#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "util/util.h"
#include "ctaps.h"
}


TEST(PreconnectionUnitTests, SetsPreconnectionAsExpected) {
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);

    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, 5005);

    ct_transport_properties_t transport_properties;

    ct_transport_properties_build(&transport_properties);

    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_preconnection_t preconnection;
    ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

    EXPECT_EQ(0, preconnection.num_local_endpoints);
    EXPECT_EQ(1, preconnection.num_remote_endpoints);
    EXPECT_EQ(AF_INET, preconnection.remote_endpoints[0].data.resolved_address.ss_family);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection.remote_endpoints[0].data.resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));
    EXPECT_EQ(memcmp(preconnection.remote_endpoints, &remote_endpoint, sizeof(ct_remote_endpoint_t)), 0);
    EXPECT_EQ(memcmp(&preconnection.transport_properties, &transport_properties, sizeof(ct_transport_properties_t)), 0);
}

TEST(PreconnectionUnitTests, TakesDeepCopyOfRemoteEndpoint) {
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);

    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, 5005);

    ct_transport_properties_t transport_properties;

    ct_transport_properties_build(&transport_properties);

    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_preconnection_t preconnection;
    ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

    memset(&remote_endpoint, 0, sizeof(ct_remote_endpoint_t));
    ASSERT_EQ(0, remote_endpoint.port);

    sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection.remote_endpoints[0].data.resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));
}

TEST(PreconnectionUnitTests, TakesDeepCopyOfRemoteEndpointWhenBuildingWithLocal) {
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);

    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, 5005);

    ct_local_endpoint_t local_endpoint;

    ct_local_endpoint_build(&local_endpoint);
    ct_local_endpoint_with_port(&local_endpoint, 6006);

    ct_transport_properties_t transport_properties;

    ct_transport_properties_build(&transport_properties);

    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_preconnection_t preconnection;
    ct_preconnection_build_with_local(&preconnection, transport_properties, &remote_endpoint, 1, NULL, local_endpoint);

    memset(&remote_endpoint, 0, sizeof(ct_remote_endpoint_t));
    ASSERT_EQ(0, remote_endpoint.port);

    sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection.remote_endpoints[0].data.resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));
}