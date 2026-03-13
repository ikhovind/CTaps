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
    sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint->resolved_address;
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
    EXPECT_EQ(remote_endpoint->resolved_address.ss_family, AF_UNSPEC);

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
    sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint->resolved_address;
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
    EXPECT_EQ(remote_endpoint->resolved_address.ss_family, AF_UNSPEC);

    ct_remote_endpoint_free(remote_endpoint);
}

TEST(RemoteEndpointResolvedEqualsTest, NullEndpoint1_ReturnsFalse) {
    ct_remote_endpoint_t rem = {0};
    ((struct sockaddr_in*)&rem.resolved_address)->sin_family = AF_INET;
    EXPECT_FALSE(ct_remote_endpoint_resolved_equals(nullptr, &rem));
}

TEST(RemoteEndpointResolvedEqualsTest, NullEndpoint2_ReturnsFalse) {
    ct_remote_endpoint_t rem = {0};
    ((struct sockaddr_in*)&rem.resolved_address)->sin_family = AF_INET;
    EXPECT_FALSE(ct_remote_endpoint_resolved_equals(&rem, nullptr));
}

TEST(RemoteEndpointResolvedEqualsTest, BothNull_ReturnsFalse) {
    EXPECT_FALSE(ct_remote_endpoint_resolved_equals(nullptr, nullptr));
}

TEST(RemoteEndpointResolvedEqualsTest, EqualAddresses_ReturnsTrue) {
    ct_remote_endpoint_t rem1 = {0}, rem2 = {0};
    struct sockaddr_in* a = (struct sockaddr_in*)&rem1.resolved_address;
    struct sockaddr_in* b = (struct sockaddr_in*)&rem2.resolved_address;
    a->sin_family = b->sin_family = AF_INET;
    a->sin_port   = b->sin_port   = htons(8080);
    inet_pton(AF_INET, "10.0.0.1", &a->sin_addr);
    inet_pton(AF_INET, "10.0.0.1", &b->sin_addr);
    EXPECT_TRUE(ct_remote_endpoint_resolved_equals(&rem1, &rem2));
}

TEST(RemoteEndpointResolvedEqualsTest, DifferentAddresses_ReturnsFalse) {
    ct_remote_endpoint_t rem1 = {0}, rem2 = {0};
    struct sockaddr_in* a = (struct sockaddr_in*)&rem1.resolved_address;
    struct sockaddr_in* b = (struct sockaddr_in*)&rem2.resolved_address;
    a->sin_family = b->sin_family = AF_INET;
    a->sin_port   = b->sin_port   = htons(8080);
    inet_pton(AF_INET, "10.0.0.1", &a->sin_addr);
    inet_pton(AF_INET, "10.0.0.2", &b->sin_addr);
    EXPECT_FALSE(ct_remote_endpoint_resolved_equals(&rem1, &rem2));
}
