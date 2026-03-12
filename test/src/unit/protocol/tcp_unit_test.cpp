#include "gtest/gtest.h"
extern "C" {
  #include "protocol/tcp/tcp.h"
  #include "fff.h"
DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(faked_uv_tcp_close_reset, uv_tcp_t*, uv_close_cb);
FAKE_VOID_FUNC(faked_uv_close, uv_handle_t*, uv_close_cb);
FAKE_VOID_FUNC(faked_socket_manager_aborted_connection_cb, ct_connection_t*);
FAKE_VOID_FUNC(faked_socket_manager_closed_connection_cb, ct_connection_t*);
FAKE_VOID_FUNC(faked_message_send_error, ct_connection_t*, ct_message_context_t*, int);
FAKE_VOID_FUNC(faked_message_free, ct_message_t*);
FAKE_VALUE_FUNC(int, __wrap_uv_is_closing, const uv_handle_t*);
}



extern "C" {
  void __real_free(void*);

  void __wrap_ct_message_free(ct_message_t* message) {
    faked_message_free(message);
  }

  void __wrap_free(void*) {
  }

  int __wrap_uv_tcp_close_reset(uv_tcp_t* handle, uv_close_cb close_cb) {
    faked_uv_tcp_close_reset(handle, close_cb);
    close_cb((uv_handle_t*)handle);
    return 0;
  }


  int __wrap_uv_close(uv_handle_t* handle, uv_close_cb close_cb) {
    faked_uv_close(handle, close_cb);
    close_cb(handle);
    return 0;
  }

  extern void tcp_on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf);
  extern void on_write(uv_write_t* req, int status);
}

class TcpUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        dummy_socket_manager.internal_socket_manager_state = ct_tcp_socket_state_new(
                                                &dummy_connection,
                                                NULL,
                                                NULL,
                                                NULL,
                                                NULL,
                                                &dummy_tcp_handle
        );
        dummy_socket_manager.callbacks.aborted_connection = faked_socket_manager_aborted_connection_cb;
        dummy_socket_manager.callbacks.closed_connection = faked_socket_manager_closed_connection_cb;
        dummy_socket_manager.callbacks.message_send_error = faked_message_send_error;
        __wrap_uv_is_closing_fake.return_val = 0;

        dummy_connection.socket_manager = &dummy_socket_manager;

        dummy_tcp_handle.data = &dummy_socket_manager;

        dummy_write_req.handle = (uv_stream_t*)&dummy_tcp_handle;
        dummy_write_req.data = &dummy_send_data;

        dummy_send_data.connection = &dummy_connection;
        dummy_send_data.message_context = &dummy_message_context;
        dummy_send_data.message = &dummy_message;

        RESET_FAKE(faked_socket_manager_aborted_connection_cb);
        RESET_FAKE(faked_socket_manager_closed_connection_cb)
        FFF_RESET_HISTORY();
    }

    
    void TearDown() override {
        __real_free(dummy_socket_manager.internal_socket_manager_state);
    }

    ct_socket_manager_t dummy_socket_manager;
    ct_connection_t dummy_connection;
    uv_tcp_t dummy_tcp_handle;
    uv_buf_t dummy_buf;
    uv_write_t dummy_write_req;
    ct_tcp_send_data_t dummy_send_data = {0};

    ct_message_t dummy_message = {0};
    ct_message_context_t dummy_message_context = {0};
};

TEST_F(TcpUnitTest, socketManagerAbortCalledOnAbort) {
    tcp_protocol_interface.abort(&dummy_connection);
    // We should close the connection, with the correct handle
    ASSERT_EQ(1, faked_uv_tcp_close_reset_fake.call_count);
    ASSERT_EQ(&dummy_tcp_handle, faked_uv_tcp_close_reset_fake.arg0_val);

    // Then in the callback, we should have invoked the socket manager callback, with the correct connection
    ASSERT_EQ(1, faked_socket_manager_aborted_connection_cb_fake.call_count);
    ASSERT_EQ(&dummy_connection, faked_socket_manager_aborted_connection_cb_fake.arg0_val);
}

TEST_F(TcpUnitTest, socketManagerCloseCalledOnClose) {
    tcp_protocol_interface.close_connection(&dummy_connection);
    // We should close the connection, with the correct handle
    ASSERT_EQ(1, faked_uv_close_fake.call_count);
    ASSERT_EQ((uv_handle_t*)&dummy_tcp_handle, faked_uv_close_fake.arg0_val);

    // Then in the callback, we should have invoked the socket manager callback, with the correct connection
    ASSERT_EQ(1, faked_socket_manager_closed_connection_cb_fake.call_count);
    ASSERT_EQ(&dummy_connection, faked_socket_manager_closed_connection_cb_fake.arg0_val);
}

TEST_F(TcpUnitTest, socketManagerCloseCalledOnCloseByPeer) {
    tcp_on_read((uv_stream_t*)&dummy_tcp_handle, UV_EOF, &dummy_buf);

    // We should close the connection, with the correct handle
    ASSERT_EQ(1, faked_uv_close_fake.call_count);
    ASSERT_EQ((uv_handle_t*)&dummy_tcp_handle, faked_uv_close_fake.arg0_val);

    // Then in the callback, we should have invoked the socket manager callback, with the correct connection
    ASSERT_EQ(1, faked_socket_manager_closed_connection_cb_fake.call_count);
    ASSERT_EQ(&dummy_connection, faked_socket_manager_closed_connection_cb_fake.arg0_val);
}

TEST_F(TcpUnitTest, socketManagerAbortCalledOnResetByPeer) {
    tcp_on_read((uv_stream_t*)&dummy_tcp_handle, UV_ECONNRESET, &dummy_buf);

    // We should close the connection, with the correct handle
    ASSERT_EQ(1, faked_uv_tcp_close_reset_fake.call_count);
    ASSERT_EQ(&dummy_tcp_handle, faked_uv_tcp_close_reset_fake.arg0_val);

    // Then in the callback, we should have invoked the socket manager callback, with the correct connection
    ASSERT_EQ(1, faked_socket_manager_aborted_connection_cb_fake.call_count);
    ASSERT_EQ(&dummy_connection, faked_socket_manager_aborted_connection_cb_fake.arg0_val);
}

TEST_F(TcpUnitTest, sendErrorInvokedOnWriteError) {
    on_write(&dummy_write_req, UV_ECONNRESET);

    ASSERT_EQ(faked_message_send_error_fake.call_count, 1);
    ASSERT_EQ(faked_message_send_error_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(faked_message_send_error_fake.arg1_val, &dummy_message_context);
    ASSERT_EQ(faked_message_send_error_fake.arg2_val, UV_ECONNRESET);
    ASSERT_EQ(faked_message_free_fake.call_count, 1);
}
