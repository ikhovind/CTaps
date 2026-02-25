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

DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(fake_on_sent, ct_connection_t*, ct_message_context_t*);
FAKE_VOID_FUNC(fake_send_error, ct_connection_t*, ct_message_context_t*, int);
FAKE_VOID_FUNC(fake_ct_message_free, ct_message_t*);
FAKE_VOID_FUNC(fake_ct_message_context_free, ct_message_context_t*);

extern "C" {
    void __wrap_ct_message_free(ct_message_t* message) {
        fake_ct_message_free(message);
    }
    void __wrap_ct_message_context_free(ct_message_context_t* message_context) {
        fake_ct_message_context_free(message_context);
    }
}

class SocketManagerUnitTests : public ::testing::Test {
protected:
    void SetUp() override {
        RESET_FAKE(fake_on_sent);
        RESET_FAKE(fake_send_error);
        RESET_FAKE(fake_ct_message_free);
        RESET_FAKE(fake_ct_message_context_free);
        FFF_RESET_HISTORY();

        dummy_connection.socket_manager = &dummy_socket_manager;

        dummy_connection_without_callbacks = dummy_connection;

        dummy_connection.connection_callbacks.sent = fake_on_sent;
        dummy_connection.connection_callbacks.send_error = fake_send_error;
    }

    
    void TearDown() override {
    }

    ct_socket_manager_t dummy_socket_manager = {0};

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

    ASSERT_EQ(fake_ct_message_context_free_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_context_free_fake.arg0_val, &dummy_message_context);
}

TEST_F(SocketManagerUnitTests, doesNotInvokesConnectionCallbackWhenNotSetButFreesContextOnSent) {
    ct_socket_manager_message_sent_cb(&dummy_connection_without_callbacks, &dummy_message_context);

    ASSERT_EQ(fake_on_sent_fake.call_count, 0);

    ASSERT_EQ(fake_ct_message_context_free_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_context_free_fake.arg0_val, &dummy_message_context);
}

TEST_F(SocketManagerUnitTests, invokesSendErrorOnSendError) {
    ct_socket_manager_message_send_error_cb(&dummy_connection, &dummy_message_context, -100);

    ASSERT_EQ(fake_on_sent_fake.call_count, 0);

    ASSERT_EQ(fake_send_error_fake.call_count, 1);
    ASSERT_EQ(fake_send_error_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(fake_send_error_fake.arg1_val, &dummy_message_context);
    ASSERT_EQ(fake_send_error_fake.arg2_val, -100);


    ASSERT_EQ(fake_ct_message_context_free_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_context_free_fake.arg0_val, &dummy_message_context);
}

TEST_F(SocketManagerUnitTests, doesNotInvokesSendErrorWhenNotSetButFreesContext) {
    ct_socket_manager_message_send_error_cb(&dummy_connection_without_callbacks, &dummy_message_context, -100);

    ASSERT_EQ(fake_send_error_fake.call_count, 0);


    ASSERT_EQ(fake_ct_message_context_free_fake.call_count, 1);
    ASSERT_EQ(fake_ct_message_context_free_fake.arg0_val, &dummy_message_context);
}
