#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "fff.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include "connection/preconnection.h"

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(ct_framer_impl_t*, __wrap_ct_framer_impl_deep_copy, const ct_framer_impl_t*);
FAKE_VOID_FUNC(__wrap_ct_framer_impl_free, ct_framer_impl_t*);
}


class PreconnectionUnitTests : public ::testing::Test {
protected:
    void SetUp() override {
        ct_set_log_level(CT_LOG_DEBUG);
        RESET_FAKE(__wrap_ct_framer_impl_deep_copy);
        RESET_FAKE(__wrap_ct_framer_impl_free);
        FFF_RESET_HISTORY();
    }

    
    void TearDown() override {
    }

    ct_preconnection_t dummy_precon = {0};

};


TEST_F(PreconnectionUnitTests, SetsPreconnectionAsExpected) {
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, 5005);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    ASSERT_NE(transport_properties, nullptr);

    // Allocated with ct_transport_properties_new()

    ct_transport_properties_set_reliability(transport_properties, PROHIBIT);
    ct_transport_properties_set_preserve_order(transport_properties, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
    ASSERT_NE(preconnection, nullptr);

    EXPECT_EQ(0, preconnection->num_local_endpoints);
    EXPECT_EQ(1, preconnection->num_remote_endpoints);
    EXPECT_EQ(AF_INET, preconnection->remote_endpoints[0].resolved_address.ss_family);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection->remote_endpoints[0].resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));
    EXPECT_EQ(memcmp(preconnection->remote_endpoints, remote_endpoint, sizeof(ct_remote_endpoint_t)), 0);

    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}

TEST_F(PreconnectionUnitTests, TakesDeepCopyOfRemoteEndpoint) {
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, 5005);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

    ct_transport_properties_set_reliability(transport_properties, PROHIBIT);
    ct_transport_properties_set_preserve_order(transport_properties, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties,NULL);
    ASSERT_NE(preconnection, nullptr);

    memset(remote_endpoint, 0, sizeof(ct_remote_endpoint_t));
    ASSERT_EQ(0, remote_endpoint->port);

    sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection->remote_endpoints[0].resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));

    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}

TEST_F(PreconnectionUnitTests, TakesDeepCopyOfRemoteEndpointWhenBuildingWithLocal) {
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, 5005);

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
    ASSERT_NE(local_endpoint, nullptr);

    ct_local_endpoint_with_port(local_endpoint, 6006);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

    // Allocated with ct_transport_properties_new()

    ct_transport_properties_set_reliability(transport_properties, PROHIBIT);
    ct_transport_properties_set_preserve_order(transport_properties, PROHIBIT);

    ct_preconnection_t* preconnection = ct_preconnection_new(local_endpoint, 1, remote_endpoint, 1, transport_properties,NULL);
    ASSERT_NE(preconnection, nullptr);

    memset(remote_endpoint, 0, sizeof(ct_remote_endpoint_t));
    ASSERT_EQ(0, remote_endpoint->port);

    sockaddr_in* addr_in = (struct sockaddr_in*)&preconnection->remote_endpoints[0].resolved_address;
    EXPECT_EQ(5005, ntohs(addr_in->sin_port));

    ct_local_endpoint_free(local_endpoint);
    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
}

TEST_F(PreconnectionUnitTests, newHandlesNullForOptionalParams) {
    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, NULL, 0,NULL,NULL);
    ASSERT_NE(preconnection, nullptr);
    EXPECT_EQ(0, preconnection->num_local_endpoints);
    EXPECT_EQ(0, preconnection->num_remote_endpoints);
    EXPECT_EQ(0, memcmp(&preconnection->transport_properties.selection_properties, &DEFAULT_SELECTION_PROPERTIES, sizeof(ct_selection_properties_t)));
    EXPECT_EQ(0, memcmp(&preconnection->transport_properties.connection_properties, &DEFAULT_CONNECTION_PROPERTIES, sizeof(ct_connection_properties_t)));
    EXPECT_EQ(NULL, preconnection->security_parameters);

    ct_preconnection_free(preconnection);
}

TEST_F(PreconnectionUnitTests, getLocalEndpointsHandlesNullCount) {
    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, NULL, 0,NULL,NULL);
    const ct_local_endpoint_t* endpoints = ct_preconnection_get_local_endpoints(preconnection, NULL);
    ASSERT_EQ(endpoints, nullptr);
    ct_preconnection_free(preconnection);
}

TEST_F(PreconnectionUnitTests, getLocalEndpointsHandlesNullPrecon) {
    size_t out_count = 5;
    const ct_local_endpoint_t* endpoints = ct_preconnection_get_local_endpoints(nullptr, &out_count);
    ASSERT_EQ(endpoints, nullptr);
    ASSERT_EQ(out_count, 0);
}

TEST_F(PreconnectionUnitTests, setFramerRejectsNullPreconnection) {
    ct_framer_impl_t dummy{};
    int result = ct_preconnection_set_framer(NULL, &dummy);
    ASSERT_EQ(result, -EINVAL);
    ASSERT_EQ(__wrap_ct_framer_impl_deep_copy_fake.call_count, 0);
    ASSERT_EQ(__wrap_ct_framer_impl_free_fake.call_count, 0);
}

TEST_F(PreconnectionUnitTests, setFramerNullOnNullWorks) {
    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, NULL, 0, NULL, NULL);
    EXPECT_EQ(preconnection->framer_impl, nullptr);
    int result = ct_preconnection_set_framer(preconnection, NULL);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(preconnection->framer_impl, nullptr);
    EXPECT_EQ(__wrap_ct_framer_impl_free_fake.call_count, 0);
    EXPECT_EQ(__wrap_ct_framer_impl_deep_copy_fake.call_count, 0);
    ct_preconnection_free(preconnection);
}

TEST_F(PreconnectionUnitTests, setFramerDeepCopiesImpl) {
    ct_framer_impl_t dummy{};
    ct_framer_impl_t copy{};
    __wrap_ct_framer_impl_deep_copy_fake.return_val = &copy;

    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, NULL, 0, NULL, NULL);
    int result = ct_preconnection_set_framer(preconnection, &dummy);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(__wrap_ct_framer_impl_deep_copy_fake.call_count, 1);
    EXPECT_EQ(__wrap_ct_framer_impl_deep_copy_fake.arg0_val, &dummy);
    EXPECT_EQ(preconnection->framer_impl, &copy);

    ct_preconnection_free(preconnection);
}

TEST_F(PreconnectionUnitTests, setFramerReplacesExistingImpl) {
    ct_framer_impl_t dummy{};
    ct_framer_impl_t first_copy{};
    ct_framer_impl_t second_copy{};
    __wrap_ct_framer_impl_deep_copy_fake.return_val = &first_copy;

    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, NULL, 0, NULL, NULL);
    EXPECT_EQ(ct_preconnection_set_framer(preconnection, &dummy), 0);

    __wrap_ct_framer_impl_deep_copy_fake.return_val = &second_copy;
    EXPECT_EQ(ct_preconnection_set_framer(preconnection, &dummy), 0);

    // Old copy was freed
    EXPECT_EQ(__wrap_ct_framer_impl_free_fake.call_count, 1);
    EXPECT_EQ(__wrap_ct_framer_impl_free_fake.arg0_val, &first_copy);

    // Deep copy was called twice, new copy is installed
    EXPECT_EQ(__wrap_ct_framer_impl_deep_copy_fake.call_count, 2);
    EXPECT_EQ(preconnection->framer_impl, &second_copy);

    ct_preconnection_free(preconnection);
}

TEST_F(PreconnectionUnitTests, setFramerNullImplFreesExisting) {
    ct_framer_impl_t dummy{};
    ct_framer_impl_t copy{};
    __wrap_ct_framer_impl_deep_copy_fake.return_val = &copy;

    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, NULL, 0, NULL, NULL);
    EXPECT_EQ(ct_preconnection_set_framer(preconnection, &dummy), 0);

    int result = ct_preconnection_set_framer(preconnection, NULL);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(__wrap_ct_framer_impl_free_fake.call_count, 1);
    EXPECT_EQ(__wrap_ct_framer_impl_free_fake.arg0_val, &copy);
    EXPECT_EQ(preconnection->framer_impl, nullptr);

    ct_preconnection_free(preconnection);
}

TEST_F(PreconnectionUnitTests, setFramerReturnsENOMEMOnCopyFailure) {
    ct_framer_impl_t dummy{};
    __wrap_ct_framer_impl_deep_copy_fake.return_val = NULL;

    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, NULL, 0, NULL, NULL);
    int result = ct_preconnection_set_framer(preconnection, &dummy);

    EXPECT_EQ(result, -ENOMEM);
    EXPECT_EQ(preconnection->framer_impl, nullptr);
    EXPECT_EQ(__wrap_ct_framer_impl_free_fake.call_count, 0);

    ct_preconnection_free(preconnection);
}
