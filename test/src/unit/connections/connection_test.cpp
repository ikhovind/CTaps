#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include <connection/socket_manager/socket_manager.h>
#include <connection/connection.h>
}

TEST(ConnectionUnitTests, TakesDeepCopyOfTransportProperties) {
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);

    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, 5005);

    ct_transport_properties_t transport_properties;

    ct_transport_properties_build(&transport_properties);
    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_connection_t connection;
    ct_local_endpoint_t local_endpoint;
    ct_socket_manager_t socket_manager = {
        .internal_socket_manager_state = nullptr,
        .protocol_impl = nullptr
    };

    ct_listener_t mock_listener = {
        .transport_properties = transport_properties,
        .local_endpoint = local_endpoint,
        .socket_manager = &socket_manager,
    };

    ct_connection_build_multiplexed(&connection, &mock_listener,  &remote_endpoint);

    ASSERT_EQ(connection.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);
    ASSERT_EQ(mock_listener.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);

    ct_tp_set_sel_prop_preference(&connection.transport_properties, RELIABILITY, REQUIRE);

    ASSERT_EQ(connection.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, REQUIRE);
    ASSERT_EQ(mock_listener.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);

    // Cleanup
    ct_connection_free_content(&connection);
}

TEST(ConnectionUnitTests, GeneratesUniqueUUIDs) {
    ct_connection_t connection1;
    ct_connection_t connection2;

    ct_connection_build_with_new_connection_group(&connection1);
    ct_connection_build_with_new_connection_group(&connection2);

    // Verify both have UUIDs
    ASSERT_GT(strlen(connection1.uuid), 0);
    ASSERT_GT(strlen(connection2.uuid), 0);

    // Verify UUIDs are different
    ASSERT_STRNE(connection1.uuid, connection2.uuid);

    // Verify UUID format (36 characters: 8-4-4-4-12 with hyphens)
    ASSERT_EQ(strlen(connection1.uuid), 36);
    ASSERT_EQ(strlen(connection2.uuid), 36);

    // Check hyphens are in the right places
    ASSERT_EQ(connection1.uuid[8], '-');
    ASSERT_EQ(connection1.uuid[13], '-');
    ASSERT_EQ(connection1.uuid[18], '-');
    ASSERT_EQ(connection1.uuid[23], '-');

    ASSERT_EQ(connection2.uuid[8], '-');
    ASSERT_EQ(connection2.uuid[13], '-');
    ASSERT_EQ(connection2.uuid[18], '-');
    ASSERT_EQ(connection2.uuid[23], '-');

    // Verify all other characters are valid hex digits or hyphens
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue; // Skip hyphens
        }
        ASSERT_TRUE(isxdigit(connection1.uuid[i]));
        ASSERT_TRUE(isxdigit(connection2.uuid[i]));
    }

    // Cleanup
    ct_connection_free_content(&connection1);
    ct_connection_free_content(&connection2);
}
