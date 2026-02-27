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

extern "C" void ct_socket_manager_message_sent_cb(ct_connection_t* connection, ct_message_context_t* message_context);
extern "C" void ct_socket_manager_message_send_error_cb(ct_connection_t* connection, ct_message_context_t* message_context, int reason);

extern "C" {
DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(fake_on_sent, ct_connection_t*, ct_message_context_t*);
FAKE_VOID_FUNC(fake_send_error, ct_connection_t*, ct_message_context_t*, int);
FAKE_VOID_FUNC(__wrap_ct_message_free, ct_message_t*);
FAKE_VOID_FUNC(__wrap_ct_message_context_free, ct_message_context_t*);
FAKE_VALUE_FUNC(int, fake_set_connection_priority, ct_connection_t*, uint8_t)
}

class SocketManagerUnitTests : public ::testing::Test {
protected:
    void SetUp() override {
        RESET_FAKE(fake_on_sent);
        RESET_FAKE(fake_send_error);
        RESET_FAKE(__wrap_ct_message_free);
        RESET_FAKE(__wrap_ct_message_context_free);
        RESET_FAKE(fake_set_connection_priority);
        FFF_RESET_HISTORY();

        fake_set_connection_priority_fake.return_val = 0;

        dummy_protocol_impl.name = "dummy_protocol";
        dummy_protocol_impl.set_connection_priority = fake_set_connection_priority;

        dummy_socket_manager.protocol_impl = &dummy_protocol_impl;

        dummy_connection.socket_manager = &dummy_socket_manager;

        dummy_connection_without_callbacks = dummy_connection;

        dummy_connection.connection_callbacks.sent = fake_on_sent;
        dummy_connection.connection_callbacks.send_error = fake_send_error;

        dummy_socket_manager_without_support.protocol_impl = &dummy_protocol_without_support;
    }

    
    void TearDown() override {
    }

    ct_socket_manager_t dummy_socket_manager = {0};
    ct_socket_manager_t dummy_socket_manager_without_support = {0};

    ct_protocol_impl_t dummy_protocol_impl = {0};
    ct_protocol_impl_t dummy_protocol_without_support = {0};

    ct_connection_t dummy_connection = {0};
    ct_connection_t dummy_connection_without_callbacks = {0};

    ct_message_t dummy_message = {0};
    ct_message_context_t dummy_message_context = {0};
};

TEST_F(SocketManagerUnitTests, invokesConnectionCallbackAndFreesContextOnSent) {
    ct_socket_manager_message_sent_cb(&dummy_connection, &dummy_message_context);

    ASSERT_EQ(fake_on_sent_fake.call_count, 1);
    ASSERT_EQ(fake_on_sent_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(fake_on_sent_fake.arg1_val, &dummy_message_context);

    ASSERT_EQ(__wrap_ct_message_context_free_fake.call_count, 1);
    ASSERT_EQ(__wrap_ct_message_context_free_fake.arg0_val, &dummy_message_context);
}

TEST_F(SocketManagerUnitTests, doesNotInvokesConnectionCallbackWhenNotSetButFreesContextOnSent) {
    ct_socket_manager_message_sent_cb(&dummy_connection_without_callbacks, &dummy_message_context);

    ASSERT_EQ(fake_on_sent_fake.call_count, 0);

    ASSERT_EQ(__wrap_ct_message_context_free_fake.call_count, 1);
    ASSERT_EQ(__wrap_ct_message_context_free_fake.arg0_val, &dummy_message_context);
}

TEST_F(SocketManagerUnitTests, invokesSendErrorOnSendError) {
    ct_socket_manager_message_send_error_cb(&dummy_connection, &dummy_message_context, -100);

    ASSERT_EQ(fake_on_sent_fake.call_count, 0);

    ASSERT_EQ(fake_send_error_fake.call_count, 1);
    ASSERT_EQ(fake_send_error_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(fake_send_error_fake.arg1_val, &dummy_message_context);
    ASSERT_EQ(fake_send_error_fake.arg2_val, -100);


    ASSERT_EQ(__wrap_ct_message_context_free_fake.call_count, 1);
    ASSERT_EQ(__wrap_ct_message_context_free_fake.arg0_val, &dummy_message_context);
}

TEST_F(SocketManagerUnitTests, doesNotInvokesSendErrorWhenNotSetButFreesContext) {
    ct_socket_manager_message_send_error_cb(&dummy_connection_without_callbacks, &dummy_message_context, -100);

    ASSERT_EQ(fake_send_error_fake.call_count, 0);


    ASSERT_EQ(__wrap_ct_message_context_free_fake.call_count, 1);
    ASSERT_EQ(__wrap_ct_message_context_free_fake.arg0_val, &dummy_message_context);
}

TEST_F(SocketManagerUnitTests, noErrorWhenProtocolAcceptsPriorityChange) {
    int rc = ct_socket_manager_notify_protocol_of_priority_change(&dummy_connection, 99);

    ASSERT_EQ(rc, 0);

    ASSERT_EQ(fake_set_connection_priority_fake.call_count, 1);
    ASSERT_EQ(fake_set_connection_priority_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(fake_set_connection_priority_fake.arg1_val, 99);
}

TEST_F(SocketManagerUnitTests, enotsupWhenProtocolDoesNotSupportPriorityChange) {
    dummy_connection.socket_manager = &dummy_socket_manager_without_support;
    int rc = ct_socket_manager_notify_protocol_of_priority_change(&dummy_connection, 99);

    ASSERT_EQ(rc, -ENOTSUP);

    ASSERT_EQ(fake_set_connection_priority_fake.call_count, 0);
}

TEST_F(SocketManagerUnitTests, errorWhenProtocolFailsOnPriorityChange) {
    fake_set_connection_priority_fake.return_val = -EIO;
    int rc = ct_socket_manager_notify_protocol_of_priority_change(&dummy_connection, 99);

    ASSERT_EQ(rc, -EIO);

    ASSERT_EQ(fake_set_connection_priority_fake.call_count, 1);
    ASSERT_EQ(fake_set_connection_priority_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(fake_set_connection_priority_fake.arg1_val, 99);
}

TEST_F(SocketManagerUnitTests, errorOnNullConnection) {
    int rc = ct_socket_manager_notify_protocol_of_priority_change(NULL, 99);

    ASSERT_EQ(rc, -EINVAL);
}

TEST_F(SocketManagerUnitTests, errorOnNullSocketManager) {
    dummy_connection.socket_manager = NULL;

    int rc = ct_socket_manager_notify_protocol_of_priority_change(&dummy_connection, 99);

    ASSERT_EQ(rc, -EINVAL);
}
