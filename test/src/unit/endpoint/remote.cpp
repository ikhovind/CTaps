#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
}


TEST(RemoteEndpointUnitTests, SetsIpv4FamilyAndAddress) {
    ct_remote_endpoint_t remote_endpoint;

    ct_remote_endpoint_build(&remote_endpoint);

    ct_remote_endpoint_with_port(&remote_endpoint, 5005);
    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));

    ct_remote_endpoint_t* out_list;
    size_t out_count = 0;

    ct_remote_endpoint_resolve(&remote_endpoint, &out_list, &out_count);

    sockaddr_in* addr = (struct sockaddr_in*)&out_list[0].data.resolved_address;

    EXPECT_EQ(5005, ntohs(addr->sin_port));
    EXPECT_EQ(5005, remote_endpoint.port);
    EXPECT_EQ(AF_INET, addr->sin_family);
    EXPECT_EQ(inet_addr("127.0.0.1"), addr->sin_addr.s_addr);
    free(out_list);
}

TEST(RemoteEndpointUnitTests, SetsIpv6FamilyAndAddress) {
    ct_remote_endpoint_t remote_endpoint;

    ct_remote_endpoint_build(&remote_endpoint);

    // localhost ipv6 resolved_address:
    in6_addr ipv6_addr = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };

    ct_remote_endpoint_with_port(&remote_endpoint, 5005);
    ct_remote_endpoint_with_ipv6(&remote_endpoint, ipv6_addr);

    ct_remote_endpoint_t* out_list;
    size_t out_count = 0;

    ct_remote_endpoint_resolve(&remote_endpoint, &out_list, &out_count);

    sockaddr_in6* addr = (struct sockaddr_in6*)&out_list[0].data.resolved_address;

    EXPECT_EQ(AF_INET6, addr->sin6_family);
    EXPECT_EQ(5005, ntohs(addr->sin6_port));
    EXPECT_EQ(5005, remote_endpoint.port);
    EXPECT_EQ(0, memcmp(&ipv6_addr, &addr->sin6_addr, sizeof(in6_addr)));
    free(out_list);
}

TEST(RemoteEndpointUnitTests, TakesDeepCopyOfHostname) {
    ct_remote_endpoint_t remote_endpoint;

    ct_remote_endpoint_build(&remote_endpoint);

    char hostname[] = "hello.com";
    int rc = ct_remote_endpoint_with_hostname(&remote_endpoint, hostname);

    ASSERT_EQ(0, rc);
    EXPECT_STREQ(hostname, remote_endpoint.hostname);
    for (int i = 0; i < strlen(hostname); i++) {
        hostname[i] = 'a';
    }
    EXPECT_STREQ("aaaaaaaaa", hostname);
    EXPECT_STREQ("hello.com", remote_endpoint.hostname);
}

TEST(RemoteEndpointUnitTests, TakesDeepCopyOfService) {
    ct_remote_endpoint_t remote_endpoint;

    char test_service[] = "test_service";
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_service(&remote_endpoint, test_service);

    test_service[0] = 'T';

    EXPECT_STREQ(remote_endpoint.service, "test_service");
    EXPECT_STREQ(test_service, "Test_service");
}

TEST(RemoteEndpointUnitTests, FailsWhenSpecifyingHostnameAfterIpv4) {
    int rc;
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);

    rc = ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ASSERT_EQ(rc, 0);
    rc = ct_remote_endpoint_with_hostname(&remote_endpoint, "hello.com");
    ASSERT_EQ(rc, -EINVAL);
    EXPECT_STREQ(remote_endpoint.hostname, NULL);
    sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint.data.resolved_address;
    EXPECT_EQ(addr->sin_addr.s_addr, inet_addr("127.0.0.1"));
}

TEST(RemoteEndpointUnitTests, FailsWhenSpecifyingIpv4AfterHostname) {
    int rc;
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);

    rc = ct_remote_endpoint_with_hostname(&remote_endpoint, "hello.com");
    EXPECT_EQ(rc, 0);
    rc = ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    EXPECT_EQ(rc, -EINVAL);
    EXPECT_STREQ(remote_endpoint.hostname, "hello.com");
    EXPECT_EQ(remote_endpoint.data.resolved_address.ss_family, AF_UNSPEC);
}

TEST(RemoteEndpointUnitTests, FailsWhenSpecifyingHostnameAfterIpv6) {
    int rc;
    ct_remote_endpoint_t remote_endpoint;

    ct_remote_endpoint_build(&remote_endpoint);

    in6_addr ipv6_addr = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };

    rc = ct_remote_endpoint_with_ipv6(&remote_endpoint, ipv6_addr);
    ASSERT_EQ(rc, 0);
    rc = ct_remote_endpoint_with_hostname(&remote_endpoint, "hello.com");
    ASSERT_EQ(rc, -EINVAL);
    EXPECT_STREQ(remote_endpoint.hostname, NULL);
    sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint.data.resolved_address;
    EXPECT_EQ(0, memcmp(&ipv6_addr, &addr->sin6_addr, sizeof(in6_addr)));
}

TEST(RemoteEndpointUnitTests, FailsWhenSpecifyingIpv6AfterHostname) {
    int rc;
    ct_remote_endpoint_t remote_endpoint;

    ct_remote_endpoint_build(&remote_endpoint);

    in6_addr ipv6_addr = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };

    rc = ct_remote_endpoint_with_hostname(&remote_endpoint, "hello.com");

    ASSERT_EQ(rc, 0);
    rc = ct_remote_endpoint_with_ipv6(&remote_endpoint, ipv6_addr);
    ASSERT_EQ(rc, -EINVAL);
    EXPECT_STREQ(remote_endpoint.hostname, "hello.com");
    EXPECT_EQ(remote_endpoint.data.resolved_address.ss_family, AF_UNSPEC);
}
