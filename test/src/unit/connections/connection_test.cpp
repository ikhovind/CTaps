#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include <connection/socket_manager/socket_manager.h>
#include <connection/connection.h>
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
    ct_socket_manager_t socket_manager;
    ct_protocol_impl_t protocol_impl;
    socket_manager.protocol_impl = &protocol_impl;
    protocol_impl.send = fake_protocol_send;

    ct_connection_t connection;
    ct_connection_build_with_new_connection_group(&connection);
    ct_connection_set_can_send(&connection, true);

    connection.socket_manager = &socket_manager;

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
    ct_connection_t* connection = ct_connection_create_empty_with_uuid();
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
