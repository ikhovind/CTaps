#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "fff.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include <connection/socket_manager/socket_manager.h>
#include <message/message.h>
#include <connection/connection.h>
#include "logging/log.h"
}

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(ct_message_t*, fake_ct_message_deep_copy, const ct_message_t*);
FAKE_VALUE_FUNC(int, fake_protocol_send, ct_connection_t*, ct_message_t*, ct_message_context_t*);
FAKE_VALUE_FUNC(ct_message_context_t*, fake_ct_message_context_new_from_connection, const ct_connection_t*);
FAKE_VALUE_FUNC(ct_message_context_t*, fake_ct_message_context_deep_copy, const ct_message_context_t*);
FAKE_VALUE_FUNC(int, fake_encode_message, ct_connection_t*, ct_message_t*, ct_message_context_t*, ct_framer_done_encoding_callback);
FAKE_VOID_FUNC(fake_ct_message_free, ct_message_t*);
FAKE_VOID_FUNC(fake_ct_message_context_free, ct_message_context_t*);

extern "C" {
    ct_message_t* __wrap_ct_message_deep_copy(const ct_message_t* message) {
        return fake_ct_message_deep_copy(message);
    }
    ct_message_context_t* __wrap_ct_message_context_new_from_connection(const ct_connection_t* connection) {
        return fake_ct_message_context_new_from_connection(connection);
    }

    ct_message_context_t* __wrap_ct_message_context_deep_copy(const ct_message_context_t* message_context) {
        return fake_ct_message_context_deep_copy(message_context);
    }

    void __wrap_ct_message_free(ct_message_t* message) {
        fake_ct_message_free(message);
    }
    void __wrap_ct_message_context_free(ct_message_context_t* message_context) {
        fake_ct_message_context_free(message_context);
    }
}

class ConnectionUnitTests : public ::testing::Test {
protected:
    void SetUp() override {
        RESET_FAKE(fake_ct_message_deep_copy);
        RESET_FAKE(fake_protocol_send);
        RESET_FAKE(fake_ct_message_context_new_from_connection);
        RESET_FAKE(fake_ct_message_context_deep_copy);
        RESET_FAKE(fake_encode_message);
        RESET_FAKE(fake_ct_message_free);
        RESET_FAKE(fake_ct_message_context_free);
        FFF_RESET_HISTORY();

        dummy_connection.transport_properties = &dummy_transport_properties;

        dummy_connection.socket_manager = &dummy_socket_manager;

        dummy_socket_manager.protocol_impl = &dummy_protocol_impl;
        fake_protocol_send_fake.return_val = 0;
        fake_ct_message_deep_copy_fake.return_val = &dummy_message;
        fake_ct_message_context_new_from_connection_fake.return_val = &dummy_message_context;
        fake_ct_message_context_deep_copy_fake.return_val = &dummy_message_context;

        dummy_protocol_impl.send = fake_protocol_send;

        dummy_connection_with_framer = dummy_connection;
        dummy_connection_with_framer.framer_impl = &dummy_framer_impl;
        dummy_framer_impl.encode_message = fake_encode_message;


        ct_connection_set_can_send(&dummy_connection, true);
    }

    
    void TearDown() override {
    }

    ct_socket_manager_t dummy_socket_manager;
    ct_connection_t dummy_connection;
    ct_connection_t dummy_connection_with_framer;
    ct_framer_impl_t dummy_framer_impl;
    ct_transport_properties_t dummy_transport_properties = {0};
    ct_message_t dummy_message = {0};
    ct_message_context_t dummy_message_context = {0};
    ct_protocol_impl_t dummy_protocol_impl = {0};
};

TEST_F(ConnectionUnitTests, sendMessageFullCreatesMessageContextOnNull) {
    int rc = ct_send_message_full(&dummy_connection, &dummy_message, NULL);
    ASSERT_EQ(rc, 0);

    ASSERT_EQ(fake_ct_message_deep_copy_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_deep_copy_fake.arg0_val, &dummy_message);

    ASSERT_EQ(fake_ct_message_context_new_from_connection_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_context_new_from_connection_fake.arg0_val, &dummy_connection);

    ASSERT_EQ(fake_protocol_send_fake.call_count, 1);
    ASSERT_EQ(fake_protocol_send_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(fake_protocol_send_fake.arg1_val, &dummy_message);
    ASSERT_EQ(fake_protocol_send_fake.arg2_val, &dummy_message_context);
}

TEST_F(ConnectionUnitTests, sendMessageFullDoesNotCreateMessageContextWhenNotNull) {
    int rc = ct_send_message_full(&dummy_connection, &dummy_message, &dummy_message_context);
    ASSERT_EQ(rc, 0);

    ASSERT_EQ(fake_ct_message_deep_copy_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_deep_copy_fake.arg0_val, &dummy_message);

    ASSERT_EQ(fake_ct_message_context_new_from_connection_fake.call_count, 0);

    ASSERT_EQ(fake_protocol_send_fake.call_count, 1);
    ASSERT_EQ(fake_protocol_send_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(fake_protocol_send_fake.arg1_val, &dummy_message);
    ASSERT_EQ(fake_protocol_send_fake.arg2_val, &dummy_message_context);
}

TEST_F(ConnectionUnitTests, sendMessageFullDoesNotCreateMessageContextAndHandsOverToFramer) {
    int rc = ct_send_message_full(&dummy_connection_with_framer, &dummy_message, NULL);
    ASSERT_EQ(rc, 0);

    ASSERT_EQ(fake_ct_message_deep_copy_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_deep_copy_fake.arg0_val, &dummy_message);

    ASSERT_EQ(fake_ct_message_context_new_from_connection_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_context_new_from_connection_fake.arg0_val, &dummy_connection_with_framer);

    ASSERT_EQ(fake_protocol_send_fake.call_count, 0);

    ASSERT_EQ(fake_encode_message_fake.call_count, 1);
    ASSERT_EQ(fake_encode_message_fake.arg0_val, &dummy_connection_with_framer);
    ASSERT_EQ(fake_encode_message_fake.arg1_val, &dummy_message);
    ASSERT_EQ(fake_encode_message_fake.arg2_val, &dummy_message_context);
}

TEST_F(ConnectionUnitTests, sendMessageFreesMessageContextOnFramerFailure) {
    fake_encode_message_fake.return_val = -101;

    int rc = ct_send_message_full(&dummy_connection_with_framer, &dummy_message, NULL);
    ASSERT_EQ(rc, -101);

    ASSERT_EQ(fake_ct_message_deep_copy_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_deep_copy_fake.arg0_val, &dummy_message);

    ASSERT_EQ(fake_ct_message_context_new_from_connection_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_context_new_from_connection_fake.arg0_val, &dummy_connection_with_framer);

    ASSERT_EQ(fake_protocol_send_fake.call_count, 0);

    ASSERT_EQ(fake_encode_message_fake.call_count, 1);
    ASSERT_EQ(fake_encode_message_fake.arg0_val, &dummy_connection_with_framer);
    ASSERT_EQ(fake_encode_message_fake.arg1_val, &dummy_message);
    ASSERT_EQ(fake_encode_message_fake.arg2_val, &dummy_message_context);

    ASSERT_EQ(fake_ct_message_context_free_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_context_free_fake.arg0_val, &dummy_message_context);
    ASSERT_EQ(fake_ct_message_free_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_free_fake.arg0_val, &dummy_message);
}

TEST_F(ConnectionUnitTests, sendMessageFreesMessageContextOnProtocolFailure) {
    fake_protocol_send_fake.return_val = -101;

    int rc = ct_send_message_full(&dummy_connection, &dummy_message, NULL);
    ASSERT_EQ(rc, -101);

    ASSERT_EQ(fake_ct_message_deep_copy_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_deep_copy_fake.arg0_val, &dummy_message);

    ASSERT_EQ(fake_ct_message_context_new_from_connection_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_context_new_from_connection_fake.arg0_val, &dummy_connection);

    ASSERT_EQ(fake_encode_message_fake.call_count, 0);

    ASSERT_EQ(fake_protocol_send_fake.call_count, 1);
    ASSERT_EQ(fake_protocol_send_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(fake_protocol_send_fake.arg1_val, &dummy_message);
    ASSERT_EQ(fake_protocol_send_fake.arg2_val, &dummy_message_context);
}

TEST_F(ConnectionUnitTests, connectionCanSendReturnsCorrectRes) {
    ct_connection_t connection;
    memset(&connection, 0, sizeof(ct_connection_t));
    connection.transport_properties = ct_transport_properties_new();

    ASSERT_FALSE(ct_connection_can_send(&connection));

    connection.transport_properties->connection_properties.list[CAN_SEND].value.bool_val = true;
    ASSERT_TRUE(ct_connection_can_send(&connection));
    ct_connection_free_content(&connection);
}

TEST_F(ConnectionUnitTests, connectionCanReceiveReturnsCorrectRes) {
    ct_connection_t connection;
    memset(&connection, 0, sizeof(ct_connection_t));
    connection.transport_properties = ct_transport_properties_new();

    ASSERT_FALSE(ct_connection_can_receive(&connection));

    connection.transport_properties->connection_properties.list[CAN_RECEIVE].value.bool_val = true;
    ASSERT_TRUE(ct_connection_can_receive(&connection));
    ct_connection_free_content(&connection);
}

TEST_F(ConnectionUnitTests, SendMessageFullFailsWhenCanSendIsFalse) {
    ct_connection_t* connection = ct_connection_create_empty_with_uuid();

    // Set canSend to false
    ct_connection_set_can_send(connection, false);

    // Try to send a message
    ct_message_t* message = ct_message_new_with_content("test", 5);
    ASSERT_NE(message, nullptr);

    int rc = ct_send_message_full(connection, message, NULL);

    // Should fail with -EPIPE
    EXPECT_EQ(rc, -EPIPE);

    // Cleanup
    ct_message_free(message);
    ct_connection_free(connection);
}

TEST_F(ConnectionUnitTests, SendMessageWithFinalSetsCanSendToFalse) {
    RESET_FAKE(fake_protocol_send);
    fake_protocol_send_fake.return_val = 0;
    ct_socket_manager_t socket_manager = {0};
    ct_protocol_impl_t protocol_impl;
    socket_manager.protocol_impl = &protocol_impl;
    protocol_impl.send = fake_protocol_send;

    ct_connection_t* connection = ct_connection_create_empty_with_uuid();
    ct_connection_set_can_send(connection, true);

    connection->socket_manager = &socket_manager;

    // Create message with FINAL property
    ct_message_t* message = ct_message_new_with_content("final message", 14);
    ASSERT_NE(message, nullptr);

    ct_message_context_t* context = ct_message_context_new();
    ASSERT_NE(context, nullptr);
    ct_message_context_set_final(context, true);

    // Send message
    int rc = ct_send_message_full(connection, message, context);

    // Verify send succeeded
    EXPECT_EQ(rc, 0);

    // Verify protocol send was called exactly once
    EXPECT_EQ(fake_protocol_send_fake.call_count, 1);

    // Verify canSend is now false
    EXPECT_FALSE(ct_connection_can_send(connection));

    // Cleanup
    ct_message_free(message);
    ct_message_context_free(context);
    ct_connection_free_content(connection);
}

TEST_F(ConnectionUnitTests, connectionPropertyGetterGetsConnectionProperty) {
    ct_connection_t* connection = ct_connection_create_empty_with_uuid();
    ct_transport_properties_t* transport_properties = ct_transport_properties_new();

    transport_properties->connection_properties.list[CAN_SEND].value.bool_val = true;

    connection->transport_properties = transport_properties;

    const ct_connection_properties_t* gotten_props = ct_connection_get_connection_properties(connection);

    ASSERT_NE(gotten_props, nullptr);
    ASSERT_EQ(gotten_props->list[CAN_SEND].value.bool_val, true);
    ASSERT_EQ((void*)gotten_props, (void*)&connection->transport_properties->connection_properties);
    ct_connection_free(connection);
}

TEST_F(ConnectionUnitTests, connectionPropertyGetterHandlesNullParam) {
    const ct_connection_properties_t* gotten_props = ct_connection_get_connection_properties(nullptr);

    ASSERT_EQ((void*)gotten_props, nullptr);
}
 
