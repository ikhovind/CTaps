#include "gtest/gtest.h"
extern "C" {
  #include "protocol/tcp/tcp.h"
  #include "fff.h"
}

DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(faked_uv_tcp_close_reset, uv_tcp_t*, uv_close_cb);
FAKE_VOID_FUNC(faked_uv_close, uv_handle_t*, uv_close_cb);
FAKE_VOID_FUNC(faked_socket_manager_aborted_connection_cb, ct_connection_t*);
FAKE_VOID_FUNC(faked_socket_manager_closed_connection_cb, ct_connection_t*);

extern "C" {
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

        dummy_connection.socket_manager = &dummy_socket_manager;

        dummy_tcp_handle.data = &dummy_socket_manager;

        RESET_FAKE(faked_socket_manager_aborted_connection_cb);
        RESET_FAKE(faked_socket_manager_closed_connection_cb)


    }

    
    void TearDown() override {
        free(dummy_socket_manager.internal_socket_manager_state);
    }

    ct_socket_manager_t dummy_socket_manager;
    ct_connection_t dummy_connection;
    uv_tcp_t dummy_tcp_handle;
    uv_buf_t dummy_buf;
};

TEST_F(TcpUnitTest, SocketManagerAbortCalledOnAbort) {
    tcp_protocol_interface.abort(&dummy_connection);
    // We should close the connection, with the correct handle
    ASSERT_EQ(1, faked_uv_tcp_close_reset_fake.call_count);
    ASSERT_EQ(&dummy_tcp_handle, faked_uv_tcp_close_reset_fake.arg0_val);

    // Then in the callback, we should have invoked the socket manager callback, with the correct connection
    ASSERT_EQ(1, faked_socket_manager_aborted_connection_cb_fake.call_count);
    ASSERT_EQ(&dummy_connection, faked_socket_manager_aborted_connection_cb_fake.arg0_val);
}

TEST_F(TcpUnitTest, SocketManagerCloseCalledOnClose) {
    tcp_protocol_interface.close(&dummy_connection);
    // We should close the connection, with the correct handle
    ASSERT_EQ(1, faked_uv_close_fake.call_count);
    ASSERT_EQ((uv_handle_t*)&dummy_tcp_handle, faked_uv_close_fake.arg0_val);

    // Then in the callback, we should have invoked the socket manager callback, with the correct connection
    ASSERT_EQ(1, faked_socket_manager_closed_connection_cb_fake.call_count);
    ASSERT_EQ(&dummy_connection, faked_socket_manager_closed_connection_cb_fake.arg0_val);
}

TEST_F(TcpUnitTest, SocketManagerCloseCalledOnCloseByPeer) {
    tcp_on_read((uv_stream_t*)&dummy_tcp_handle, UV_EOF, &dummy_buf);

    // We should close the connection, with the correct handle
    ASSERT_EQ(1, faked_uv_close_fake.call_count);
    ASSERT_EQ((uv_handle_t*)&dummy_tcp_handle, faked_uv_close_fake.arg0_val);

    // Then in the callback, we should have invoked the socket manager callback, with the correct connection
    ASSERT_EQ(1, faked_socket_manager_closed_connection_cb_fake.call_count);
    ASSERT_EQ(&dummy_connection, faked_socket_manager_closed_connection_cb_fake.arg0_val);
}

TEST_F(TcpUnitTest, SocketManagerAbortCalledOnResetByPeer) {
    tcp_on_read((uv_stream_t*)&dummy_tcp_handle, UV_ECONNRESET, &dummy_buf);

    // We should close the connection, with the correct handle
    ASSERT_EQ(1, faked_uv_tcp_close_reset_fake.call_count);
    ASSERT_EQ(&dummy_tcp_handle, faked_uv_tcp_close_reset_fake.arg0_val);

    // Then in the callback, we should have invoked the socket manager callback, with the correct connection
    ASSERT_EQ(1, faked_socket_manager_aborted_connection_cb_fake.call_count);
    ASSERT_EQ(&dummy_connection, faked_socket_manager_aborted_connection_cb_fake.arg0_val);
}
