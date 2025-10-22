#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "util/util.h"
#include "ctaps.h"
#include "endpoints/local/local_endpoint.h"
#include "transport_properties/transport_properties.h"
}


TEST(PreconnectionUnitTests, SetsPreconnectionAsExpected) {
    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);

    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);

    EXPECT_EQ(0, preconnection.num_local_endpoints);
    EXPECT_EQ(1, preconnection.num_remote_endpoints);
    EXPECT_EQ(AF_INET, preconnection.remote_endpoints[0].data.resolved_address.ss_family);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection.remote_endpoints[0].data.resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));
    EXPECT_EQ(memcmp(preconnection.remote_endpoints, &remote_endpoint, sizeof(RemoteEndpoint)), 0);
    EXPECT_EQ(memcmp(&preconnection.transport_properties, &transport_properties, sizeof(TransportProperties)), 0);
}

TEST(PreconnectionUnitTests, TakesDeepCopyOfRemoteEndpoint) {
    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);

    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    Preconnection preconnection;
    preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);

    memset(&remote_endpoint, 0, sizeof(RemoteEndpoint));
    ASSERT_EQ(0, remote_endpoint.port);

    sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection.remote_endpoints[0].data.resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));
}

TEST(PreconnectionUnitTests, TakesDeepCopyOfRemoteEndpointWhenBuildingWithLocal) {
    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);

    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    LocalEndpoint local_endpoint;

    local_endpoint_build(&local_endpoint);
    local_endpoint_with_port(&local_endpoint, 6006);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);

    tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    Preconnection preconnection;
    preconnection_build_with_local(&preconnection, transport_properties, &remote_endpoint, 1, local_endpoint);

    memset(&remote_endpoint, 0, sizeof(RemoteEndpoint));
    ASSERT_EQ(0, remote_endpoint.port);

    sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection.remote_endpoints[0].data.resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));
}