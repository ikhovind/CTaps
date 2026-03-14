#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include "fff.h"
#include "endpoint/local_endpoint.h"
#include "endpoint/remote_endpoint.h"
}

#include "fixtures/integration_fixture.h"

extern "C" {
DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, uv_udp_bind, uv_udp_t*, const struct sockaddr*, unsigned int);
FAKE_VALUE_FUNC(int, uv_interface_addresses, uv_interface_address_t**, int*);
FAKE_VOID_FUNC(uv_free_interface_addresses, uv_interface_address_t*, int);
FAKE_VALUE_FUNC(int, faked_resolve_local_endpoint_from_handle, uv_handle_t*, ct_connection_t*);
}

// Fake this function, otherwise it will overwrite the address passed to out uv_udp_bind, meaning that the argument
// list will be overwritten
extern "C" int __wrap_resolve_local_endpoint_from_handle(uv_handle_t* handle, ct_connection_t* connection) {
  // Call the fff fake, which allows us to track calls and set return values.
  return faked_resolve_local_endpoint_from_handle(handle, connection);
}

static uv_interface_address_t fake_interfaces[] = {
    {
        .name = "test_if0", // The interface name we'll test for
        .address = {
            .address4 = {
                .sin_family = AF_INET,
                .sin_addr = { .s_addr = inet_addr("192.168.1.100") }
            }
        }
    },
    {
        .name = "lo",
        .address = {
            .address4 = {
                .sin_family = AF_INET,
                .sin_addr = { .s_addr = inet_addr("127.0.0.1") }
            }
        }
    }
};

// This function will be the implementation of our fake
int custom_uv_interface_addresses(uv_interface_address_t** addresses, int* count) {
  *addresses = fake_interfaces;
  *count = sizeof(fake_interfaces) / sizeof(fake_interfaces[0]);
  return 0;
}


TEST(LocalEndpointUnitTests, setsPort) {
    ct_local_endpoint_t local_endpoint;
    ct_local_endpoint_build(&local_endpoint);
    ct_local_endpoint_with_port(&local_endpoint, 5005);

    EXPECT_EQ(local_endpoint.port, 5005);
}


TEST(LocalEndpointUnitTests, TakesDeepCopyOfService) {
    ct_local_endpoint_t local_endpoint;

    char test_service[] = "test_service";
    ct_local_endpoint_build(&local_endpoint);
    ct_local_endpoint_with_service(&local_endpoint, test_service);

    test_service[0] = 'T';

    EXPECT_STREQ(local_endpoint.service, "test_service");
    EXPECT_STREQ(test_service, "Test_service");
}

TEST(LocalEndpointUnitTests, TakesDeepCopyOfInterface) {
    ct_local_endpoint_t local_endpoint;

    char test_interface[] = "test_interface";
    ct_local_endpoint_build(&local_endpoint);
    ct_local_endpoint_with_interface(&local_endpoint, test_interface);

    test_interface[0] = 'T';

    EXPECT_STREQ(local_endpoint.interface_name, "test_interface");
    EXPECT_STREQ(test_interface, "Test_interface");
    free(local_endpoint.interface_name);
}

TEST(LocalEndpointResolvedEqualsTest, NullEndpoint1_ReturnsFalse) {
    ct_local_endpoint_t loc = {0};
    ((struct sockaddr_in*)&loc.resolved_address)->sin_family = AF_INET;
    EXPECT_FALSE(ct_local_endpoint_resolved_equals(nullptr, &loc));
}

TEST(LocalEndpointResolvedEqualsTest, NullEndpoint2_ReturnsFalse) {
    ct_local_endpoint_t loc = {0};
    ((struct sockaddr_in*)&loc.resolved_address)->sin_family = AF_INET;
    EXPECT_FALSE(ct_local_endpoint_resolved_equals(&loc, nullptr));
}

TEST(LocalEndpointResolvedEqualsTest, BothNull_ReturnsFalse) {
    EXPECT_FALSE(ct_local_endpoint_resolved_equals(nullptr, nullptr));
}

TEST(LocalEndpointResolvedEqualsTest, EqualAddresses_ReturnsTrue) {
    ct_local_endpoint_t loc1 = {0}, loc2 = {0};
    struct sockaddr_in* a = (struct sockaddr_in*)&loc1.resolved_address;
    struct sockaddr_in* b = (struct sockaddr_in*)&loc2.resolved_address;
    a->sin_family = b->sin_family = AF_INET;
    a->sin_port   = b->sin_port   = htons(8080);
    inet_pton(AF_INET, "10.0.0.1", &a->sin_addr);
    inet_pton(AF_INET, "10.0.0.1", &b->sin_addr);
    EXPECT_TRUE(ct_local_endpoint_resolved_equals(&loc1, &loc2));
}

TEST(LocalEndpointResolvedEqualsTest, DifferentAddresses_ReturnsFalse) {
    ct_local_endpoint_t loc1 = {0}, loc2 = {0};
    struct sockaddr_in* a = (struct sockaddr_in*)&loc1.resolved_address;
    struct sockaddr_in* b = (struct sockaddr_in*)&loc2.resolved_address;
    a->sin_family = b->sin_family = AF_INET;
    a->sin_port   = b->sin_port   = htons(8080);
    inet_pton(AF_INET, "10.0.0.1", &a->sin_addr);
    inet_pton(AF_INET, "10.0.0.2", &b->sin_addr);
    EXPECT_FALSE(ct_local_endpoint_resolved_equals(&loc1, &loc2));
}

TEST(LocalEndpointFromSockaddrTest, IPv4_SetsPortAndAddress) {
    ct_local_endpoint_t ep = {0};
    struct sockaddr_storage addr = {0};
    struct sockaddr_in* in = (struct sockaddr_in*)&addr;
    in->sin_family = AF_INET;
    in->sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &in->sin_addr);

    ASSERT_EQ(ct_local_endpoint_from_sockaddr(&ep, &addr), 0);
    EXPECT_EQ(ep.port, 8080);

    struct sockaddr_in* stored = (struct sockaddr_in*)&ep.resolved_address;
    EXPECT_EQ(stored->sin_family, AF_INET);
    EXPECT_EQ(stored->sin_port, htons(8080));
    EXPECT_EQ(memcmp(&stored->sin_addr, &in->sin_addr, sizeof(struct in_addr)), 0);
}

TEST(LocalEndpointFromSockaddrTest, IPv6_SetsPortAndAddress) {
    ct_local_endpoint_t ep = {0};
    struct sockaddr_storage addr = {0};
    struct sockaddr_in6* in6 = (struct sockaddr_in6*)&addr;
    in6->sin6_family = AF_INET6;
    in6->sin6_port = htons(443);
    inet_pton(AF_INET6, "::1", &in6->sin6_addr);

    ASSERT_EQ(ct_local_endpoint_from_sockaddr(&ep, &addr), 0);
    EXPECT_EQ(ep.port, 443);

    struct sockaddr_in6* stored = (struct sockaddr_in6*)&ep.resolved_address;
    EXPECT_EQ(stored->sin6_family, AF_INET6);
    EXPECT_EQ(stored->sin6_port, htons(443));
    EXPECT_EQ(memcmp(&stored->sin6_addr, &in6->sin6_addr, sizeof(struct in6_addr)), 0);
}

TEST(LocalEndpointFromSockaddrTest, ServiceAlreadySet_ReturnsEINVAL) {
    ct_local_endpoint_t ep = {0};
    ep.service = (char*)"http";
    struct sockaddr_storage addr = {0};
    ((struct sockaddr_in*)&addr)->sin_family = AF_INET;

    EXPECT_EQ(ct_local_endpoint_from_sockaddr(&ep, &addr), -EINVAL);
}

TEST(LocalEndpointFromSockaddrTest, UnknownFamily_ReturnsEINVAL) {
    ct_local_endpoint_t ep = {0};
    struct sockaddr_storage addr = {0};
    addr.ss_family = AF_UNIX;

    EXPECT_EQ(ct_local_endpoint_from_sockaddr(&ep, &addr), -EINVAL);
}

TEST(LocalEndpointFromSockaddrTest, ServiceAlreadySet_DoesNotModifyEndpoint) {
    ct_local_endpoint_t ep = {0};
    ep.service = (char*)"http";
    ep.port = 9999;
    struct sockaddr_storage addr = {0};
    ((struct sockaddr_in*)&addr)->sin_family = AF_INET;

    ct_local_endpoint_from_sockaddr(&ep, &addr);
    EXPECT_EQ(ep.port, 9999);  // unchanged
}

TEST(LocalEndpointFromSockaddrTest, nullAddrReturnsEINVAL) {
    ct_local_endpoint_t ep = {0};
    int rc = ct_local_endpoint_from_sockaddr(&ep, nullptr);
    ASSERT_EQ(rc, -EINVAL);
}
