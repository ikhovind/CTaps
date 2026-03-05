#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

class QuicMigrationTest : public CTapsGenericFixture {
protected:
  void SetUp() override {
    CTapsGenericFixture::SetUp();
  }
};


// Test fixture helper - sets up two loopback aliases so the stack has
// a genuine second path to discover and potentially migrate to.
// Run: ip addr add 127.0.0.2/8 dev lo   (in test setup)
//      ip addr del 127.0.0.2/8 dev lo   (to simulate path failure)

TEST_F(QuicMigrationTest, MigratesAfterPrimaryPathFails) {
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
        .ready = send_message_and_receive_blocking_primary_path,
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

    ct_remote_endpoint_free_strings(&remotes[0]);
    ct_remote_endpoint_free_strings(&remotes[1]);
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
    ct_security_parameters_free(security_parameters);
}
