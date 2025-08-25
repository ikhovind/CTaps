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


TEST(RemoteEndpointUnitTests, SetsIpv4FamilyAndAddress) {
    RemoteEndpoint remote_endpoint;

    remote_endpoint_with_port(&remote_endpoint, 5005);
    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));

    sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint.data.address;
    EXPECT_EQ(5005, ntohs(addr->sin_port));
    EXPECT_EQ(5005, remote_endpoint.port);
    EXPECT_EQ(AF_INET, addr->sin_family);
    EXPECT_EQ(inet_addr("127.0.0.1"), addr->sin_addr.s_addr);
}

TEST(RemoteEndpointUnitTests, SetsIpv6FamilyAndAddress) {
    RemoteEndpoint remote_endpoint;

    // localhost ipv6 address:
    in6_addr ipv6_addr = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };

    remote_endpoint_with_port(&remote_endpoint, 5005);
    remote_endpoint_with_ipv6(&remote_endpoint, ipv6_addr);
    sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint.data.address;

    EXPECT_EQ(AF_INET6, addr->sin6_family);
    EXPECT_EQ(5005, ntohs(addr->sin6_port));
    EXPECT_EQ(5005, remote_endpoint.port);
    EXPECT_EQ(0, memcmp(&ipv6_addr, &addr->sin6_addr, sizeof(in6_addr)));
}


/*
TEST(RemoteEndpointUnitTests, CanDnsLookupHostName) {
    ctaps_initialize();
    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);

    remote_endpoint_with_hostname(&remote_endpoint, "google.com");
}
*/
