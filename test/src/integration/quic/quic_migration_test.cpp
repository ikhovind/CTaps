#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fixtures/integration_fixture.h"
extern "C" {
#include "fff.h"
#include "ctaps.h"
#include <logging/log.h>
}

class QuicMigrationTest : public CTapsGenericFixture {
protected:
  void SetUp() override {
    CTapsGenericFixture::SetUp();
  }
};


// We bring down the remote address 127.0.0.1 and check that we migrate
// to 127.0.0.2
TEST_F(QuicMigrationTest, migratesAfterPrimaryRemoteFails) {
    struct IptablesGuard guard = IptablesGuard();
    struct IpAddressGuard ipAddr = IpAddressGuard();
    ct_remote_endpoint_t* remotes[2] = {0};
    remotes[0] = ct_remote_endpoint_new();
    remotes[1] = ct_remote_endpoint_new();
    ct_remote_endpoint_with_ipv4(remotes[0], inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remotes[0], QUIC_PING_PORT);
    ct_remote_endpoint_with_ipv4(remotes[1], inet_addr("127.0.0.2"));
    ct_remote_endpoint_with_port(remotes[1], QUIC_PING_PORT);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    ASSERT_NE(transport_properties, nullptr);

    ct_transport_properties_set_reliability(transport_properties, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
    ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC
    ct_transport_properties_set_advertises_alt_address(transport_properties, true);

    ct_security_parameters_t* security_parameters = ct_security_parameters_new();
    ASSERT_NE(security_parameters, nullptr);
    const char* alpn_strings = "simple-ping";
    ct_security_parameters_add_alpn(security_parameters, alpn_strings);
    ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
    ct_local_endpoint_with_ipv4(local_endpoint, inet_addr("127.0.0.1"));

    ct_preconnection_t* preconnection = ct_preconnection_new(
        &local_endpoint,
        1,
        remotes,
        2,
        transport_properties,
        security_parameters
    );

    ct_connection_callbacks_t callbacks = {
        .establishment_error = on_establishment_error,
        .ready = send_message_and_receive_blocking_primary_path_for_remote,
        .per_connection_context = &test_context,
    };

    ct_preconnection_initiate(preconnection, &callbacks);
    ct_start_event_loop();

    ct_connection_t* connection = test_context.client_connections[0];
    ASSERT_TRUE(ct_connection_is_closed(connection));


    ASSERT_EQ(per_connection_messages[connection].size(), 2);
    ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
    ASSERT_STREQ(per_connection_messages[connection][1]->content, "Pong: ping");


    const struct sockaddr_in* to_addr = (const struct sockaddr_in*)&ct_connection_get_active_remote_endpoint(connection)->resolved_address;
    EXPECT_EQ(to_addr->sin_addr.s_addr, inet_addr("127.0.0.2"));

    const struct sockaddr_in* from_addr = (const struct sockaddr_in*)&ct_connection_get_active_local_endpoint(connection)->resolved_address;
    EXPECT_EQ(from_addr->sin_addr.s_addr, inet_addr("127.0.0.1"));

    ASSERT_EQ(to_addr->sin_port, htons(QUIC_PING_PORT));

    ct_remote_endpoint_free(remotes[0]);
    ct_remote_endpoint_free(remotes[1]);
    ct_local_endpoint_free(local_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
    ct_security_parameters_free(security_parameters);
}

// We bring down the local address 127.0.0.1 and check that we migrate
TEST_F(QuicMigrationTest, migratesAfterPrimaryLocalFails) {
    struct IptablesGuard guard = IptablesGuard();
    struct IpAddressGuard ipAddr = IpAddressGuard();
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));

    ct_local_endpoint_t* local_endpoints[2] = {0};
    local_endpoints[0] = ct_local_endpoint_new();
    local_endpoints[1] = ct_local_endpoint_new();

    ct_local_endpoint_with_ipv4(local_endpoints[0], inet_addr("127.0.0.1"));
    ct_local_endpoint_with_ipv4(local_endpoints[1], inet_addr("127.0.0.2"));

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    ASSERT_NE(transport_properties, nullptr);

    ct_transport_properties_set_reliability(transport_properties, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
    ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC
    ct_transport_properties_set_advertises_alt_address(transport_properties, true);

    ct_security_parameters_t* security_parameters = ct_security_parameters_new();
    ASSERT_NE(security_parameters, nullptr);
    const char* alpn_strings = "simple-ping";
    ct_security_parameters_add_alpn(security_parameters, alpn_strings);
    ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_preconnection_t* preconnection = ct_preconnection_new(
        local_endpoints,
        2,
        &remote_endpoint,
        1,
        transport_properties,
        security_parameters
    );

    ct_connection_callbacks_t callbacks = {
        .establishment_error = on_establishment_error,
        .ready = send_message_and_receive_blocking_primary_path_for_local,
        .per_connection_context = &test_context,
    };

    ct_preconnection_initiate(preconnection, &callbacks);
    ct_start_event_loop();

    ct_connection_t* connection = test_context.client_connections[0];
    ASSERT_TRUE(ct_connection_is_closed(connection));
    ASSERT_EQ(per_connection_messages[connection].size(), 2);
    ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
    ASSERT_STREQ(per_connection_messages[connection][1]->content, "Pong: ping");

    ASSERT_EQ(test_context.local_sockaddr.size(), 2);

    EXPECT_EQ(((struct sockaddr_in*)&test_context.local_sockaddr[0])->sin_addr.s_addr, inet_addr("127.0.0.1"))
        << "First message should be sent from 127.0.0.1 before migration";
    EXPECT_EQ(((struct sockaddr_in*)&test_context.local_sockaddr[1])->sin_addr.s_addr, inet_addr("127.0.0.2"))
        << "Second message should be sent from 127.0.0.2 after migration";

    ct_local_endpoint_free(local_endpoints[0]);
    ct_local_endpoint_free(local_endpoints[1]);
    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
    ct_security_parameters_free(security_parameters);
}
