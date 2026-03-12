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
FAKE_VALUE_FUNC(int, fake_set_connection_priority, ct_connection_t*, uint8_t);
FAKE_VALUE_FUNC(GSList*, __wrap_g_slist_prepend, GSList*, gpointer);
FAKE_VALUE_FUNC(int, fake_listen, ct_socket_manager_t*);
FAKE_VOID_FUNC(fake_listener_ready, ct_listener_t*);
FAKE_VOID_FUNC(fake_establishment_error, ct_listener_t*, int);
FAKE_VALUE_FUNC(int, fake_close, ct_connection_t*);
}

class SocketManagerUnitTests : public ::testing::Test {
protected:
    void SetUp() override {
        RESET_FAKE(fake_on_sent);
        RESET_FAKE(fake_send_error);
        RESET_FAKE(__wrap_ct_message_free);
        RESET_FAKE(__wrap_ct_message_context_free);
        RESET_FAKE(fake_set_connection_priority);
        RESET_FAKE(__wrap_g_slist_prepend);
        RESET_FAKE(fake_listen);
        RESET_FAKE(fake_listener_ready);
        RESET_FAKE(fake_establishment_error);
        RESET_FAKE(fake_close);
        FFF_RESET_HISTORY();

        fake_set_connection_priority_fake.return_val = 0;

        dummy_protocol_impl.name = "dummy_protocol";
        dummy_protocol_impl.set_connection_priority = fake_set_connection_priority;
        dummy_protocol_impl.listen = fake_listen;
        dummy_protocol_impl.close = fake_close;

        dummy_socket_manager.protocol_impl = &dummy_protocol_impl;

        dummy_connection.socket_manager = &dummy_socket_manager;

        dummy_connection_without_callbacks = dummy_connection;

        dummy_connection.connection_callbacks.sent = fake_on_sent;
        dummy_connection.connection_callbacks.send_error = fake_send_error;

        dummy_socket_manager_without_support.protocol_impl = &dummy_protocol_without_support;


        dummy_socket_manager.protocol_impl = &dummy_protocol_impl;

        dummy_listener.listener_callbacks.listener_ready      = fake_listener_ready;
        dummy_listener.listener_callbacks.establishment_error = fake_establishment_error;
        dummy_listener.socket_manager = &dummy_socket_manager;
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
    ct_listener_t       dummy_listener       = {0};
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

TEST_F(SocketManagerUnitTests, addConnectionDoesNothingOnNullSocketManager) {
    ct_socket_manager_add_connection(NULL, &dummy_connection);

    ASSERT_EQ(__wrap_g_slist_prepend_fake.call_count, 0);
    ASSERT_EQ(dummy_connection.socket_manager->ref_count, 0);
}

TEST_F(SocketManagerUnitTests, addConnectionDoesNothingOnNullConnection) {
    ct_socket_manager_add_connection(&dummy_socket_manager, NULL);

    ASSERT_EQ(__wrap_g_slist_prepend_fake.call_count, 0);
    ASSERT_EQ(dummy_socket_manager.ref_count, 0);
}

TEST_F(SocketManagerUnitTests, addConnectionPrependsToList) {
    GSList* fake_in_list = (GSList*)0xdeadcafe;
    GSList* fake_out_list = (GSList*)0xdeadbeef;
    dummy_socket_manager.all_connections = fake_in_list;

    __wrap_g_slist_prepend_fake.return_val = fake_out_list;

    ct_socket_manager_add_connection(&dummy_socket_manager, &dummy_connection);

    ASSERT_EQ(__wrap_g_slist_prepend_fake.call_count, 1);
    ASSERT_EQ(__wrap_g_slist_prepend_fake.arg0_val, fake_in_list);
    ASSERT_EQ(__wrap_g_slist_prepend_fake.arg1_val, &dummy_connection);
    ASSERT_EQ(dummy_socket_manager.all_connections, fake_out_list);
}

TEST_F(SocketManagerUnitTests, addConnectionSetsSocketManagerViaRef) {
    dummy_connection.socket_manager = NULL;
    ct_socket_manager_add_connection(&dummy_socket_manager, &dummy_connection);

    ASSERT_EQ(dummy_connection.socket_manager, &dummy_socket_manager);
    ASSERT_EQ(dummy_socket_manager.ref_count, 1);
}

TEST_F(SocketManagerUnitTests, nullListener_doesNotCallProtocol) {
    ct_socket_manager_listen(nullptr);

    ASSERT_EQ(fake_listen_fake.call_count, 0);
}

TEST_F(SocketManagerUnitTests, listenFails_callsEstablishmentErrorWithCorrectArgs) {
    fake_listen_fake.return_val = -1;
    dummy_listener.listener_callbacks.establishment_error = fake_establishment_error;

    ct_socket_manager_listen(&dummy_listener);

    ASSERT_EQ(fake_establishment_error_fake.call_count, 1);
    ASSERT_EQ(fake_establishment_error_fake.arg0_val, &dummy_listener);
    ASSERT_EQ(fake_establishment_error_fake.arg1_val, -1);
    ASSERT_EQ(fake_listener_ready_fake.call_count, 0);
}

TEST_F(SocketManagerUnitTests, listenFails_noEstablishmentErrorCallback_doesNotCrash) {
    fake_listen_fake.return_val                           = -1;
    dummy_listener.listener_callbacks.establishment_error = nullptr;

    ASSERT_NO_FATAL_FAILURE(ct_socket_manager_listen(&dummy_listener));

    ASSERT_EQ(fake_listener_ready_fake.call_count, 0);
}

TEST_F(SocketManagerUnitTests, listenSucceeds_callsListenerReadyWithCorrectArgs) {
    fake_listen_fake.return_val                      = 0;
    dummy_listener.listener_callbacks.listener_ready = fake_listener_ready;

    ct_socket_manager_listen(&dummy_listener);

    ASSERT_EQ(fake_listener_ready_fake.call_count, 1);
    ASSERT_EQ(fake_listener_ready_fake.arg0_val, &dummy_listener);
    ASSERT_EQ(fake_establishment_error_fake.call_count, 0);
}

TEST_F(SocketManagerUnitTests, listenSucceeds_noListenerReadyCallback_doesNotCrash) {
    fake_listen_fake.return_val                      = 0;
    dummy_listener.listener_callbacks.listener_ready = nullptr;

    ASSERT_NO_FATAL_FAILURE(ct_socket_manager_listen(&dummy_listener));

    ASSERT_EQ(fake_establishment_error_fake.call_count, 0);
}

TEST_F(SocketManagerUnitTests, passesSocketManagerNotListenerToProtocol) {
    ct_socket_manager_listen(&dummy_listener);

    ASSERT_EQ(fake_listen_fake.call_count, 1);
    ASSERT_EQ(fake_listen_fake.arg0_val, &dummy_socket_manager);
}

TEST_F(SocketManagerUnitTests, closeConnection_nullConnection_returnsEinval) {
    int rc = ct_socket_manager_close_connection(NULL);

    ASSERT_EQ(rc, -EINVAL);
    ASSERT_EQ(fake_close_fake.call_count, 0);
}

TEST_F(SocketManagerUnitTests, closeConnection_protocolFails_propagatesError) {
    fake_close_fake.return_val = -EIO;

    int rc = ct_socket_manager_close_connection(&dummy_connection);

    ASSERT_EQ(rc, -EIO);
    ASSERT_EQ(fake_close_fake.call_count, 1);
    ASSERT_EQ(fake_close_fake.arg0_val, &dummy_connection);
}

TEST_F(SocketManagerUnitTests, closeConnection_protocolSucceeds_returnsZero) {
    fake_close_fake.return_val = 0;

    int rc = ct_socket_manager_close_connection(&dummy_connection);

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(fake_close_fake.call_count, 1);
    ASSERT_EQ(fake_close_fake.arg0_val, &dummy_connection);
}
