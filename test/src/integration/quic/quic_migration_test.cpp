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
    ct_remote_endpoint_t remotes[2] = {0};
    ct_remote_endpoint_with_ipv4(&remotes[0], inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remotes[0], QUIC_PING_PORT);
    ct_remote_endpoint_with_ipv4(&remotes[1], inet_addr("127.0.0.2"));
    ct_remote_endpoint_with_port(&remotes[1], QUIC_PING_PORT);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    ASSERT_NE(transport_properties, nullptr);

    ct_transport_properties_set_reliability(transport_properties, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
    ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC

    ct_security_parameters_t* security_parameters = ct_security_parameters_new();
    ASSERT_NE(security_parameters, nullptr);
    const char* alpn_strings = "simple-ping";
    ct_security_parameters_add_alpn(security_parameters, alpn_strings);
    ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_preconnection_t* preconnection = ct_preconnection_new(
        NULL,
        0,
        remotes,
        2,
        transport_properties,
        security_parameters
    );

    ct_connection_callbacks_t callbacks = {
        .establishment_error = on_establishment_error,
        .ready = send_message_and_receive_blocking_primary_path_for_remote,
        .user_connection_context = &test_context,
    };

    ct_preconnection_initiate(preconnection, callbacks);
    ct_start_event_loop();

    ct_connection_t* connection = test_context.client_connections[0];
    ASSERT_TRUE(ct_connection_is_closed(connection));
    ASSERT_EQ(per_connection_messages[connection].size(), 2);
    ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
    ASSERT_STREQ(per_connection_messages[connection][1]->content, "Pong: ping");


    char to_ip[INET6_ADDRSTRLEN];
    uint16_t dest_port;
    const struct sockaddr_in* to_addr = (const struct sockaddr_in*)&ct_connection_get_active_remote_endpoint(connection)->data.resolved_address;
    inet_ntop(AF_INET, &to_addr->sin_addr, to_ip, sizeof(to_ip));
    dest_port = ntohs(to_addr->sin_port);
    EXPECT_STREQ(to_ip, "127.0.0.2");
    ASSERT_EQ(dest_port, QUIC_PING_PORT);

    ct_remote_endpoint_free_content(&remotes[0]);
    ct_remote_endpoint_free_content(&remotes[1]);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
    ct_security_parameters_free(security_parameters);
}

// We bring down the local address 127.0.0.1 and check that we migrate
TEST_F(QuicMigrationTest, migratesAfterPrimaryLocalFails) {
    struct IptablesGuard guard = IptablesGuard();
    struct IpAddressGuard ipAddr = IpAddressGuard();
    struct OnlyLoopBackTo4433 onlyLoopBackTo4433 = OnlyLoopBackTo4433();
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ct_remote_endpoint_with_port(remote_endpoint, QUIC_PING_PORT);
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    ASSERT_NE(transport_properties, nullptr);

    ct_transport_properties_set_reliability(transport_properties, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(transport_properties, REQUIRE);
    ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC

    ct_security_parameters_t* security_parameters = ct_security_parameters_new();
    ASSERT_NE(security_parameters, nullptr);
    const char* alpn_strings = "simple-ping";
    ct_security_parameters_add_alpn(security_parameters, alpn_strings);
    ct_security_parameters_add_client_certificate(security_parameters, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");

    ct_preconnection_t* preconnection = ct_preconnection_new(
        NULL,
        0,
        remote_endpoint,
        1,
        transport_properties,
        security_parameters
    );

    ct_connection_callbacks_t callbacks = {
        .establishment_error = on_establishment_error,
        .ready = send_message_and_receive_blocking_primary_path_for_local,
        .sent = capture_local_on_sent,
        .user_connection_context = &test_context,
    };

    ct_preconnection_initiate(preconnection, callbacks);
    ct_start_event_loop();

    ct_connection_t* connection = test_context.client_connections[0];
    ASSERT_TRUE(ct_connection_is_closed(connection));
    ASSERT_EQ(per_connection_messages[connection].size(), 2);
    ASSERT_STREQ(per_connection_messages[connection][0]->content, "Pong: ping");
    ASSERT_STREQ(per_connection_messages[connection][1]->content, "Pong: ping");

    ASSERT_EQ(test_context.local_endpoints.size(), 2);
    ASSERT_NE(memcmp(&test_context.local_endpoints[0]->data.resolved_address, &test_context.local_endpoints[1]->data.resolved_address, sizeof(struct sockaddr_in)), 0) << "Local endpoints should have different resolved addresses after migration";

    ct_remote_endpoint_free(remote_endpoint);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
    ct_security_parameters_free(security_parameters);
    ASSERT_FALSE(true);
}
