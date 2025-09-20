#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "util/util.h"
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
}


TEST(RemoteEndpointUnitTests, SetsIpv4FamilyAndAddress) {
    RemoteEndpoint remote_endpoint;

    remote_endpoint_build(&remote_endpoint);

    remote_endpoint_with_port(&remote_endpoint, 5005);
    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));

    RemoteEndpoint* out_list;
    size_t out_count = 0;

    remote_endpoint_resolve(&remote_endpoint, &out_list, &out_count);

    sockaddr_in* addr = (struct sockaddr_in*)&out_list[0].data.resolved_address;

    EXPECT_EQ(5005, ntohs(addr->sin_port));
    EXPECT_EQ(5005, remote_endpoint.port);
    EXPECT_EQ(AF_INET, addr->sin_family);
    EXPECT_EQ(inet_addr("127.0.0.1"), addr->sin_addr.s_addr);
    free(out_list);
}

TEST(RemoteEndpointUnitTests, SetsIpv6FamilyAndAddress) {
    RemoteEndpoint remote_endpoint;

    remote_endpoint_build(&remote_endpoint);

    // localhost ipv6 resolved_address:
    in6_addr ipv6_addr = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };

    remote_endpoint_with_port(&remote_endpoint, 5005);
    remote_endpoint_with_ipv6(&remote_endpoint, ipv6_addr);

    RemoteEndpoint* out_list;
    size_t out_count = 0;

    remote_endpoint_resolve(&remote_endpoint, &out_list, &out_count);

    sockaddr_in6* addr = (struct sockaddr_in6*)&out_list[0].data.resolved_address;

    EXPECT_EQ(AF_INET6, addr->sin6_family);
    EXPECT_EQ(5005, ntohs(addr->sin6_port));
    EXPECT_EQ(5005, remote_endpoint.port);
    EXPECT_EQ(0, memcmp(&ipv6_addr, &addr->sin6_addr, sizeof(in6_addr)));
    free(out_list);
}

TEST(RemoteEndpointUnitTests, TakesDeepCopyOfHostname) {
    RemoteEndpoint remote_endpoint;

    remote_endpoint_build(&remote_endpoint);

    char hostname[] = "hello.com";
    int rc = remote_endpoint_with_hostname(&remote_endpoint, hostname);

    ASSERT_EQ(0, rc);
    EXPECT_STREQ(hostname, remote_endpoint.hostname);
    for (int i = 0; i < strlen(hostname); i++) {
        hostname[i] = 'a';
    }
    EXPECT_STREQ("aaaaaaaaa", hostname);
    EXPECT_STREQ("hello.com", remote_endpoint.hostname);
}

TEST(RemoteEndpointUnitTests, TakesDeepCopyOfService) {
    RemoteEndpoint remote_endpoint;

    char test_service[] = "test_service";
    remote_endpoint_build(&remote_endpoint);
    remote_endpoint_with_service(&remote_endpoint, test_service);

    test_service[0] = 'T';

    EXPECT_STREQ(remote_endpoint.service, "test_service");
    EXPECT_STREQ(test_service, "Test_service");
}
