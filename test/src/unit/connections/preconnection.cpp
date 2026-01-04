#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
}


TEST(PreconnectionUnitTests, SetsPreconnectionAsExpected) {
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, 5005);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

    // Allocated with ct_transport_properties_new()

    ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
    ASSERT_NE(preconnection, nullptr);

    EXPECT_EQ(0, preconnection->num_local_endpoints);
    EXPECT_EQ(1, preconnection->num_remote_endpoints);
    EXPECT_EQ(AF_INET, preconnection->remote_endpoints[0].data.resolved_address.ss_family);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection->remote_endpoints[0].data.resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));
    EXPECT_EQ(memcmp(preconnection->remote_endpoints, remote_endpoint, sizeof(ct_remote_endpoint_t)), 0);
    EXPECT_EQ(memcmp(&preconnection->transport_properties, transport_properties, sizeof(ct_transport_properties_t)), 0);

    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}

TEST(PreconnectionUnitTests, TakesDeepCopyOfRemoteEndpoint) {
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, 5005);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

    // Allocated with ct_transport_properties_new()

    ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
    ASSERT_NE(preconnection, nullptr);

    memset(remote_endpoint, 0, sizeof(ct_remote_endpoint_t));
    ASSERT_EQ(0, remote_endpoint->port);

    sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection->remote_endpoints[0].data.resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));

    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}

TEST(PreconnectionUnitTests, TakesDeepCopyOfRemoteEndpointWhenBuildingWithLocal) {
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, 5005);

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
    ASSERT_NE(local_endpoint, nullptr);

    ct_local_endpoint_with_port(local_endpoint, 6006);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

    // Allocated with ct_transport_properties_new()

    ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
    ASSERT_NE(preconnection, nullptr);
    ct_preconnection_set_local_endpoint(preconnection, local_endpoint);

    memset(remote_endpoint, 0, sizeof(ct_remote_endpoint_t));
    ASSERT_EQ(0, remote_endpoint->port);

    sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection->remote_endpoints[0].data.resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));

    ct_local_endpoint_free(local_endpoint);
    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}
