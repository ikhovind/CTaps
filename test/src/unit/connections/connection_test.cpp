#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include <connection/socket_manager/socket_manager.h>
#include <connection/connection.h>
}

TEST(ConnectionUnitTests, TakesDeepCopyOfTransportProperties) {
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ASSERT_NE(remote_endpoint, nullptr);

    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, 5005);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
  ASSERT_NE(transport_properties, nullptr);

    // Allocated with ct_transport_properties_new()
    ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_connection_t* connection = create_empty_connection_with_uuid();
    ct_socket_manager_t socket_manager = {
        .internal_socket_manager_state = nullptr,
        .protocol_impl = nullptr
    };

    ct_listener_t mock_listener = {
        .transport_properties = *transport_properties,
        .local_endpoint = 0,
        .socket_manager = &socket_manager,
    };

    ct_connection_build_multiplexed(connection, &mock_listener,  remote_endpoint);

    ASSERT_EQ(connection->transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);
    ASSERT_EQ(mock_listener.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);

    ct_tp_set_sel_prop_preference(&connection->transport_properties, RELIABILITY, REQUIRE);

    ASSERT_EQ(connection->transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, REQUIRE);
    ASSERT_EQ(mock_listener.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);

    // // Cleanup
    ct_connection_free(connection);
    ct_transport_properties_free(transport_properties);
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

TEST(ConnectionUnitTests, connectionCanSendReturnsCorrectRes) {
    ct_connection_t connection;
    memset(&connection, 0, sizeof(ct_connection_t));

    ASSERT_FALSE(ct_connection_can_send(&connection));

    connection.transport_properties.connection_properties.list[CAN_SEND].value.bool_val = true;
    ASSERT_TRUE(ct_connection_can_send(&connection));
}

TEST(ConnectionUnitTests, connectionCanReceiveReturnsCorrectRes) {
    ct_connection_t connection;
    memset(&connection, 0, sizeof(ct_connection_t));

    ASSERT_FALSE(ct_connection_can_receive(&connection));

    connection.transport_properties.connection_properties.list[CAN_RECEIVE].value.bool_val = true;
    ASSERT_TRUE(ct_connection_can_receive(&connection));
}

TEST(ConnectionUnitTests, SendMessageFullFailsWhenCanSendIsFalse) {
    ct_connection_t connection;
    ct_connection_build_with_new_connection_group(&connection);

    // Set canSend to false
    ct_connection_set_can_send(&connection, false);

    // Try to send a message
    ct_message_t* message = ct_message_new_with_content("test", 5);
    ASSERT_NE(message, nullptr);

    int rc = ct_send_message_full(&connection, message, NULL);

    // Should fail with -EPIPE
    EXPECT_EQ(rc, -EPIPE);

    // Cleanup
    ct_message_free(message);
    ct_connection_free_content(&connection);
}

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, fake_protocol_send, ct_connection_t*, ct_message_t*, ct_message_context_t*);

TEST(ConnectionUnitTests, SendMessageWithFinalSetsCanSendToFalse) {
    RESET_FAKE(fake_protocol_send);
    fake_protocol_send_fake.return_val = 0;

    ct_connection_t connection;
    ct_connection_build_with_new_connection_group(&connection);
    ct_connection_set_can_send(&connection, true);

    // Set up fake protocol
    connection.protocol.send = fake_protocol_send;

    // Create message with FINAL property
    ct_message_t* message = ct_message_new_with_content("final message", 14);
    ASSERT_NE(message, nullptr);

    ct_message_context_t* context = ct_message_context_new();
    ASSERT_NE(context, nullptr);
    ct_message_context_set_final(context, true);

    // Send message
    int rc = ct_send_message_full(&connection, message, context);

    // Verify send succeeded
    EXPECT_EQ(rc, 0);

    // Verify protocol send was called exactly once
    EXPECT_EQ(fake_protocol_send_fake.call_count, 1);

    // Verify canSend is now false
    EXPECT_FALSE(ct_connection_can_send(&connection));

    // Cleanup
    ct_message_free(message);
    ct_message_context_free(context);
    ct_connection_free_content(&connection);
}

TEST(ConnectionUnitTests, connectionPropertyGetterGetsConnectionProperty) {
    ct_connection_t* connection = create_empty_connection_with_uuid();
    ct_transport_properties_t* transport_properties = ct_transport_properties_new();

    transport_properties->connection_properties.list[CAN_SEND].value.bool_val = true;

    connection->transport_properties = *transport_properties;

    const ct_connection_properties_t* gotten_props = ct_connection_get_connection_properties(connection);

    ASSERT_NE(gotten_props, nullptr);
    ASSERT_EQ(gotten_props->list[CAN_SEND].value.bool_val, true);
    ASSERT_EQ((void*)gotten_props, (void*)&connection->transport_properties.connection_properties);
}

TEST(ConnectionUnitTests, connectionPropertyGetterHandlesNullParam) {
    const ct_connection_properties_t* gotten_props = ct_connection_get_connection_properties(nullptr);

    ASSERT_EQ((void*)gotten_props, nullptr);
}
