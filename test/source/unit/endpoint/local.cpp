#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "util/util.h"
#include "ctaps.h"
#include "endpoints/local/local_endpoint.h"
}


TEST(LocalEndpointUnitTests, SetsIpv4FamilyAndAddress) {
    LocalEndpoint local_endpoint;

    local_endpoint_build(&local_endpoint);

    local_endpoint_with_port(&local_endpoint, 5005);
    local_endpoint_with_ipv4(&local_endpoint, inet_addr("127.0.0.1"));

    sockaddr_in* addr = (struct sockaddr_in*)&local_endpoint.data.address;

    EXPECT_EQ(LOCAL_ENDPOINT_TYPE_ADDRESS, local_endpoint.type);
    EXPECT_EQ(5005, ntohs(addr->sin_port));
    EXPECT_EQ(5005, local_endpoint.port);
    EXPECT_EQ(AF_INET, addr->sin_family);
    EXPECT_EQ(inet_addr("127.0.0.1"), addr->sin_addr.s_addr);
}

TEST(LocalEndpointUnitTests, SetsIpv6FamilyAndAddress) {
    LocalEndpoint local_endpoint;

    local_endpoint_build(&local_endpoint);

    // localhost ipv6 address:
    in6_addr ipv6_addr = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };

    local_endpoint_with_port(&local_endpoint, 5005);
    local_endpoint_with_ipv6(&local_endpoint, ipv6_addr);
    sockaddr_in6* addr = (struct sockaddr_in6*)&local_endpoint.data.address;

    EXPECT_EQ(LOCAL_ENDPOINT_TYPE_ADDRESS, local_endpoint.type);
    EXPECT_EQ(AF_INET6, addr->sin6_family);
    EXPECT_EQ(5005, ntohs(addr->sin6_port));
    EXPECT_EQ(5005, local_endpoint.port);
    EXPECT_EQ(0, memcmp(&ipv6_addr, &addr->sin6_addr, sizeof(in6_addr)));
}