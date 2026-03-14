#include <gmock/gmock-matchers.h>

#include "fixtures/integration_fixture.h"
#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, __wrap_uv_getaddrinfo, uv_loop_t*,
                             uv_getaddrinfo_t*,
                             uv_getaddrinfo_cb,
                             const char*,
                             const char*,
                             const struct addrinfo*);

FAKE_VOID_FUNC(__wrap_uv_freeaddrinfo,struct addrinfo*);



    static int fake_uv_getaddrinfo_impl(uv_loop_t* loop, uv_getaddrinfo_t* req,
                                         uv_getaddrinfo_cb cb, const char* node,
                                         const char* service, const struct addrinfo* hints) {
        struct sockaddr_in* addr = (struct sockaddr_in*)calloc(1, sizeof(struct sockaddr_in));
        addr->sin_family = AF_INET;
        inet_pton(AF_INET, "142.250.74.46", &addr->sin_addr);

        struct addrinfo* res = (struct addrinfo*)calloc(1, sizeof(struct addrinfo));
        res->ai_family = AF_INET;
        res->ai_addr = (struct sockaddr*)addr;
        res->ai_addrlen = sizeof(struct sockaddr_in);
        res->ai_next = NULL;

        req->addrinfo = res;
        cb(req, 0, res);
        return 0;
    }
}

class DnsIntegrationTest : public CTapsGenericFixture {
protected:
    void SetUp() override {
        CTapsGenericFixture::SetUp();
        // Reset all mock data before each test
        FFF_RESET_HISTORY();
        RESET_FAKE(__wrap_uv_getaddrinfo);
    }
};
TEST_F(DnsIntegrationTest, canDnsLookupHostName) {
    __wrap_uv_getaddrinfo_fake.custom_fake = fake_uv_getaddrinfo_impl;

    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);
    ct_remote_endpoint_with_hostname(remote_endpoint, "google.com");
    ct_remote_endpoint_with_port(remote_endpoint, 1234);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    ASSERT_NE(transport_properties, nullptr);
    ct_transport_properties_set_reliability(transport_properties, PROHIBIT);
    ct_transport_properties_set_preserve_order(transport_properties, PROHIBIT);
    ct_transport_properties_set_congestion_control(transport_properties, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties, NULL);
    ASSERT_NE(preconnection, nullptr);

    ct_connection_callbacks_t connection_callbacks = {
        .ready = on_connection_ready,
        .per_connection_context = &test_context
    };
    ct_preconnection_initiate(preconnection, connection_callbacks);
    ct_start_event_loop();

    ct_connection_t* saved_connection = test_context.client_connections[0];

    struct sockaddr_in* addr = (struct sockaddr_in*)&ct_connection_get_active_remote_endpoint(saved_connection)->resolved_address;
    EXPECT_EQ(addr->sin_family, AF_INET);
    EXPECT_EQ(ntohs(addr->sin_port), 1234);
    EXPECT_EQ(ct_connection_get_active_remote_endpoint(saved_connection)->port, 1234);

    // Verify the mock was actually called
    EXPECT_GE(__wrap_uv_getaddrinfo_fake.call_count, 1);
    for (size_t i = 0; i < __wrap_uv_getaddrinfo_fake.call_count; i++) {
        EXPECT_STREQ(__wrap_uv_getaddrinfo_fake.arg3_history[i], "google.com");
    }

    EXPECT_EQ(__wrap_uv_freeaddrinfo_fake.call_count, __wrap_uv_getaddrinfo_fake.call_count);

    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}
