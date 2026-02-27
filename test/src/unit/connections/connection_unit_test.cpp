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
FAKE_VALUE_FUNC(int, fake_socket_manager_insert_connection, ct_socket_manager_t*, const ct_remote_endpoint_t*, ct_connection_t*);
FAKE_VALUE_FUNC(ct_socket_manager_t*, fake_ct_socket_manager_ref, ct_socket_manager_t*);

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

    int __wrap_socket_manager_insert_connection(ct_socket_manager_t* socket_manager, const ct_remote_endpoint_t* remote, ct_connection_t* connection) {
        fake_socket_manager_insert_connection(socket_manager, remote, connection);
        return 0;
    }

    ct_socket_manager_t* __wrap_ct_socket_manager_ref(ct_socket_manager_t* socket_manager) {
        return fake_ct_socket_manager_ref(socket_manager);
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

        fake_ct_socket_manager_ref_fake.return_val = &dummy_socket_manager;

        dummy_connection_group = ct_connection_group_new();
        int rc = ct_connection_group_add_connection(dummy_connection_group, &dummy_connection);
        ASSERT_EQ(rc, 0);

        dummy_connection.socket_manager = &dummy_socket_manager;
        dummy_connection.local_endpoint = &dummy_local_endpoint;
        dummy_connection.remote_endpoint = &dummy_remote_endpoint;
        dummy_connection.properties.priority = CT_CONNECTION_DEFAULT_PRIORITY;

        dummy_socket_manager.protocol_impl = &dummy_protocol_impl;
        fake_protocol_send_fake.return_val = 0;
        fake_ct_message_deep_copy_fake.return_val = &dummy_message;
        fake_ct_message_context_new_from_connection_fake.return_val = &dummy_message_context;
        fake_ct_message_context_deep_copy_fake.return_val = &dummy_message_context;

        dummy_protocol_impl.send = fake_protocol_send;

        dummy_connection_with_framer = dummy_connection;
        dummy_connection_with_framer.connection_group = 0; 

        dummy_connection_with_framer.framer_impl = &dummy_framer_impl;
        dummy_framer_impl.encode_message = fake_encode_message;

        ct_connection_set_can_send(&dummy_connection, true);
        ct_connection_set_can_send(&dummy_connection_with_framer, true);
    }

    
    void TearDown() override {
        log_info("Tearing down test, freeing clone and connection group");
        if (clone) {
            // Avoid freeing stack-allocated dummy data
            clone->socket_manager = nullptr;
            clone->local_endpoint = nullptr;
            clone->remote_endpoint = nullptr;
            clone->connection_group = nullptr;
            ct_connection_free(clone);
        }
        ct_connection_group_free(dummy_connection_group);
    }

    ct_local_endpoint_t dummy_local_endpoint = {0};
    ct_remote_endpoint_t dummy_remote_endpoint = {0};
    ct_socket_manager_t dummy_socket_manager;
    ct_connection_t dummy_connection = {0};
    ct_connection_t dummy_connection_with_framer;
    ct_framer_impl_t dummy_framer_impl;
    ct_connection_group_t* dummy_connection_group;
    ct_transport_properties_t dummy_transport_properties = {0};
    ct_message_t dummy_message = {0};
    ct_message_context_t dummy_message_context = {0};
    ct_protocol_impl_t dummy_protocol_impl = {0};
    ct_connection_t* clone = nullptr;
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
    connection.properties.can_send = false;
    //
    ASSERT_FALSE(ct_connection_can_send(&connection));

    connection.properties.can_send = true;
    ASSERT_TRUE(ct_connection_can_send(&connection));
    ct_connection_free_content(&connection);
}

TEST_F(ConnectionUnitTests, connectionCanReceiveReturnsCorrectRes) {
    ct_connection_t connection;
    memset(&connection, 0, sizeof(ct_connection_t));
    connection.properties.can_receive = false;

    ASSERT_FALSE(ct_connection_can_receive(&connection));

    connection.properties.can_receive = true;
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
    dummy_connection.connection_group->transport_properties->connection_properties.list[RECV_CHECKSUM_LEN].value.uint32_val = 1234;

    const ct_connection_properties_t* gotten_props = ct_connection_get_connection_properties(&dummy_connection);

    ASSERT_NE(gotten_props, nullptr);
    ASSERT_EQ(gotten_props->list[RECV_CHECKSUM_LEN].value.uint32_val, 1234);
    ASSERT_EQ((void*)gotten_props, (void*)&dummy_connection.connection_group->transport_properties->connection_properties);
}

TEST_F(ConnectionUnitTests, connectionPropertyGetterHandlesNullParam) {
    const ct_connection_properties_t* gotten_props = ct_connection_get_connection_properties(nullptr);

    ASSERT_EQ((void*)gotten_props, nullptr);
}
 
TEST_F(ConnectionUnitTests, clonedConnectionSharesConnectionProperties) {
    clone = ct_connection_create_clone(&dummy_connection, NULL, NULL, NULL);
    ASSERT_NE(clone, nullptr);

    ct_transport_properties_set_conn_timeout(dummy_connection.connection_group->transport_properties, 1234);

    const ct_connection_properties_t* gotten_props = ct_connection_get_connection_properties(clone);

    ASSERT_NE(gotten_props, nullptr);
    ASSERT_EQ(gotten_props->list[CONN_TIMEOUT].value.uint32_val, 1234);
    ASSERT_EQ((void*)gotten_props, (void*)&clone->connection_group->transport_properties->connection_properties);
}

TEST_F(ConnectionUnitTests, cloneSocketManagerIsNullWhenNoneProvided) {
    clone = ct_connection_create_clone(&dummy_connection, nullptr, nullptr, nullptr);
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->socket_manager, nullptr);
}

TEST_F(ConnectionUnitTests, cloneSocketManagerIsSetWhenProvided) {
    int prev_ref_count = dummy_socket_manager.ref_count;
    clone = ct_connection_create_clone(&dummy_connection, &dummy_socket_manager, nullptr, nullptr);
    ASSERT_NE(clone, nullptr);

    ASSERT_EQ(fake_socket_manager_insert_connection_fake.call_count, 1);
    ASSERT_EQ(fake_socket_manager_insert_connection_fake.arg0_val, &dummy_socket_manager);
    ASSERT_EQ(fake_socket_manager_insert_connection_fake.arg1_val, clone->remote_endpoint);
    ASSERT_EQ(fake_socket_manager_insert_connection_fake.arg2_val, clone);

    ASSERT_EQ(fake_ct_socket_manager_ref_fake.call_count, 1);
    ASSERT_EQ(fake_ct_socket_manager_ref_fake.arg0_val, &dummy_socket_manager);

    ASSERT_EQ(clone->socket_manager, &dummy_socket_manager);
}

TEST_F(ConnectionUnitTests, priorityIsSetToDefaultWhenCloning) {
    // make sure that priority is not read from source connection
    ct_connection_set_priority(&dummy_connection, 1234);

    clone = ct_connection_create_clone(&dummy_connection, nullptr, nullptr, nullptr);
    ASSERT_NE(clone, nullptr);

    ASSERT_EQ(clone->properties.priority, CT_CONNECTION_DEFAULT_PRIORITY);
}

TEST_F(ConnectionUnitTests, priorityIsNotShared) {
    clone = ct_connection_create_clone(&dummy_connection, nullptr, nullptr, nullptr);
    ASSERT_NE(clone, nullptr);

    ct_connection_set_priority(clone, 50);

    ASSERT_EQ(dummy_connection.properties.priority, CT_CONNECTION_DEFAULT_PRIORITY);
    ASSERT_EQ(clone->properties.priority, 50);
}
