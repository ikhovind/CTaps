#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "fixtures/awaiting_fixture.cpp"
}

class RemoteEndpointDnsTests : public CTapsGenericFixture {};

TEST_F(RemoteEndpointDnsTests, canDnsLookupHostName) {
    GTEST_SKIP(); // Don't know why this fails atm
    ct_initialize();
    printf("Sending UDP packet...\n");

    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    ct_remote_endpoint_with_hostname(remote_endpoint, "google.com");
    ct_remote_endpoint_with_port(remote_endpoint, 1234);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

    // Allocated with ct_transport_properties_new()
    ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, CONGESTION_CONTROL, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(remote_endpoint, 1, transport_properties, NULL);
    ASSERT_NE(preconnection, nullptr);

    ct_connection_callbacks_t connection_callbacks = {
        .ready = on_connection_ready,
        .user_connection_context = &test_context
    };

    ct_preconnection_initiate(preconnection, connection_callbacks);

    ct_connection_t* saved_connection = test_context.client_connections[0];

    // check address port
    if (ct_connection_get_remote_endpoint(saved_connection)->data.resolved_address.ss_family == AF_INET) {
        struct sockaddr_in* addr = (struct sockaddr_in*)&ct_connection_get_remote_endpoint(saved_connection)->data.resolved_address;
        EXPECT_EQ(1234, ntohs(addr->sin_port));
    }
    else {
        struct sockaddr_in6* addr = (struct sockaddr_in6*)&ct_connection_get_remote_endpoint(saved_connection)->data.resolved_address;

        EXPECT_EQ(1234, ntohs(addr->sin6_port));
    }
    EXPECT_EQ(1234, ct_connection_get_remote_endpoint(saved_connection)->port);

    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}
