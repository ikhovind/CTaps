#include <gmock/gmock-matchers.h>
#include <netinet/in.h>

#include "gtest/gtest.h"
extern "C" {
#include "ctaps.h"
#include "endpoint/remote_endpoint.h"
#include "endpoint/util.h"
#include "ctaps_internal.h"
}

TEST(SockaddrEqualTest, IPv4_EqualAddressAndPort) {
    struct sockaddr_storage a = {0}, b = {0};
    struct sockaddr_in* a4 = (struct sockaddr_in*)&a;
    struct sockaddr_in* b4 = (struct sockaddr_in*)&b;
    a4->sin_family = b4->sin_family = AF_INET;
    a4->sin_port   = b4->sin_port   = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &a4->sin_addr);
    inet_pton(AF_INET, "192.168.1.1", &b4->sin_addr);
    EXPECT_TRUE(ct_sockaddr_equal(&a, &b));
}

TEST(SockaddrEqualTest, IPv4_DifferentPort) {
    struct sockaddr_storage a = {0}, b = {0};
    struct sockaddr_in* a4 = (struct sockaddr_in*)&a;
    struct sockaddr_in* b4 = (struct sockaddr_in*)&b;
    a4->sin_family = b4->sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.1", &a4->sin_addr);
    inet_pton(AF_INET, "192.168.1.1", &b4->sin_addr);
    a4->sin_port = htons(8080);
    b4->sin_port = htons(9090);
    EXPECT_FALSE(ct_sockaddr_equal(&a, &b));
}

TEST(SockaddrEqualTest, IPv4_DifferentAddress) {
    struct sockaddr_storage a = {0}, b = {0};
    struct sockaddr_in* a4 = (struct sockaddr_in*)&a;
    struct sockaddr_in* b4 = (struct sockaddr_in*)&b;
    a4->sin_family = b4->sin_family = AF_INET;
    a4->sin_port   = b4->sin_port   = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &a4->sin_addr);
    inet_pton(AF_INET, "192.168.1.2", &b4->sin_addr);
    EXPECT_FALSE(ct_sockaddr_equal(&a, &b));
}

TEST(SockaddrEqualTest, IPv6_EqualAddressAndPort) {
    struct sockaddr_storage a = {0}, b = {0};
    struct sockaddr_in6* a6 = (struct sockaddr_in6*)&a;
    struct sockaddr_in6* b6 = (struct sockaddr_in6*)&b;
    a6->sin6_family = b6->sin6_family = AF_INET6;
    a6->sin6_port   = b6->sin6_port   = htons(443);
    inet_pton(AF_INET6, "::1", &a6->sin6_addr);
    inet_pton(AF_INET6, "::1", &b6->sin6_addr);
    EXPECT_TRUE(ct_sockaddr_equal(&a, &b));
}

TEST(SockaddrEqualTest, IPv6_DifferentPort) {
    struct sockaddr_storage a = {0}, b = {0};
    struct sockaddr_in6* a6 = (struct sockaddr_in6*)&a;
    struct sockaddr_in6* b6 = (struct sockaddr_in6*)&b;
    a6->sin6_family = b6->sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::1", &a6->sin6_addr);
    inet_pton(AF_INET6, "::1", &b6->sin6_addr);
    a6->sin6_port = htons(443);
    b6->sin6_port = htons(8443);
    EXPECT_FALSE(ct_sockaddr_equal(&a, &b));
}

TEST(SockaddrEqualTest, IPv6_DifferentAddress) {
    struct sockaddr_storage a = {0}, b = {0};
    struct sockaddr_in6* a6 = (struct sockaddr_in6*)&a;
    struct sockaddr_in6* b6 = (struct sockaddr_in6*)&b;
    a6->sin6_family = b6->sin6_family = AF_INET6;
    a6->sin6_port   = b6->sin6_port   = htons(443);
    inet_pton(AF_INET6, "::1", &a6->sin6_addr);
    inet_pton(AF_INET6, "::2", &b6->sin6_addr);
    EXPECT_FALSE(ct_sockaddr_equal(&a, &b));
}

TEST(SockaddrEqualTest, FamilyMismatch_IPv4vsIPv6) {
    struct sockaddr_storage a = {0}, b = {0};
    ((struct sockaddr_in*)&a)->sin_family  = AF_INET;
    ((struct sockaddr_in6*)&b)->sin6_family = AF_INET6;
    EXPECT_FALSE(ct_sockaddr_equal(&a, &b));
}

TEST(SockaddrEqualTest, UnknownFamily_ReturnsFalse) {
    struct sockaddr_storage a = {0}, b = {0};
    a.ss_family = b.ss_family = AF_UNIX;  // not handled by the function
    EXPECT_FALSE(ct_sockaddr_equal(&a, &b));
}
