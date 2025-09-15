#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "util/util.h"
#include "ctaps.h"
#include "endpoints/local/local_endpoint.h"
#include "fff.h"
}

#include "fixtures/awaiting_fixture.cpp"

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

extern "C" {
DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, uv_udp_bind, uv_udp_t*, const struct sockaddr*, unsigned int);
FAKE_VALUE_FUNC(int, uv_interface_addresses, uv_interface_address_t**, int*);
FAKE_VOID_FUNC(uv_free_interface_addresses, uv_interface_address_t*, int);
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



TEST_F(CTapsGenericFixture, BindUsesCorrectInterfaceAddress) {
    FFF_RESET_HISTORY();
    // --- ARRANGE ---
    // Set our custom function as the implementation for the fake
    uv_interface_addresses_fake.custom_fake = custom_uv_interface_addresses;

    // Set a default return value for the other fake
    uv_udp_bind_fake.return_val = 0;

    // Create and configure the LocalEndpoint to use our test interface
    LocalEndpoint local_endpoint;
    local_endpoint_build(&local_endpoint);
    local_endpoint_with_port(&local_endpoint, 8080);
    local_endpoint_with_interface(&local_endpoint, "test_if0");

    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);

    TransportProperties transport_properties;
    transport_properties_build(&transport_properties);
    tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);

    Preconnection preconnection;
    preconnection_build_with_local(&preconnection, transport_properties, &remote_endpoint, 1, local_endpoint);

    CallbackContext callback_context = {
        .awaiter = &awaiter,
        .messages = &received_messages,
    };

    InitDoneCb init_done_cb = {
        .init_done_callback = on_connection_ready,
        .user_data = &callback_context
    };


    // --- ACT ---

    Connection connection;
    preconnection_initiate(&preconnection, &connection, init_done_cb, nullptr);
    awaiter.await(1);

    // --- ASSERT ---
    // 1. Verify that our mocked functions were called as expected
    ASSERT_EQ(uv_interface_addresses_fake.call_count, 1);
    ASSERT_EQ(uv_udp_bind_fake.call_count, 1);
    ASSERT_EQ(uv_free_interface_addresses_fake.call_count, 1);

    // 2. This is the key assertion: Inspect the address passed to uv_udp_bind
    const struct sockaddr* bound_addr = uv_udp_bind_fake.arg1_val;
    ASSERT_NE(bound_addr, nullptr);
    ASSERT_EQ(bound_addr->sa_family, AF_INET);

    // Cast to sockaddr_in to check the IP and port
    const struct sockaddr_in* bound_addr_in = (const struct sockaddr_in*)bound_addr;

    // Verify the IP address matches the one we set for "test_if0" in our fake data
    EXPECT_STREQ(inet_ntoa(bound_addr_in->sin_addr), "192.168.1.100");

    // Verify the port was set correctly
    EXPECT_EQ(ntohs(bound_addr_in->sin_port), 8080);
}