#include <gmock/gmock-matchers.h>
#include <netinet/in.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "endpoint/remote_endpoint.h"
#include "ctaps_internal.h"
}

TEST(RemoteEndpointUnitTests, TakesDeepCopyOfHostname) {
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    char hostname[] = "hello.com";
    int rc = ct_remote_endpoint_with_hostname(remote_endpoint, hostname);

    ASSERT_EQ(0, rc);
    EXPECT_STREQ(hostname, remote_endpoint->hostname);
    for (int i = 0; i < strlen(hostname); i++) {
        hostname[i] = 'a';
    }
    EXPECT_STREQ("aaaaaaaaa", hostname);
    EXPECT_STREQ("hello.com", remote_endpoint->hostname);

    ct_remote_endpoint_free(remote_endpoint);
}

TEST(RemoteEndpointUnitTests, TakesDeepCopyOfService) {
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    char test_service[] = "test_service";
    ct_remote_endpoint_with_service(remote_endpoint, test_service);

    test_service[0] = 'T';

    EXPECT_STREQ(remote_endpoint->service, "test_service");
    EXPECT_STREQ(test_service, "Test_service");

    ct_remote_endpoint_free(remote_endpoint);
}

TEST(RemoteEndpointUnitTests, FailsWhenSpecifyingHostnameAfterIpv4) {
    int rc;
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    rc = ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ASSERT_EQ(rc, 0);
    rc = ct_remote_endpoint_with_hostname(remote_endpoint, "hello.com");
    ASSERT_EQ(rc, -EINVAL);
    EXPECT_STREQ(remote_endpoint->hostname, NULL);
    sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint->data.resolved_address;
    EXPECT_EQ(addr->sin_addr.s_addr, inet_addr("127.0.0.1"));

    ct_remote_endpoint_free(remote_endpoint);
}

TEST(RemoteEndpointUnitTests, FailsWhenSpecifyingIpv4AfterHostname) {
    int rc;
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    rc = ct_remote_endpoint_with_hostname(remote_endpoint, "hello.com");
    EXPECT_EQ(rc, 0);
    rc = ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    EXPECT_EQ(rc, -EINVAL);
    EXPECT_STREQ(remote_endpoint->hostname, "hello.com");
    EXPECT_EQ(remote_endpoint->data.resolved_address.ss_family, AF_UNSPEC);

    ct_remote_endpoint_free(remote_endpoint);
}

TEST(RemoteEndpointUnitTests, FailsWhenSpecifyingHostnameAfterIpv6) {
    int rc;
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    in6_addr ipv6_addr = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };

    rc = ct_remote_endpoint_with_ipv6(remote_endpoint, ipv6_addr);
    ASSERT_EQ(rc, 0);
    rc = ct_remote_endpoint_with_hostname(remote_endpoint, "hello.com");
    ASSERT_EQ(rc, -EINVAL);
    EXPECT_STREQ(remote_endpoint->hostname, NULL);
    sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint->data.resolved_address;
    EXPECT_EQ(0, memcmp(&ipv6_addr, &addr->sin6_addr, sizeof(in6_addr)));

    ct_remote_endpoint_free(remote_endpoint);
}

TEST(RemoteEndpointUnitTests, FailsWhenSpecifyingIpv6AfterHostname) {
    int rc;
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    in6_addr ipv6_addr = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };

    rc = ct_remote_endpoint_with_hostname(remote_endpoint, "hello.com");

    ASSERT_EQ(rc, 0);
    rc = ct_remote_endpoint_with_ipv6(remote_endpoint, ipv6_addr);
    ASSERT_EQ(rc, -EINVAL);
    EXPECT_STREQ(remote_endpoint->hostname, "hello.com");
    EXPECT_EQ(remote_endpoint->data.resolved_address.ss_family, AF_UNSPEC);

    ct_remote_endpoint_free(remote_endpoint);
}

TEST(RemoteEndpointUnitTest, remoteEndpointEqualsIgnoresNonSockaddrProperties) {
    ct_remote_endpoint_t rem1 = {0};
    ct_remote_endpoint_t rem2 = {0};

    rem1.port = 1234;
    rem2.port = 4321;

    struct sockaddr_in* rem1_addr = (struct sockaddr_in*)&rem1.data.resolved_address;
    struct sockaddr_in* rem2_addr = (struct sockaddr_in*)&rem2.data.resolved_address;

    rem1_addr->sin_addr.s_addr = INADDR_LOOPBACK;
    rem2_addr->sin_addr.s_addr = INADDR_LOOPBACK;
    rem1.port = 1234;
    rem2.port = 1234;

    ASSERT_TRUE(ct_remote_endpoint_resolved_equals(&rem1, &rem2));
}

TEST(RemoteEndpointUnitTest, remoteEndpointEqualsReturnsFalseOnFamilyDiff) {
    ct_remote_endpoint_t rem1 = {0};
    ct_remote_endpoint_t rem2 = {0};

    struct sockaddr_in* rem1_addr = (struct sockaddr_in*)&rem1.data.resolved_address;
    struct sockaddr_in* rem2_addr = (struct sockaddr_in*)&rem2.data.resolved_address;

    rem2_addr->sin_family = AF_INET6;
    rem1.port = 1234;
    rem2.port = 1234;

    ASSERT_FALSE(ct_remote_endpoint_resolved_equals(&rem1, &rem2));
}

TEST(RemoteEndpointUnitTest, remoteEndpointEqualsReturnsFalseOnDiff) {
    ct_remote_endpoint_t rem1 = {0};
    ct_remote_endpoint_t rem2 = {0};

    struct sockaddr_in* rem1_addr = (struct sockaddr_in*)&rem1.data.resolved_address;
    struct sockaddr_in* rem2_addr = (struct sockaddr_in*)&rem2.data.resolved_address;

    rem1_addr->sin_family = AF_INET;
    rem2_addr->sin_family = AF_INET;
    rem1_addr->sin_port = 1234;
    rem2_addr->sin_port = 4321;

    ASSERT_FALSE(ct_remote_endpoint_resolved_equals(&rem1, &rem2));
}
